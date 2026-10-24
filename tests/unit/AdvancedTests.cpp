#include "core/catalog/Catalog.h"
#include "core/edits/EditHistory.h"
#include "core/render/AdvancedColor.h"
#include "core/render/DevelopPipeline.h"
#include "tests/unit/LegacyCatalogFixture.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sqlite3.h>
#include <unistd.h>

namespace {
void require(bool result, const char *message) {
    if (!result)
        throw std::runtime_error(message);
}
template <typename F> void throws(F function) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Expected an exception.");
}
void sql(const std::filesystem::path &path, const char *text) {
    sqlite3 *raw = nullptr;
    const int opened = sqlite3_open(path.c_str(), &raw);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
    require(opened == SQLITE_OK, "Cannot open test catalog.");
    require(sqlite3_exec(db.get(), text, nullptr, nullptr, nullptr) == SQLITE_OK,
            "Cannot prepare catalog fixture.");
}
void colorTests() {
    const luma::ToneCurve identity;
    for (double x : {-2.0, -0.1, 0.0, 0.1, 0.5, 1.0, 2.0, 8.0})
        require(std::abs(identity.value(x) - x) < 1e-12,
                "Identity curve changed signed/HDR input.");
    const luma::PointCurve points{{{0, 0.05}, {0.25, 0.1}, {0.5, 0.65}, {0.75, 0.9}, {1, 1}}};
    const luma::ToneCurve curve(points);
    double previous = curve.value(0);
    for (int i = 1; i <= 10000; ++i) {
        const auto value = curve.value(static_cast<double>(i) / 10000);
        require(value >= previous - 1e-12 && value >= 0.05 && value <= 1,
                "Monotone curve overshot or inverted tones.");
        previous = value;
    }
    for (const auto &point : points.points)
        require(std::abs(curve.value(point.x) - point.y) < 1e-12,
                "Curve did not pass through its control point.");
    throws([] {
        luma::ToneCurve invalid(luma::PointCurve{{{0, 0}, {0.5, 0.2}, {0.5, 0.8}, {1, 1}}});
    });
    throws([] { luma::ToneCurve invalid(luma::PointCurve{{{0.1, 0}, {1, 1}}}); });
    for (int mask = 0; mask < 16; ++mask) {
        luma::EditDocument edits;
        edits.curveShadows = mask & 1 ? 100 : -100;
        edits.curveDarks = mask & 2 ? 100 : -100;
        edits.curveLights = mask & 4 ? 100 : -100;
        edits.curveHighlights = mask & 8 ? 100 : -100;
        previous = 0;
        for (int i = 1; i <= 4096; ++i) {
            const auto value = luma::parametricCurve(static_cast<double>(i) / 4096, edits);
            require(value >= previous && value <= 1, "Parametric extremes inverted tones.");
            previous = value;
        }
        require(luma::parametricCurve(-0.1, edits) < 0 && luma::parametricCurve(2, edits) > 1,
                "Parametric curve clipped signed/HDR values.");
    }
    for (const std::array<double, 3> rgb : {std::array{1.0, 0.0, 0.0}, std::array{0.1, 0.3, 0.8},
                                            std::array{0.0, 1.0, 0.2}, std::array{0.7, 0.7, 0.7}}) {
        const auto result = luma::fromHSL(luma::toHSL(rgb));
        for (std::size_t c = 0; c < 3; ++c)
            require(std::abs(result[c] - rgb[c]) < 1e-12, "HSL round trip changed RGB.");
    }
    std::array<luma::HSLBand, 8> bands{};
    bands[0].hue = 100;
    const auto orange = luma::mixHSL({1, 0, 0}, bands);
    require(std::abs(orange[0] - 1) < 1e-12 && std::abs(orange[1] - 0.5) < 1e-12 && orange[2] == 0,
            "Red Hue +100 must rotate by 30 degrees.");
    for (auto &band : bands)
        band = {100, 100, 100};
    const auto gray = luma::mixHSL({0.4, 0.4, 0.4}, bands);
    require(gray == std::array{0.4, 0.4, 0.4}, "Color mixer changed a neutral pixel.");
    require(luma::hueWeights(0) == luma::hueWeights(360), "Hue weights did not wrap around red.");
    for (double hue : {0.0, 15.0, 89.0, 180.0, 299.0, 359.999}) {
        double sum = 0;
        for (double weight : luma::hueWeights(hue)) {
            require(weight >= 0 && weight <= 1, "Hue weight out of range.");
            sum += weight;
        }
        require(std::abs(sum - 1) < 1e-12, "Hue bands do not form a partition.");
    }
    luma::EditDocument edits;
    const std::array<float, 3> hdr{3, -0.1F, 0.25F};
    const auto baseline = luma::DevelopPipeline(edits).pixel(hdr);
    edits.curves[1] = {{{0, 0.05}, {0.4, 0.7}, {1, 1}}};
    edits.setParameter("hsl.red.saturation", -30);
    edits.setEnabled(luma::Correction::curves, false);
    edits.setEnabled(luma::Correction::hsl, false);
    require(luma::DevelopPipeline(edits).pixel(hdr) == baseline,
            "Disabled advanced controls changed rendering.");
    const auto saved = edits;
    throws([&] { edits.setParameter("hsl.red.hue", std::numeric_limits<double>::quiet_NaN()); });
    throws([&] { edits.setParameter("hsl.gold.hue", 2); });
    require(edits == saved, "Invalid HSL edit changed the document.");
    luma::EditHistory history;
    history.begin();
    history.apply(edits);
    edits.setParameter("curve.shadows", 35);
    history.apply(edits);
    history.end();
    history.undo();
    require(history.current() == luma::EditDocument{},
            "One gesture did not undo all advanced edits.");
    history.redo();
    require(history.current() == edits, "Redo lost advanced edits.");
}
void catalogTests() {
    std::string pattern =
        (std::filesystem::temp_directory_path() / "luma-advanced-XXXXXX").string();
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back(0);
    require(mkdtemp(name.data()) != nullptr, "Cannot create advanced fixture directory.");
    const std::filesystem::path folder(name.data()), source = folder / "fixture.DNG",
                                                     path = folder / "catalog.sqlite";
    auto cleanup = [&] {
        std::error_code error;
        std::filesystem::remove_all(folder, error);
    };
    try {
        std::ofstream(source).put('x');
        luma::PhotoMetadata metadata;
        metadata.width = 128;
        metadata.height = 96;
        std::string id;
        luma::EditDocument original;
        original.exposure = 1.25;
        original.temperature = 5100;
        original.customWhiteBalance = true;
        original.setEnabled(luma::Correction::shadows, false);
        {
            luma::Catalog catalog(path);
            id = catalog.addAndSelect(source, "fixture-checksum", metadata).id;
            catalog.saveEdits(id, original);
            catalog.setRating({id}, 4);
        }
        sql(path, fixture::removePostV11Tables);
        sql(path, R"sql(
            BEGIN;
            CREATE TABLE historical_edits(
                asset_id TEXT PRIMARY KEY REFERENCES assets(id) ON DELETE CASCADE,
                schema_version INTEGER NOT NULL DEFAULT 2 CHECK(schema_version=2),
                exposure REAL NOT NULL DEFAULT 0,temperature REAL NOT NULL DEFAULT 6500,
                tint REAL NOT NULL DEFAULT 0,custom_wb INTEGER NOT NULL DEFAULT 0,
                contrast REAL NOT NULL DEFAULT 0,highlights REAL NOT NULL DEFAULT 0,
                shadows REAL NOT NULL DEFAULT 0,whites REAL NOT NULL DEFAULT 0,
                blacks REAL NOT NULL DEFAULT 0,vibrance REAL NOT NULL DEFAULT 0,saturation REAL NOT NULL DEFAULT 0,
                enabled INTEGER NOT NULL DEFAULT 511 CHECK(enabled BETWEEN 0 AND 511),
                revision INTEGER NOT NULL DEFAULT 0,updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
            INSERT INTO historical_edits(asset_id,exposure,temperature,tint,custom_wb,contrast,highlights,shadows,whites,blacks,vibrance,saturation,enabled,revision,updated_at)
                SELECT asset_id,exposure,temperature,tint,custom_wb,contrast,highlights,shadows,whites,blacks,vibrance,saturation,enabled&511,revision,updated_at FROM edit_documents;
            DROP TRIGGER initialize_local_masks; DROP TABLE edit_local_masks;
            DROP TABLE edit_documents; ALTER TABLE historical_edits RENAME TO edit_documents;
            DROP TABLE edit_curve_points; DROP TABLE edit_hsl_bands;
            DELETE FROM schema_migrations WHERE version>=4; PRAGMA user_version=3;
            COMMIT;
        )sql");
        {
            luma::Catalog catalog(path);
            require(catalog.activeAsset()->id == id && catalog.activeAsset()->edits == original,
                    "Catalog 3→4 migration lost edits or selection.");
            require(catalog.libraryPage({}, 0, 1).front().rating == 4,
                    "Migration lost library metadata.");
            original.curves[0] = {{{0, 0}, {0.25, 0.15}, {0.75, 0.9}, {1, 1}}};
            original.curves[2] = {{{0, 0.02}, {1, 1}}};
            original.setParameter("curve.lights", 12);
            original.setParameter("hsl.orange.hue", -25);
            original.setParameter("hsl.blue.luminance", 40);
            catalog.saveEdits(id, original);
        }
        luma::Catalog catalog(path);
        require(catalog.activeAsset()->edits == original, "Restart lost advanced edit parameters.");
        sql(path, "CREATE TRIGGER fail_curves BEFORE INSERT ON edit_curve_points BEGIN SELECT "
                  "RAISE(ABORT,'injected write error'); END;");
        auto next = original;
        next.exposure = -2;
        next.curves[0].points[1].y = 0.4;
        throws([&] { catalog.saveEdits(id, next); });
        require(catalog.activeAsset()->edits == original,
                "Failed advanced write partially committed basic or curve edits.");
        sql(path, "DROP TRIGGER fail_curves;");
        catalog.saveExposure(id, 0.5);
        original.exposure = 0.5;
        require(catalog.activeAsset()->edits == original,
                "Exposure-only update erased curves or HSL.");
        sql(path, "DELETE FROM edit_hsl_bands WHERE band=4;");
        throws([&] { catalog.activeAsset(); });
        catalog.saveEdits(id, original);
        sql(path, "DELETE FROM edit_curve_points WHERE channel=0 AND position=1;");
        throws([&] { catalog.activeAsset(); });
        catalog.saveEdits(id, original);
        catalog.verify();
        cleanup();
    } catch (...) {
        cleanup();
        throw;
    }
}
} // namespace
int main() {
    try {
        colorTests();
        catalogTests();
        std::cout << "Advanced color, history, migration, durability and rollback passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
