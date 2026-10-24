#include "apps/shared/SessionService.h"
#include "core/edits/EditHistory.h"
#include "core/export/JpegICC.h"
#include "core/image/Bayer.h"
#include "core/jobs/JobManager.h"
#include "core/render/CpuRenderer.h"
#include "core/render/DevelopPipeline.h"
#include "core/render/RetouchPass.h"
#include "tests/unit/LegacyCatalogFixture.h"
#include <array>
#include <cmath>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <sqlite3.h>

namespace {
void require(bool result, const char *message) {
    if (!result) {
        throw std::runtime_error(message);
    }
}
template <typename F> void throws(F function) {
    bool caught = false;
    try {
        function();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected an exception.");
}
void imageTests() {
    require(std::abs(luma::applyExposure(0.25F, 1) - 0.5F) < 1e-7,
            "Exposure must double linear light.");
    require(luma::applyExposure(2.0F, 2) == 8.0F, "HDR values must not clip before display.");
    require(luma::applyExposure(-0.1F, 1) < 0, "Negative color-transform values must survive.");
    throws([] { luma::applyExposure(1, std::numeric_limits<double>::quiet_NaN()); });
    throws([] { luma::applyExposure(1, 5.01); });
    for (const auto pattern : {std::array{0, 1, 1, 2}, std::array{2, 1, 1, 0},
                               std::array{1, 0, 2, 1}, std::array{1, 2, 0, 1}}) {
        luma::BayerFrame frame(6, 6, pattern);
        constexpr std::array<float, 3> expected{0.8F, 0.4F, 0.2F};
        for (int y = 0; y < 6; ++y) {
            for (int x = 0; x < 6; ++x) {
                frame.samples[y * 6 + x] = expected[frame.colorAt(x, y)];
            }
        }
        auto image = luma::demosaicBilinear(frame);
        for (std::size_t i = 0; i < image.rgb.size(); ++i) {
            require(std::abs(image.rgb[i] - expected[i % 3]) < 1e-6,
                    "CFA reconstruction failed, including edges.");
        }
        luma::cameraToLinearSRGB(image, {1, 0, 0, 0, 1, 0, 0, 0, 1});
        require(std::abs(image.rgb[0] - 0.8F) < 1e-6, "Identity camera matrix changed pixels.");
    }
    luma::FloatImage gray(2, 3);
    std::fill(gray.rgb.begin(), gray.rgb.end(), 0.18F);
    const auto raster = luma::renderCPU(gray, {});
    require(std::abs(static_cast<int>(raster.rgba[0]) - 118) <= 2,
            "Linear gray must use the sRGB transfer curve.");
    require(raster.rgba[3] == 255, "Display output must be opaque.");
    require(luma::renderCPU(gray, {1}).rgba[0] > raster.rgba[0],
            "Exposure did not change display pixels.");
    for (int orientation = 0; orientation < 8; ++orientation) {
        const auto image = luma::renderCPU(gray, {}, orientation);
        require(image.width == (orientation & 4 ? 3 : 2), "Rotated width is wrong.");
        require(image.height == (orientation & 4 ? 2 : 3), "Rotated height is wrong.");
    }
    gray.rgb[0] = 0.5F;
    const auto normal = luma::renderCPU(gray, {});
    const auto rotated = luma::renderCPU(gray, {}, 6);
    require(rotated.rgba[8] == normal.rgba[0], "90-degree orientation mapping is wrong.");
    const auto preview = luma::standardPreview(gray, 2);
    require(preview.width == 1 && preview.height == 2, "Preview bound is wrong.");
    luma::CancellationSource stop;
    stop.cancel();
    throws([&] { luma::renderCPU(gray, {}, 0, {}, stop.token()); });
    throws([] { luma::FloatImage invalid(0, 10); });
}
void retouchSourceTests() {
    for (const auto size : {std::array{64, 64}, std::array{97, 83}}) {
        luma::FloatImage image(size[0], size[1]);
        std::fill(image.rgb.begin(), image.rgb.end(), .25F);
        const auto center = (static_cast<std::size_t>(size[1] / 2) * size[0] + size[0] / 2) * 3;
        image.rgb[center] = 1;
        const auto original = image.rgb;
        luma::RetouchStroke stroke;
        stroke.id = "1234-abcd";
        stroke.mode = luma::RetouchMode::clone;
        stroke.radius = .06;
        stroke.points = {{.5, .5}};
        const auto offset = luma::findRetouchSource(image, stroke);
        require(offset == luma::findRetouchSource(image, stroke),
                "Retouch source search was not deterministic.");
        require(std::isfinite(offset[0]) && std::isfinite(offset[1]) &&
                    std::hypot(offset[0] * size[0], offset[1] * size[1]) >
                        2 * stroke.radius * std::min(size[0], size[1]),
                "Retouch source overlaps the target.");
        stroke.sourceOffsetX = offset[0];
        stroke.sourceOffsetY = offset[1];
        luma::RetouchPass repaired(image, {stroke});
        require(std::abs(repaired.image().rgb[center] - .25F) < 1e-6 && image.rgb == original,
                "Retouch did not repair the isolated defect or mutated its source.");
        require(luma::decodeRetouch(luma::encodeRetouch({stroke})) == std::vector{stroke},
                "Retouch persistence lost the resolved donor.");
        luma::CancellationSource cancelled;
        cancelled.cancel();
        throws([&] { luma::findRetouchSource(image, stroke, cancelled.token()); });
        image.alpha.assign(static_cast<std::size_t>(size[0]) * size[1], 0);
        throws([&] { luma::findRetouchSource(image, stroke); });
    }
}
void jpegProfileTests() {
    const std::vector<std::uint8_t> jpeg{255, 216, 255, 224, 0, 4, 1,   2,
                                         255, 218, 0,   2,   7, 8, 255, 217};
    const std::vector<std::uint8_t> profile(70000, 42);
    const auto tagged = luma::embedJpegICC(jpeg, profile);
    require(tagged[8] == 255 && tagged[9] == 226, "JPEG ICC marker missing.");
    require(tagged[24] == 1 && tagged[25] == 2, "Multipart JPEG ICC numbering is wrong.");
    require(std::equal(jpeg.begin(), jpeg.begin() + 8, tagged.begin()),
            "ICC embedding moved the JFIF header.");
    require(std::equal(jpeg.end() - 8, jpeg.end(), tagged.end() - 8),
            "ICC embedding changed compressed pixels.");
    require(luma::embedJpegICC(tagged, profile) == tagged,
            "ICC replacement accumulated duplicate segments.");
    throws([&] { luma::embedJpegICC({}, profile); });
    throws([&] { luma::embedJpegICC(jpeg, {}); });
    throws([&] {
        luma::embedJpegICC(std::vector<std::uint8_t>{255, 216, 255, 224, 255, 255}, profile);
    });
}
void developTests() {
    luma::EditDocument edits;
    luma::DevelopPipeline baseline(edits);
    const std::array<float, 3> pixel{0.5F, 0.25F, 0.125F};
    const auto neutral = baseline.pixel(pixel);
    for (std::size_t c = 0; c < 3; ++c)
        require(std::abs(neutral[c] - pixel[c]) < 1e-6, "Neutral Develop changed RGB.");
    edits.exposure = 1;
    const auto exposed = luma::DevelopPipeline(edits).pixel(pixel);
    require(std::abs(exposed[0] - 1) < 1e-6, "Exposure is not linear.");
    edits.setEnabled(luma::Correction::exposure, false);
    require(luma::DevelopPipeline(edits).pixel(pixel) == neutral,
            "Disabled Exposure changed output.");
    require(edits.exposure == 1, "Disabling erased a parameter.");
    edits = {};
    edits.saturation = -100;
    const auto gray = luma::DevelopPipeline(edits).pixel(pixel);
    require(gray[0] == gray[1] && gray[1] == gray[2], "Saturation -100 is not monochrome.");
    for (double temperature : {2000.0, 2850.0, 6500.0, 25000.0}) {
        edits = {};
        edits.customWhiteBalance = true;
        edits.temperature = temperature;
        edits.tint = 150;
        const auto result = luma::DevelopPipeline(edits).pixel(pixel);
        for (float channel : result)
            require(std::isfinite(channel), "WB boundary produced non-finite pixels.");
        edits.setEnabled(luma::Correction::whiteBalance, false);
        require(luma::DevelopPipeline(edits).pixel(pixel) == neutral,
                "Disabled manual WB must restore As Shot.");
    }
    for (double sign : {-100.0, 100.0}) {
        edits = {};
        edits.contrast = sign;
        edits.highlights = -sign;
        edits.shadows = sign;
        edits.whites = -sign;
        edits.blacks = sign;
        const luma::DevelopPipeline pipeline(edits);
        float previous = 0;
        for (int i = 0; i <= 1000; ++i) {
            const float value = static_cast<float>(std::exp2(-12 + i * 0.016));
            const auto result = pipeline.pixel({value, value, value});
            require(result[0] >= previous, "Tone boundary inverted luminance ordering.");
            previous = result[0];
        }
    }
    edits = {};
    edits.highlights = -100;
    require(luma::DevelopPipeline(edits).pixel({2, 2, 2})[0] < 2,
            "Highlights did not recover float headroom.");
    edits = {};
    edits.shadows = 100;
    require(luma::DevelopPipeline(edits).pixel({0.02F, 0.02F, 0.02F})[0] > 0.02F,
            "Shadows did not lift low light.");
    edits = {};
    edits.vibrance = 100;
    require(luma::DevelopPipeline(edits).pixel({0.2F, 0.2F, 0.2F})[0] == 0.2F,
            "Vibrance altered neutral gray.");
    throws([&] { edits.setParameter("tint", 151); });
    throws([&] { edits.setParameter("exposure", std::numeric_limits<double>::quiet_NaN()); });
    throws([&] { edits.setParameter("unknown", 1); });
    require(edits.tint == 0, "Invalid edit partially mutated document.");
    luma::EditHistory history;
    history.begin();
    for (int i = 1; i <= 20; ++i) {
        auto next = history.current();
        next.exposure = i / 10.0;
        history.apply(next);
    }
    history.end();
    require(history.canUndo(), "Gesture has no undo.");
    history.undo();
    require(history.current() == luma::EditDocument{} && !history.canUndo(),
            "One slider gesture must be one Undo.");
    history.redo();
    require(history.current().exposure == 2, "Redo failed.");
    history.undo();
    edits = history.current();
    edits.shadows = 30;
    history.apply(edits);
    require(!history.canRedo(), "New edit did not invalidate redo.");
    history.apply({});
    history.undo();
    require(history.current().shadows == 30, "Reset must be one reversible operation.");
}
void catalogTests() {
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("luma-unit-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    try {
        const auto source = directory / "Кадр с пробелами.NEF";
        std::ofstream(source) << "synthetic test bytes";
        const auto path = directory / "catalog.sqlite";
        std::string id;
        {
            luma::Catalog catalog(path);
            require(!catalog.activeAsset(), "New catalog should be empty.");
            luma::PhotoMetadata metadata;
            metadata.width = 6;
            metadata.height = 4;
            metadata.camera = "Synthetic";
            const auto asset = catalog.addAndSelect(source, "test-checksum", metadata);
            id = asset.id;
            require(id.size() == 36, "Asset ID must be a UUID.");
            catalog.saveExposure(id, 1.25);
            require(catalog.addAndSelect(source, "test-checksum", metadata).edits.exposure == 1.25,
                    "Reimport lost edits.");
            throws([&] { catalog.addAndSelect(source, "changed-checksum", metadata); });
            throws([&] { catalog.saveExposure(id, std::numeric_limits<double>::infinity()); });
            throws([&] { catalog.saveExposure("missing-id", 0); });
            catalog.verify();
        }
        {
            luma::Catalog reopened(path);
            const auto asset = reopened.activeAsset();
            require(asset && asset->id == id && asset->edits.exposure == 1.25,
                    "Durable edit or selected photo was lost after restart.");
            require(asset->path.filename() == source.filename(), "Unicode path was corrupted.");
            reopened.verify();
        }
        {
            luma::Catalog catalog(path);
            auto edits = catalog.activeAsset()->edits;
            edits.temperature = 5350;
            edits.tint = 12;
            edits.customWhiteBalance = true;
            edits.highlights = -45;
            edits.shadows = 23;
            edits.vibrance = 35;
            edits.setEnabled(luma::Correction::shadows, false);
            catalog.saveEdits(id, edits);
            require(catalog.activeAsset()->edits == edits, "Develop document round trip failed.");
            catalog.saveExposure(id, 0.75);
            require(catalog.activeAsset()->edits.temperature == 5350,
                    "Legacy Exposure update erased Develop.");
        }
        sqlite3 *db = nullptr;
        require(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "Cannot open schema test database.");
        require(sqlite3_exec(db, fixture::removePostV11Tables, nullptr, nullptr, nullptr) ==
                    SQLITE_OK,
                "Cannot remove newer features from the historical M0 fixture.");
        const char *legacy = R"sql(
            BEGIN;
            CREATE TABLE legacy_edits(asset_id TEXT PRIMARY KEY REFERENCES assets(id) ON DELETE CASCADE,
                schema_version INTEGER NOT NULL DEFAULT 1 CHECK(schema_version=1),
                exposure REAL NOT NULL DEFAULT 0 CHECK(exposure BETWEEN -5 AND 5),
                revision INTEGER NOT NULL DEFAULT 0,updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
            INSERT INTO legacy_edits(asset_id,exposure,revision,updated_at)
                SELECT asset_id,exposure,revision,updated_at FROM edit_documents;
            DROP TRIGGER initialize_local_masks; DROP TABLE edit_local_masks;
            DROP TABLE edit_documents;
            ALTER TABLE legacy_edits RENAME TO edit_documents;
            DROP TABLE edit_curve_points;
            DROP TABLE edit_hsl_bands;
            DROP TABLE import_items;
            DROP TABLE import_sessions;
            DROP TABLE collection_assets;
            DROP TABLE collections;
            DROP INDEX assets_filename;
            DROP INDEX assets_flag;
            DELETE FROM schema_migrations WHERE version>=2;
            PRAGMA user_version=1;
            COMMIT;
        )sql";
        require(sqlite3_exec(db, legacy, nullptr, nullptr, nullptr) == SQLITE_OK,
                "Cannot establish historical M0 fixture.");
        sqlite3_close(db);
        {
            luma::Catalog migrated(path);
            const auto asset = migrated.activeAsset();
            require(asset && asset->id == id && asset->edits.exposure == 0.75,
                    "M0 migration lost exposure or selection.");
            require(!asset->edits.customWhiteBalance && asset->edits.temperature == 6500,
                    "Migration defaults changed As Shot.");
            migrated.verify();
        }
        require(sqlite3_open(path.c_str(), &db) == SQLITE_OK,
                "Cannot reopen future schema fixture.");
        require(sqlite3_exec(db, "PRAGMA user_version=22", nullptr, nullptr, nullptr) == SQLITE_OK,
                "Cannot create future catalog.");
        sqlite3_close(db);
        throws([&] { luma::Catalog future(path); });
        std::filesystem::remove_all(directory);
    } catch (...) {
        std::filesystem::remove_all(directory);
        throw;
    }
}
void jobTests() {
    luma::JobManager jobs;
    require(jobs.idle(), "An empty job pool is not idle.");
    std::promise<void> firstStarted;
    std::promise<void> release;
    auto waiting = release.get_future().share();
    auto first = jobs.submit(luma::JobPriority::background, [&](const luma::JobContext &) {
        firstStarted.set_value();
        waiting.wait();
    });
    firstStarted.get_future().wait();
    require(!jobs.idle(), "Running worker was reported idle.");
    std::atomic<bool> executed = false;
    auto cancelled = jobs.submit(luma::JobPriority::background,
                                 [&](const luma::JobContext &) { executed = true; });
    cancelled.cancel();
    std::promise<void> finished;
    auto interactive =
        jobs.submit(luma::JobPriority::interactive, [&](const luma::JobContext &context) {
            context.setProgress(0.5);
            finished.set_value();
        });
    release.set_value();
    require(finished.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready,
            "Interactive job stalled.");
    jobs.shutdown();
    require(!executed, "Cancelled job ran.");
    require(cancelled.state() == luma::JobState::cancelled,
            "Cancelled job state was not retained.");
    require(first.state() == luma::JobState::completed, "Completed job state is wrong.");
    require(interactive.progress() == 1, "Job progress did not complete.");
    require(jobs.idle(), "A stopped, drained pool is not idle.");
}
void cancelledWorkerIdleTests() {
    luma::JobManager jobs;
    std::promise<void> started, release;
    auto waiting = release.get_future().share();
    auto previous = jobs.submit(luma::JobPriority::background, [&](const luma::JobContext &) {
        started.set_value();
        waiting.wait();
    });
    started.get_future().wait();
    previous.cancel();
    auto newest = jobs.submit(luma::JobPriority::background, [](const luma::JobContext &) {});
    newest.cancel();
    jobs.submit(luma::JobPriority::interactive, [](const luma::JobContext &) {});
    const bool newestTerminal = newest.state() == luma::JobState::cancelled;
    const bool oldWorkerRetained = previous.state() == luma::JobState::running && !jobs.idle();
    release.set_value();
    jobs.shutdown();
    require(newestTerminal, "Cancelled queued handle did not reach its terminal state.");
    require(oldWorkerRetained, "Terminal replacement handle hid an older cancelled worker.");
    require(jobs.idle() && previous.state() == luma::JobState::cancelled,
            "Cancelled worker was not drained before idle.");
}
} // namespace
int main() {
    try {
        imageTests();
        retouchSourceTests();
        developTests();
        jpegProfileTests();
        catalogTests();
        jobTests();
        cancelledWorkerIdleTests();
        std::cout << "Core unit tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
