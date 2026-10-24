#include "core/catalog/Catalog.h"
#include "core/catalog/SqliteStatement.h"
#include "core/presets/SettingsSubset.h"
#include <algorithm>
#include <array>
#include <sqlite3.h>

namespace luma {
namespace {
using detail::Statement;
constexpr std::array tables{"edit_documents",   "edit_curve_points", "edit_hsl_bands",
                            "edit_local_masks", "edit_retouch",      "edit_creative"};
constexpr std::array columns{"schema_version",
                             "exposure",
                             "temperature",
                             "tint",
                             "custom_wb",
                             "contrast",
                             "highlights",
                             "shadows",
                             "whites",
                             "blacks",
                             "vibrance",
                             "saturation",
                             "enabled",
                             "curve_shadows",
                             "curve_darks",
                             "curve_lights",
                             "curve_highlights",
                             "advanced_saved",
                             "sharpen_amount",
                             "sharpen_radius",
                             "sharpen_detail",
                             "sharpen_masking",
                             "noise_luminance",
                             "noise_luminance_detail",
                             "noise_luminance_contrast",
                             "noise_color",
                             "noise_color_detail",
                             "noise_color_smoothness",
                             "crop_x",
                             "crop_y",
                             "crop_width",
                             "crop_height",
                             "crop_angle",
                             "crop_aspect",
                             "crop_quarter_turns",
                             "crop_flip_h",
                             "crop_flip_v",
                             "lens_profile",
                             "lens_distortion",
                             "lens_vignetting",
                             "lens_ca",
                             "transform_rotate",
                             "transform_horizontal",
                             "transform_vertical",
                             "transform_aspect",
                             "transform_scale",
                             "transform_x_offset",
                             "transform_y_offset",
                             "transform_constrain_crop",
                             "transform_guided",
                             "transform_m11",
                             "transform_m12",
                             "transform_m13",
                             "transform_m21",
                             "transform_m22",
                             "transform_m23",
                             "transform_m31",
                             "transform_m32"};
class InterruptibleSQL {
  public:
    InterruptibleSQL(sqlite3 *db, CancellationToken stop) : db_(db), stop_(std::move(stop)) {
        sqlite3_progress_handler(
            db_, 1000,
            [](void *context) {
                return static_cast<InterruptibleSQL *>(context)->stop_.isCancelled() ? 1 : 0;
            },
            this);
    }
    ~InterruptibleSQL() {
        clear();
    }
    void clear() {
        sqlite3_progress_handler(db_, 0, nullptr, nullptr);
    }

