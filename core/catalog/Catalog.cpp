#ifdef LUMA_UX_PERSISTENCE_PROBE
#include "tests/ux/PersistenceProbe.h"
#endif
#include "core/catalog/Catalog.h"
#include "core/catalog/CreativeStorage.h"
#include "core/catalog/Identity.h"
#include "core/catalog/LocalMaskStorage.h"
#include "core/catalog/MetadataStorage.h"
#include "core/catalog/RetouchStorage.h"
#include "core/catalog/SqliteStatement.h"
#include "core/image/Bayer.h"
#include "core/render/TransformMapping.h"
#include <array>
#include <chrono>
#include <iomanip>
#include <random>
#include <sqlite3.h>
#include <sstream>
#include <thread>

namespace luma {
namespace {
using detail::Statement;
AssetRecord readAsset(sqlite3 *db, const Statement &query) {
    PhotoMetadata metadata;
    metadata.camera = query.string(3);
    metadata.lens = query.string(4);
    metadata.iso = query.optional(5);
    metadata.shutterSeconds = query.optional(6);
    metadata.aperture = query.optional(7);
    metadata.focalLength = query.optional(8);
    metadata.width = static_cast<int>(query.value(9));
    metadata.height = static_cast<int>(query.value(10));
    metadata.orientation = static_cast<int>(query.value(11));
    if (auto timestamp = query.optional(12)) {
        metadata.captureTimestamp = static_cast<std::int64_t>(*timestamp);
    }
    EditDocument edits{query.value(13)};
    edits.temperature = query.value(14);
    edits.tint = query.value(15);
    edits.customWhiteBalance = query.value(16) != 0;
    edits.contrast = query.value(17);
    edits.highlights = query.value(18);
    edits.shadows = query.value(19);
    edits.whites = query.value(20);
    edits.blacks = query.value(21);
    edits.vibrance = query.value(22);
    edits.saturation = query.value(23);
    edits.enabledCorrections = static_cast<std::uint32_t>(query.value(24));
    if (query.value(25) != EditDocument::schemaVersion)
        throw std::runtime_error("Unsupported edit document version.");
    edits.curveShadows = query.value(26);
    edits.curveDarks = query.value(27);
    edits.curveLights = query.value(28);
    edits.curveHighlights = query.value(29);
    edits.sharpenAmount = query.value(31);
    edits.sharpenRadius = query.value(32);
    edits.sharpenDetail = query.value(33);
    edits.sharpenMasking = query.value(34);
    edits.noiseLuminance = query.value(35);
    edits.noiseLuminanceDetail = query.value(36);
    edits.noiseLuminanceContrast = query.value(37);
    edits.noiseColor = query.value(38);
    edits.noiseColorDetail = query.value(39);
    edits.noiseColorSmoothness = query.value(40);
    edits.crop.setParameters({{"crop.x", query.value(41)},
                              {"crop.y", query.value(42)},
                              {"crop.width", query.value(43)},
                              {"crop.height", query.value(44)},
                              {"crop.angle", query.value(45)},
                              {"crop.aspect", query.value(46)},
                              {"crop.quarterTurns", query.value(47)},
                              {"crop.flipHorizontal", query.value(48)},
                              {"crop.flipVertical", query.value(49)}});
    edits.lens.profile = query.string(50);
    edits.lens.setParameter("lens.distortion", query.value(51));
    edits.lens.setParameter("lens.vignetting", query.value(52));
    edits.lens.setParameter("lens.chromaticAberration", query.value(53));
    edits.transform.setParameters({{"transform.rotate", query.value(54)},
                                   {"transform.horizontal", query.value(55)},
                                   {"transform.vertical", query.value(56)},
                                   {"transform.aspect", query.value(57)},
                                   {"transform.scale", query.value(58)},
                                   {"transform.xOffset", query.value(59)},
                                   {"transform.yOffset", query.value(60)},
                                   {"transform.constrainCrop", query.value(61)},
                                   {"transform.guided", query.value(62)},
                                   {"transform.m11", query.value(63)},
                                   {"transform.m12", query.value(64)},
                                   {"transform.m13", query.value(65)},
                                   {"transform.m21", query.value(66)},
                                   {"transform.m22", query.value(67)},
                                   {"transform.m23", query.value(68)},
                                   {"transform.m31", query.value(69)},
                                   {"transform.m32", query.value(70)}});
    const bool stored = query.value(30) != 0;
    if (stored)
        for (auto &curve : edits.curves)
            curve.points.clear();
    Statement points(db, "SELECT channel,position,x,y FROM edit_curve_points WHERE asset_id=?1 "
                         "ORDER BY channel,position");
    points.text(1, query.string(0));
    while (points.row()) {
        const auto channel = points.integer(0), position = points.integer(1);
        if (!stored || channel < 0 || channel >= 4 || position < 0 || position >= 32 ||
            static_cast<std::size_t>(position) !=
                edits.curves[static_cast<std::size_t>(channel)].points.size())
            throw std::runtime_error("Incomplete or invalid stored curve points.");
        edits.curves[static_cast<std::size_t>(channel)].points.push_back(
            {points.value(2), points.value(3)});
    }
    Statement bands(
        db,
        "SELECT band,hue,saturation,luminance FROM edit_hsl_bands WHERE asset_id=?1 ORDER BY band");
    bands.text(1, query.string(0));
    std::size_t count = 0;
    while (bands.row()) {
        if (!stored || count >= edits.hsl.size() ||
            bands.integer(0) != static_cast<std::int64_t>(count))
            throw std::runtime_error("Incomplete or invalid stored HSL bands.");
        edits.hsl[count++] = {bands.value(1), bands.value(2), bands.value(3)};
    }
    if (stored && count != edits.hsl.size())
        throw std::runtime_error("Missing stored HSL bands.");
    edits.localMasks = detail::readLocalMasks(db, query.string(0));
    edits.retouch = detail::readRetouch(db, query.string(0));
    edits.creative = detail::readCreative(db, query.string(0));
    edits.validate();
    if (edits.enabled(Correction::transform) && !edits.transform.neutral())
        (void)TransformMapping(metadata.width, metadata.height, metadata.orientation,
                               edits.transform, true);
    return {query.string(0), query.string(1), query.string(2),
            metadata,        edits,           detail::readMetadataOverlay(db, query.string(0))};
}
constexpr auto selectFields =
    "SELECT a.id,a.original_path,a.checksum,a.camera,a.lens,a.iso,"
    "a.shutter_seconds,a.aperture,a.focal_length,a.width,a.height,a."
    "orientation,a.capture_date,e.exposure,e.temperature,e.tint,e.custom_wb,e.contrast,"
    "e.highlights,e.shadows,e.whites,e.blacks,e.vibrance,e.saturation,e.enabled,e.schema_version,e."
    "curve_shadows,e.curve_darks,e.curve_lights,e.curve_highlights,e.advanced_saved,"
    "e.sharpen_amount,e.sharpen_radius,e.sharpen_detail,e.sharpen_masking,"
    "e.noise_luminance,e.noise_luminance_detail,e.noise_luminance_contrast,e.noise_color,e.noise_"
    "color_detail,e.noise_color_smoothness,"
    "e.crop_x,e.crop_y,e.crop_width,e.crop_height,e.crop_angle,e.crop_aspect,e.crop_quarter_turns,"
    "e.crop_flip_h,e.crop_flip_v,e.lens_profile,e.lens_distortion,e.lens_vignetting,e.lens_ca,"
    "e.transform_rotate,e.transform_horizontal,e.transform_vertical,e.transform_aspect,e.transform_"
    "scale,e.transform_x_offset,e.transform_y_offset,e.transform_constrain_crop,e.transform_guided,"
    "e.transform_m11,e.transform_m12,e.transform_m13,e.transform_m21,e.transform_m22,e.transform_"
    "m23,e.transform_m31,e.transform_m32 "
    "FROM assets a JOIN edit_documents e ON e.asset_id=a.id ";
} // namespace

Catalog::Catalog(const std::filesystem::path &path, CatalogOpenMode mode) {
    if (mode == CatalogOpenMode::createIfMissing && !path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    if (sqlite3_open_v2(path.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX |
                            (mode == CatalogOpenMode::createIfMissing ? SQLITE_OPEN_CREATE : 0),
                        nullptr) != SQLITE_OK) {
        const std::string error =
            db_ ? sqlite3_errmsg(db_) : "Unable to allocate SQLite connection";
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        throw std::runtime_error(error);
    }
    try {
        sqlite3_busy_timeout(db_, 3000);
        execute("PRAGMA foreign_keys=ON; PRAGMA synchronous=FULL;");
        // SQLite can return BUSY immediately for concurrent first-time WAL setup,
        // without invoking the configured busy handler. Retry only that operation.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (true) {
            try {
                Statement wal(db_, "PRAGMA journal_mode=WAL");
                if (!wal.row() || wal.string(0) != "wal")
                    throw std::runtime_error("Catalog requires WAL journaling.");
                break;
            } catch (const std::exception &) {
                const int code = sqlite3_errcode(db_);
                if ((code != SQLITE_BUSY && code != SQLITE_LOCKED) ||
                    std::chrono::steady_clock::now() >= deadline)
                    throw;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        const auto schemaVersion = [this] {
            Statement version(db_, "PRAGMA user_version");
            if (!version.row()) {
                throw std::runtime_error("Cannot read catalog schema version.");
            }
            return static_cast<int>(version.value(0));
        };
        int current = schemaVersion();
        // Current-schema readers must remain usable during WAL writes/imports.
        if (current == 21)
            return;
        if (current > 21)
            throw std::runtime_error("Catalog was created by a newer version of Luma.");
        if (current > 0) {
            // Preserve a restorable pre-migration SQLite snapshot, including WAL.
            // Failure leaves the old schema intact; never migrate without a backup.
            const auto folder = path.parent_path() / (path.filename().string() + ".luma-backups");
            backupTo(folder / ("schema-" + std::to_string(current) + "-" + newID() + ".sqlite3"));
        }
        execute("BEGIN IMMEDIATE;");
        // Another connection may have migrated while this one awaited the lock.
        current = schemaVersion();
        if (current > 21) {
            throw std::runtime_error("Catalog was created by a newer version of Luma.");
        }
        if (current == 0) {
            execute(R"sql(
                CREATE TABLE schema_migrations(version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
                CREATE TABLE assets(
                    id TEXT PRIMARY KEY, original_path TEXT NOT NULL UNIQUE, filename TEXT NOT NULL,
                    extension TEXT NOT NULL, filesize INTEGER NOT NULL CHECK(filesize>=0), checksum TEXT NOT NULL,
                    capture_date INTEGER, import_date TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                    width INTEGER NOT NULL CHECK(width>0), height INTEGER NOT NULL CHECK(height>0),
                    orientation INTEGER NOT NULL, camera TEXT, lens TEXT, focal_length REAL,
                    aperture REAL, shutter_seconds REAL, iso REAL, gps_latitude REAL, gps_longitude REAL,
                    rating INTEGER NOT NULL DEFAULT 0 CHECK(rating BETWEEN 0 AND 5),
                    flag INTEGER NOT NULL DEFAULT 0 CHECK(flag IN (-1,0,1)), color_label TEXT);
                CREATE INDEX assets_checksum ON assets(checksum,filesize);
                CREATE INDEX assets_capture_date ON assets(capture_date);
                CREATE INDEX assets_rating ON assets(rating,id);
                CREATE TABLE edit_documents(
                    asset_id TEXT PRIMARY KEY REFERENCES assets(id) ON DELETE CASCADE,
                    schema_version INTEGER NOT NULL DEFAULT 1 CHECK(schema_version=1),
                    exposure REAL NOT NULL DEFAULT 0 CHECK(exposure BETWEEN -5 AND 5),
                    revision INTEGER NOT NULL DEFAULT 0,
                    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
                CREATE TABLE session(slot INTEGER PRIMARY KEY CHECK(slot=1), asset_id TEXT NOT NULL REFERENCES assets(id));
                INSERT INTO schema_migrations(version) VALUES(1);
                PRAGMA user_version=1;
            )sql");
            current = 1;
        }
        if (current == 1) {
            execute(R"sql(
                CREATE TABLE edit_documents_v2(
                    asset_id TEXT PRIMARY KEY REFERENCES assets(id) ON DELETE CASCADE,
                    schema_version INTEGER NOT NULL DEFAULT 2 CHECK(schema_version=2),
                    exposure REAL NOT NULL DEFAULT 0 CHECK(exposure BETWEEN -5 AND 5),
                    temperature REAL NOT NULL DEFAULT 6500 CHECK(temperature BETWEEN 2000 AND 25000),
                    tint REAL NOT NULL DEFAULT 0 CHECK(tint BETWEEN -150 AND 150),
                    custom_wb INTEGER NOT NULL DEFAULT 0 CHECK(custom_wb IN (0,1)),
                    contrast REAL NOT NULL DEFAULT 0 CHECK(contrast BETWEEN -100 AND 100),
                    highlights REAL NOT NULL DEFAULT 0 CHECK(highlights BETWEEN -100 AND 100),
                    shadows REAL NOT NULL DEFAULT 0 CHECK(shadows BETWEEN -100 AND 100),
                    whites REAL NOT NULL DEFAULT 0 CHECK(whites BETWEEN -100 AND 100),
                    blacks REAL NOT NULL DEFAULT 0 CHECK(blacks BETWEEN -100 AND 100),
                    vibrance REAL NOT NULL DEFAULT 0 CHECK(vibrance BETWEEN -100 AND 100),
                    saturation REAL NOT NULL DEFAULT 0 CHECK(saturation BETWEEN -100 AND 100),
                    enabled INTEGER NOT NULL DEFAULT 511 CHECK(typeof(enabled)='integer' AND enabled BETWEEN 0 AND 511),
                    revision INTEGER NOT NULL DEFAULT 0,
                    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
                INSERT INTO edit_documents_v2(asset_id,exposure,revision,updated_at)
                    SELECT asset_id,exposure,revision,updated_at FROM edit_documents;
                DROP TABLE edit_documents;
                ALTER TABLE edit_documents_v2 RENAME TO edit_documents;
                INSERT INTO schema_migrations(version) VALUES(2);
                PRAGMA user_version=2;
            )sql");
            current = 2;
        }
        if (current == 2) {
            execute(R"sql(
                CREATE TABLE collections(id TEXT PRIMARY KEY,name TEXT NOT NULL UNIQUE
                    CHECK(length(trim(name)) BETWEEN 1 AND 128),created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
                CREATE TABLE collection_assets(collection_id TEXT NOT NULL REFERENCES collections(id) ON DELETE CASCADE,
                    asset_id TEXT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,PRIMARY KEY(collection_id,asset_id));
                CREATE INDEX collection_assets_asset ON collection_assets(asset_id,collection_id);
                CREATE INDEX assets_filename ON assets(filename COLLATE NOCASE,id);
                CREATE INDEX assets_flag ON assets(flag,id);
                CREATE TABLE import_sessions(id TEXT PRIMARY KEY,mode TEXT NOT NULL CHECK(mode IN ('add','copy')),
                    destination TEXT NOT NULL,state TEXT NOT NULL CHECK(state IN ('running','completed','cancelled','interrupted')),
                    owner_pid INTEGER NOT NULL,skip_duplicates INTEGER NOT NULL DEFAULT 1 CHECK(skip_duplicates IN (0,1)),created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                    finished_at TEXT);
                CREATE TABLE import_items(session_id TEXT NOT NULL REFERENCES import_sessions(id),
                    position INTEGER NOT NULL CHECK(position>=0),source_path TEXT NOT NULL,target_path TEXT NOT NULL DEFAULT '',
                    temporary_path TEXT NOT NULL DEFAULT '',checksum TEXT NOT NULL DEFAULT '',filesize INTEGER NOT NULL DEFAULT 0,
                    state TEXT NOT NULL CHECK(state IN ('queued','staged','committed','duplicate','failed','cancelled')),
                    asset_id TEXT REFERENCES assets(id),error TEXT NOT NULL DEFAULT '',PRIMARY KEY(session_id,position));
                INSERT INTO schema_migrations(version) VALUES(3);
                PRAGMA user_version=3;
            )sql");
            current = 3;
        }
        if (current == 3) {
            execute(R"sql(
                CREATE TABLE edit_documents_v3(
                    asset_id TEXT PRIMARY KEY REFERENCES assets(id) ON DELETE CASCADE,
                    schema_version INTEGER NOT NULL DEFAULT 3 CHECK(schema_version=3),
                    exposure REAL NOT NULL DEFAULT 0 CHECK(exposure BETWEEN -5 AND 5),
                    temperature REAL NOT NULL DEFAULT 6500 CHECK(temperature BETWEEN 2000 AND 25000),
                    tint REAL NOT NULL DEFAULT 0 CHECK(tint BETWEEN -150 AND 150),
                    custom_wb INTEGER NOT NULL DEFAULT 0 CHECK(custom_wb IN (0,1)),
                    contrast REAL NOT NULL DEFAULT 0 CHECK(contrast BETWEEN -100 AND 100),
                    highlights REAL NOT NULL DEFAULT 0 CHECK(highlights BETWEEN -100 AND 100),
                    shadows REAL NOT NULL DEFAULT 0 CHECK(shadows BETWEEN -100 AND 100),
                    whites REAL NOT NULL DEFAULT 0 CHECK(whites BETWEEN -100 AND 100),
                    blacks REAL NOT NULL DEFAULT 0 CHECK(blacks BETWEEN -100 AND 100),
                    vibrance REAL NOT NULL DEFAULT 0 CHECK(vibrance BETWEEN -100 AND 100),
                    saturation REAL NOT NULL DEFAULT 0 CHECK(saturation BETWEEN -100 AND 100),
                    enabled INTEGER NOT NULL DEFAULT 2047 CHECK(typeof(enabled)='integer' AND enabled BETWEEN 0 AND 2047),
                    curve_shadows REAL NOT NULL DEFAULT 0 CHECK(curve_shadows BETWEEN -100 AND 100),
                    curve_darks REAL NOT NULL DEFAULT 0 CHECK(curve_darks BETWEEN -100 AND 100),
                    curve_lights REAL NOT NULL DEFAULT 0 CHECK(curve_lights BETWEEN -100 AND 100),
                    curve_highlights REAL NOT NULL DEFAULT 0 CHECK(curve_highlights BETWEEN -100 AND 100),
                    advanced_saved INTEGER NOT NULL DEFAULT 0 CHECK(advanced_saved IN (0,1)),
                    revision INTEGER NOT NULL DEFAULT 0,
                    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
                INSERT INTO edit_documents_v3(asset_id,exposure,temperature,tint,custom_wb,contrast,highlights,shadows,whites,blacks,vibrance,saturation,enabled,revision,updated_at)
                    SELECT asset_id,exposure,temperature,tint,custom_wb,contrast,highlights,shadows,whites,blacks,vibrance,saturation,enabled|1536,revision,updated_at FROM edit_documents;
                DROP TABLE edit_documents;
                ALTER TABLE edit_documents_v3 RENAME TO edit_documents;
                CREATE TABLE edit_curve_points(
                    asset_id TEXT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
                    channel INTEGER NOT NULL CHECK(channel BETWEEN 0 AND 3),
                    position INTEGER NOT NULL CHECK(position BETWEEN 0 AND 31),
                    x REAL NOT NULL CHECK(x BETWEEN 0 AND 1), y REAL NOT NULL CHECK(y BETWEEN 0 AND 1),
                    PRIMARY KEY(asset_id,channel,position));
                CREATE TABLE edit_hsl_bands(
                    asset_id TEXT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
                    band INTEGER NOT NULL CHECK(band BETWEEN 0 AND 7),
                    hue REAL NOT NULL CHECK(hue BETWEEN -100 AND 100),
                    saturation REAL NOT NULL CHECK(saturation BETWEEN -100 AND 100),
                    luminance REAL NOT NULL CHECK(luminance BETWEEN -100 AND 100),
                    PRIMARY KEY(asset_id,band));
                INSERT INTO schema_migrations(version) VALUES(4);
                PRAGMA user_version=4;
            )sql");
            current = 4;
        }
        if (current == 4) {
            migrateSharpening();
            current = 5;
        }
        if (current == 5) {
            migrateNoiseReduction();
            current = 6;
        }
        if (current == 6) {
            migrateCrop();
            current = 7;
        }
        if (current == 7) {
            migrateLens();
            current = 8;
        }
        if (current == 8) {
            migrateTransform();
            current = 9;
        }
        if (current == 9) {
            migrateLocalMasks();
            current = 10;
        }
        if (current == 10) {
            migrateMaskComponents();
            current = 11;
        }
        if (current == 11) {
            migrateBitmapMasks();
            current = 12;
        }
        if (current == 12) {
            migrateRetouch();
            current = 13;
        }
        if (current == 13) {
            migrateSemanticIndex();
            current = 14;
        }
        if (current == 14) {
            migrateDerivedImages();
            current = 15;
        }
        if (current == 15) {
            migrateMetadata();
            current = 16;
        }
        if (current == 16) {
            migrateMetadataInterop();
            current = 17;
        }
        if (current == 17) {
            migrateEditVersions();
            current = 18;
        }
        if (current == 18) {
            migrateCreative();
            current = 19;
        }
        if (current == 19) {
            migrateSmartCollections();
            current = 20;
        }
        if (current == 20) {
            migrateImportPlans();
        }
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}
Catalog::~Catalog() {
    if (db_) {
        sqlite3_close(db_);
    }
}
void Catalog::execute(const char *sql) const {
    if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("Catalog: ") + sqlite3_errmsg(db_));
    }
}

AssetRecord Catalog::addAndSelect(const std::filesystem::path &source, const std::string &checksum,
                                  const PhotoMetadata &metadata) {
    execute("BEGIN IMMEDIATE;");
    try {
        auto result = addAssetRecord(source, checksum, metadata, true);
        execute("COMMIT;");
        return result;
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
AssetRecord Catalog::addAssetRecord(const std::filesystem::path &source,
                                    const std::string &checksum, const PhotoMetadata &metadata,
                                    bool selectActive) {
    const auto path = std::filesystem::canonical(source);
    Statement query(db_, (std::string(selectFields) + "WHERE a.original_path=?1").c_str());
    query.text(1, path.string());
    AssetRecord result;
    if (query.row()) {
        result = readAsset(db_, query);
        if (result.checksum != checksum) {
            throw std::runtime_error("Original file changed. Existing edits were preserved; "
                                     "automatic replacement is disabled.");
        }
    } else {
        result = {newID(), path, checksum, metadata, {}};
        Statement insert(
            db_, "INSERT INTO "
                 "assets(id,original_path,filename,extension,filesize,checksum,capture_date,"
                 "width,height,orientation,camera,lens,focal_length,aperture,shutter_seconds,"
                 "iso) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16)");
        insert.text(1, result.id);
        insert.text(2, path.string());
        insert.text(3, path.filename().string());
        insert.text(4, path.extension().string());
        insert.number(5, static_cast<double>(std::filesystem::file_size(path)));
        insert.text(6, checksum);
        insert.number(7, metadata.captureTimestamp
                             ? std::optional<double>(*metadata.captureTimestamp)
                             : std::nullopt);
        insert.number(8, metadata.width);
        insert.number(9, metadata.height);
        insert.number(10, metadata.orientation);
        insert.text(11, metadata.camera);
        insert.text(12, metadata.lens);
        insert.number(13, metadata.focalLength);
        insert.number(14, metadata.aperture);
        insert.number(15, metadata.shutterSeconds);
        insert.number(16, metadata.iso);
        insert.row();
        Statement edit(db_, "INSERT INTO edit_documents(asset_id) VALUES(?1)");
        edit.text(1, result.id);
        edit.row();
        detail::initializeMetadata(db_, result.id, path, metadata);
    }
    if (selectActive) {
        Statement select(db_, "INSERT INTO session(slot,asset_id) VALUES(1,?1) ON CONFLICT(slot) "
                              "DO UPDATE SET asset_id=excluded.asset_id");
        select.text(1, result.id);
        select.row();
    }
    result.descriptive = detail::readMetadataOverlay(db_, result.id);
    return result;
}

std::optional<AssetRecord> Catalog::activeAsset() const {
    Statement query(
        db_,
        (std::string(selectFields) + "JOIN session s ON s.asset_id=a.id WHERE s.slot=1").c_str());
    return query.row() ? std::optional<AssetRecord>(readAsset(db_, query)) : std::nullopt;
}
std::optional<AssetRecord> Catalog::assetByID(const std::string &id) const {
    Statement query(db_, (std::string(selectFields) + "WHERE a.id=?1").c_str());
    query.text(1, id);
    return query.row() ? std::optional<AssetRecord>(readAsset(db_, query)) : std::nullopt;
}
bool Catalog::originalAtPath(const std::filesystem::path &canonicalPath,
                             CancellationToken stop) const {
    Statement query(db_, "SELECT 1 FROM assets WHERE original_path=?1 LIMIT 1");
    query.text(1, canonicalPath.string());
    if (query.row())
        return true;
    if (!std::filesystem::exists(canonicalPath))
        return false;
    Statement paths(db_, "SELECT original_path FROM assets");
    while (paths.row()) {
        checkCancellation(stop);
        std::error_code error;
        if (std::filesystem::equivalent(canonicalPath, paths.string(0), error) && !error)
            return true;
    }
    return false;
}
void Catalog::installExport(const std::filesystem::path &target, CancellationToken stop,
                            const std::function<void()> &install) {
    execute("BEGIN IMMEDIATE;");
    try {
        checkCancellation(stop);
        if (originalAtPath(target, stop))
            throw std::runtime_error("Export cannot replace a catalog original.");
        install();
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
std::optional<AssetSnapshot> Catalog::assetSnapshot(const std::string &id) {
    execute("BEGIN;");
    try {
        std::optional<AssetSnapshot> result;
        if (auto asset = assetByID(id)) {
            Statement revision(db_, "SELECT revision FROM edit_documents WHERE asset_id=?1");
            revision.text(1, id);
            if (!revision.row() || revision.integer(0) < 0)
                throw std::runtime_error("Missing or invalid saved edit revision.");
            result = AssetSnapshot{std::move(*asset), revision.integer(0)};
        }
        execute("COMMIT;");
        return result;
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
std::optional<AssetRecord> Catalog::duplicate(const std::string &checksum,
                                              std::int64_t size) const {
    Statement query(db_, (std::string(selectFields) +
                          "WHERE a.checksum=?1 AND a.filesize=?2 ORDER BY a.id LIMIT 1")
                             .c_str());
    query.text(1, checksum);
    query.integer(2, size);
    return query.row() ? std::optional<AssetRecord>(readAsset(db_, query)) : std::nullopt;
}
void Catalog::saveEdits(const std::string &assetID, EditDocument edits) {
    edits.validate();
    execute("BEGIN IMMEDIATE;");
    try {
        writeEdits(assetID, edits);
#ifdef LUMA_UX_PERSISTENCE_PROBE
        ux::observePersistenceCheckpoint(sqlite3_db_filename(db_, "main"), "catalog.before-commit",
                                         {}, {});
#endif
        execute("COMMIT;");
#ifdef LUMA_UX_PERSISTENCE_PROBE
        ux::observePersistenceCheckpoint(sqlite3_db_filename(db_, "main"), "catalog.after-commit",
                                         {}, {});
#endif
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void Catalog::writeEdits(const std::string &assetID, const EditDocument &edits) {
    if (edits.enabled(Correction::transform) && !edits.transform.neutral()) {
        Statement dimensions(db_, "SELECT width,height,orientation FROM assets WHERE id=?1");
        dimensions.text(1, assetID);
        if (!dimensions.row())
            throw std::runtime_error("Cannot save geometry: photo is missing.");
        (void)TransformMapping(static_cast<int>(dimensions.integer(0)),
                               static_cast<int>(dimensions.integer(1)),
                               static_cast<int>(dimensions.integer(2)), edits.transform, true);
    }

    Statement query(
        db_,
        "UPDATE edit_documents SET exposure=?1,temperature=?2,tint=?3,custom_wb=?4,"
        "contrast=?5,highlights=?6,shadows=?7,whites=?8,blacks=?9,vibrance=?10,saturation=?11,"
        "enabled=?12,curve_shadows=?14,curve_darks=?15,curve_lights=?16,curve_highlights=?17,"
        "sharpen_amount=?18,sharpen_radius=?19,sharpen_detail=?20,sharpen_masking=?21,"
        "noise_luminance=?22,noise_luminance_detail=?23,noise_luminance_contrast=?24,noise_color=?"
        "25,noise_color_detail=?26,noise_color_smoothness=?27,"
        "crop_x=?28,crop_y=?29,crop_width=?30,crop_height=?31,crop_angle=?32,crop_aspect=?33,crop_"
        "quarter_turns=?34,crop_flip_h=?35,crop_flip_v=?36,lens_profile=?37,lens_distortion=?38,"
        "lens_vignetting=?39,lens_ca=?40,transform_rotate=?41,transform_horizontal=?42,transform_"
        "vertical=?43,transform_aspect=?44,transform_scale=?45,transform_x_offset=?46,transform_y_"
        "offset=?47,transform_constrain_crop=?48,transform_guided=?49,transform_m11=?50,transform_"
        "m12=?51,transform_m13=?52,transform_m21=?53,transform_m22=?54,transform_m23=?55,transform_"
        "m31=?56,transform_m32=?57,"
        "advanced_saved=1,revision=revision+1,updated_at=CURRENT_TIMESTAMP WHERE asset_id=?13");
    query.number(1, edits.exposure);
    query.number(2, edits.temperature);
    query.number(3, edits.tint);
    query.number(4, edits.customWhiteBalance ? 1 : 0);
    query.number(5, edits.contrast);
    query.number(6, edits.highlights);
    query.number(7, edits.shadows);
    query.number(8, edits.whites);
    query.number(9, edits.blacks);
    query.number(10, edits.vibrance);
    query.number(11, edits.saturation);
    query.number(12, edits.enabledCorrections);
    query.text(13, assetID);
    query.number(14, edits.curveShadows);
    query.number(15, edits.curveDarks);
    query.number(16, edits.curveLights);
    query.number(17, edits.curveHighlights);
    query.number(18, edits.sharpenAmount);
    query.number(19, edits.sharpenRadius);
    query.number(20, edits.sharpenDetail);
    query.number(21, edits.sharpenMasking);
    query.number(22, edits.noiseLuminance);
    query.number(23, edits.noiseLuminanceDetail);
    query.number(24, edits.noiseLuminanceContrast);
    query.number(25, edits.noiseColor);
    query.number(26, edits.noiseColorDetail);
    query.number(27, edits.noiseColorSmoothness);
    query.number(28, edits.crop.x);
    query.number(29, edits.crop.y);
    query.number(30, edits.crop.width);
    query.number(31, edits.crop.height);
    query.number(32, edits.crop.angle);
    query.number(33, edits.crop.aspect);
    query.number(34, edits.crop.quarterTurns);
    query.number(35, edits.crop.flipHorizontal);
    query.number(36, edits.crop.flipVertical);
    query.text(37, edits.lens.profile);
    query.number(38, edits.lens.distortion);
    query.number(39, edits.lens.vignetting);
    query.number(40, edits.lens.chromaticAberration);
    for (std::size_t i = 0; i < transformParameters.size(); ++i)
        query.number(static_cast<int>(41 + i), edits.transform.parameter(transformParameters[i]));
    query.row();
    if (sqlite3_changes(db_) != 1)
        throw std::runtime_error("Cannot save edits: asset is missing from catalog.");
    writeLocalMasks(assetID, edits.localMasks);
    writeRetouch(assetID, edits.retouch);
    writeCreative(assetID, edits.creative);
    for (const auto *sql : {"DELETE FROM edit_curve_points WHERE asset_id=?1",
                            "DELETE FROM edit_hsl_bands WHERE asset_id=?1"}) {
        Statement clear(db_, sql);
        clear.text(1, assetID);
        clear.row();
    }
    for (std::size_t channel = 0; channel < edits.curves.size(); ++channel) {
        const auto &points = edits.curves[channel].points;
        for (std::size_t position = 0; position < points.size(); ++position) {
            Statement insert(db_, "INSERT INTO edit_curve_points(asset_id,channel,position,x,y) "
                                  "VALUES(?1,?2,?3,?4,?5)");
            insert.text(1, assetID);
            insert.integer(2, static_cast<std::int64_t>(channel));
            insert.integer(3, static_cast<std::int64_t>(position));
            insert.number(4, points[position].x);
            insert.number(5, points[position].y);
            insert.row();
        }
    }
    for (std::size_t band = 0; band < edits.hsl.size(); ++band) {
        Statement insert(db_, "INSERT INTO edit_hsl_bands(asset_id,band,hue,saturation,luminance) "
                              "VALUES(?1,?2,?3,?4,?5)");
        insert.text(1, assetID);
        insert.integer(2, static_cast<std::int64_t>(band));
        insert.number(3, edits.hsl[band].hue);
        insert.number(4, edits.hsl[band].saturation);
        insert.number(5, edits.hsl[band].luminance);
        insert.row();
    }
}
void Catalog::saveExposure(const std::string &assetID, double ev) {
    EditDocument{ev}.validate();
    execute("BEGIN IMMEDIATE;");
    try {
        Statement query(
            db_, "UPDATE edit_documents SET "
                 "exposure=?1,revision=revision+1,updated_at=CURRENT_TIMESTAMP WHERE asset_id=?2");
        query.number(1, ev);
        query.text(2, assetID);
        query.row();
        if (sqlite3_changes(db_) != 1) {
            throw std::runtime_error("Cannot save edits: asset is missing from catalog.");
        }
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void Catalog::verify() const {
    Statement integrity(db_, "PRAGMA integrity_check");
    if (!integrity.row() || integrity.string(0) != "ok") {
        throw std::runtime_error("Catalog integrity check failed.");
    }
    Statement foreignKeys(db_, "PRAGMA foreign_key_check");
    if (foreignKeys.row()) {
        throw std::runtime_error("Catalog foreign key check failed.");
    }
}
} // namespace luma