  private:
    sqlite3 *db_;
    CancellationToken stop_;
};
void check(const CancellationToken &stop) {
    if (stop.isCancelled())
        throw std::runtime_error("Batch editing cancelled.");
}
std::size_t count(sqlite3 *db) {
    Statement query(db, "SELECT COUNT(*) FROM temp.batch_changed");
    if (!query.row())
        throw std::runtime_error("Cannot read batch journal.");
    return static_cast<std::size_t>(query.integer(0));
}
std::string editFields() {
    std::string result = "asset_id";
    for (const auto *column : columns)
        result += "," + std::string(column);
    return result;
}
} // namespace
BatchEditResult Catalog::applyBatchEdits(const std::vector<std::string> &ids,
                                         const EditDocument &source, std::uint32_t groups,
                                         CancellationToken stop,
                                         const std::function<void(double)> &progress,
                                         const std::string &activeID,
                                         std::span<const std::string> parameters) {
    validateSettingsGroups(groups);
    source.validate();
    if (!parameters.empty())
        parameterSelectionGroups(parameters);
    if (ids.empty())
        throw std::runtime_error("Select photographs before applying settings.");
    check(stop);
    // The session journal is disk-backed, with a bounded page cache. A single
    // connection retains only the latest successful batch, not a RAM edit array.
    if (!batchStorageConfigured_) {
        execute("PRAGMA temp_store=FILE; PRAGMA temp.cache_size=-4096;");
        batchStorageConfigured_ = true;
    }
    InterruptibleSQL interrupt(db_, stop);
    try {
        execute("BEGIN IMMEDIATE;");
        execute("DROP TABLE IF EXISTS temp.batch_targets; DROP TABLE IF EXISTS temp.batch_changed;"
                "CREATE TEMP TABLE batch_targets(asset_id TEXT PRIMARY KEY);"
                "CREATE TEMP TABLE batch_changed(asset_id TEXT PRIMARY KEY);");
        for (const auto &id : ids) {
            check(stop);
            Statement insert(db_, "INSERT INTO temp.batch_targets VALUES(?1)");
            insert.text(1, id);
            insert.row();
        }
        for (const auto *table : tables) {
            for (const auto *phase : {"before", "after"}) {
                const auto journal = "batch_" + std::string(phase) + "_" + table;
                execute(("DROP TABLE IF EXISTS temp." + journal + ";").c_str());
            }
            const auto journal = "batch_before_" + std::string(table);
            execute(("CREATE TEMP TABLE " + journal + " AS SELECT m.* FROM main." + table +
                     " m JOIN temp.batch_targets t ON t.asset_id=m.asset_id;")
                        .c_str());
        }
        const auto interval = std::max<std::size_t>(1, ids.size() / 1000);
        for (std::size_t index = 0; index < ids.size(); ++index) {
            check(stop);
            const auto asset = assetByID(ids[index]);
            if (!asset)
                throw std::runtime_error("Batch target is missing from the catalog.");
            const auto next = mergeSettings(asset->edits, source, groups, parameters);
            if (next != asset->edits) {
                writeEdits(ids[index], next);
                Statement changed(db_, "INSERT INTO temp.batch_changed VALUES(?1)");
                changed.text(1, ids[index]);
                changed.row();
            }
            if (progress && (index % interval == 0 || index + 1 == ids.size()))
                progress(static_cast<double>(index + 1) / static_cast<double>(ids.size()));
        }
        const auto changed = count(db_);
        if (!changed) {
            interrupt.clear();
            execute("ROLLBACK;"); // Keep the previous batch's undo journal on a no-op.
            return {};
        }
        for (const auto *table : tables) {
            const auto before = "batch_before_" + std::string(table);
            const auto after = "batch_after_" + std::string(table);
            execute(("DELETE FROM temp." + before +
                     " WHERE asset_id NOT IN (SELECT asset_id FROM temp.batch_changed);")
                        .c_str());
            execute(("CREATE TEMP TABLE " + after + " AS SELECT m.* FROM main." + table +
                     " m JOIN temp.batch_changed t ON t.asset_id=m.asset_id;")
                        .c_str());
            for (const auto &journal : {before, after})
                execute(("CREATE INDEX temp." + journal + "_asset ON " + journal + "(asset_id);")
                            .c_str());
        }
        execute("DROP TABLE temp.batch_targets;");
        check(stop);
        const auto active = batchActiveEdits(activeID);
        execute("COMMIT;");
        batchState_ = BatchState::applied;
        return {changed, active};
    } catch (...) {
        interrupt.clear(); // Cancellation must never interrupt rollback itself.
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
bool Catalog::batchAffects(const std::string &id) const {
    if (batchState_ == BatchState::none)
        return false;
    Statement query(db_, "SELECT 1 FROM temp.batch_changed WHERE asset_id=?1");
    query.text(1, id);
    return query.row();
}
std::optional<EditDocument> Catalog::batchActiveEdits(const std::string &id) const {
    if (id.empty())
        return {};
    Statement query(db_, "SELECT 1 FROM temp.batch_changed WHERE asset_id=?1");
    query.text(1, id);
    if (!query.row())
        return {};
    const auto asset = assetByID(id);
    if (!asset)
        throw std::runtime_error("Active batch target is missing from the catalog.");
    return asset->edits;
}
BatchEditResult Catalog::restoreBatchEdits(bool redo, CancellationToken stop,
                                           const std::string &activeID) {
    if (redo ? !canRedoBatch() : !canUndoBatch())
        throw std::runtime_error("No batch operation is available to undo or redo.");
    check(stop);
    InterruptibleSQL interrupt(db_, stop);
    try {
        execute("BEGIN IMMEDIATE;");
        const std::string expected = redo ? "before" : "after";
        const std::string destination = redo ? "after" : "before";
        for (const auto *table : tables) {
            const auto fields = std::string(table) == "edit_documents" ? editFields() : "*";
            const auto current = "SELECT " + fields + " FROM main." + table +
                                 " WHERE asset_id IN (SELECT asset_id FROM temp.batch_changed)";
            const auto snapshot = "SELECT " + fields + " FROM temp.batch_" + expected + "_" + table;
            for (const auto &comparison :
                 {current + " EXCEPT " + snapshot, snapshot + " EXCEPT " + current}) {
                Statement difference(db_, comparison.c_str());
                if (difference.row())
                    throw std::runtime_error(
                        "Batch Undo/Redo conflicts with newer edits. No photos were changed.");
            }
        }
        std::string assignments;
        for (const auto *column : columns) {
            if (!assignments.empty())
                assignments += ",";
            assignments += std::string(column) + "=(SELECT " + column + " FROM temp.batch_" +
                           destination +
                           "_edit_documents s WHERE s.asset_id=edit_documents.asset_id)";
        }
        execute(("UPDATE main.edit_documents SET " + assignments +
                 ",revision=revision+1,updated_at=CURRENT_TIMESTAMP"
                 " WHERE asset_id IN (SELECT asset_id FROM temp.batch_changed);")
                    .c_str());
        for (const auto *table : {"edit_curve_points", "edit_hsl_bands", "edit_local_masks",
                                  "edit_retouch", "edit_creative"}) {
            execute(("DELETE FROM main." + std::string(table) +
                     " WHERE asset_id IN (SELECT asset_id FROM temp.batch_changed);")
                        .c_str());
            execute(("INSERT INTO main." + std::string(table) + " SELECT * FROM temp.batch_" +
                     destination + "_" + table + ";")
                        .c_str());
        }
        const auto changed = count(db_);
        check(stop);
        const auto active = batchActiveEdits(activeID);
        execute("COMMIT;");
        batchState_ = redo ? BatchState::applied : BatchState::undone;
        return {changed, active};
    } catch (...) {
        interrupt.clear();
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
} // namespace luma
