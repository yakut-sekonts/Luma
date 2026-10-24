#include "apps/shared/PhotoDecoder.h"
#include "core/export/LinearOutput.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/FileHasher.h"
#include "platform/macos/ImageAlignment.h"
#include "tests/ux/CrashScenarios.h"
#include "tests/ux/MemoryPressureProbe.h"
#include "tests/ux/PersistenceProbe.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QtEndian>
#include <algorithm>
#include <bit>
#include <cerrno>
#include <cmath>
#include <map>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef LUMA_UX_MERGE_PROBE
#error "Merge observation is restricted to the UX executable"
#endif
namespace ux {
// Copy the real handle on the GUI thread. Cancellation clears aiBusy before the
// worker exits; a terminal handle is required before inspecting cleanup.
class MergeJobProbe final {
  public:
    static luma::JobHandle job(const EditorController &editor);
};
luma::JobHandle MergeJobProbe::job(const EditorController &editor) {
    return editor.aiJob_;
}
namespace {
struct WriteHold {
    SqliteFault &fault;
    explicit WriteHold(SqliteFault &value) : fault(value) {
        fault.holdWrites(true);
    }
    ~WriteHold() {
        fault.holdWrites(false);
    }
    void release() {
        fault.holdWrites(false);
    }
};
void idle(Context &c) {
    c.driver->wait(
        [&] { return !c.library.working() && !c.library.loading() && !c.library.selecting(); },
        "Merge Library settled");
}
std::vector<luma::AssetRecord> importSources(Context &c, bool panorama) {
    auto &d = *c.driver;
    QList<QUrl> sources;
    for (const auto &name :
         QDir(c.fixture).entryList({panorama ? "*.png" : "*.dng"}, QDir::Files, QDir::Name))
        sources.append(QUrl::fromLocalFile(c.fixture + '/' + name));
    require(sources.size() == (panorama ? 2 : 3), "Wrong owned merge fixture count");
    require(d.object("library.importDialog")->setProperty("sources", QVariant::fromValue(sources)),
            "Import dialog refused merge source URLs");
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "Merge import dialog opened");
    d.click("library.importStart");
    d.wait([&] { return !c.library.working() && c.library.count() == sources.size(); },
           "Owned merge originals imported");
    idle(c);
    require(c.library.error().isEmpty(), c.library.error());
    std::vector<luma::AssetRecord> result;
    luma::Catalog catalog(c.catalogFile.toStdString());
    for (const auto &id : catalog.libraryIDs({})) {
        const auto asset = catalog.assetByID(id);
        require(asset.has_value(), "Imported merge source disappeared");
        result.push_back(*asset);
    }
    d.click(c.card(0));
    d.click(c.card(static_cast<int>(sources.size()) - 1), Qt::ShiftModifier);
    idle(c);
    require(c.library.selectedCount() == sources.size(), "Merge multi-selection failed");
    return result;
}
// Independent bounded TIFF reader: inspect the stored Float DNG, not a preview
// or production decoder that could normalize/clamp the values under test.
struct FloatDng {
    QByteArray bytes;
    int width = 0, height = 0;
    double maximum = 0;
    std::vector<float> pixels;
    explicit FloatDng(const std::filesystem::path &path) {
        QFile file(QString::fromStdString(path.string()));
        require(file.open(QIODevice::ReadOnly), "Cannot inspect derived DNG");
        bytes = file.readAll();
        auto u16 = [&](quint64 offset) {
            require(offset + 2 <= static_cast<quint64>(bytes.size()), "Truncated TIFF short");
            return qFromLittleEndian<quint16>(bytes.constData() + offset);
        };
        auto u32 = [&](quint64 offset) {
            require(offset + 4 <= static_cast<quint64>(bytes.size()), "Truncated TIFF long");
            return qFromLittleEndian<quint32>(bytes.constData() + offset);
        };
        require(bytes.startsWith("II") && u16(2) == 42, "Derived output is not little-endian TIFF");
        auto directory = [&](quint64 offset) {
            std::map<unsigned, quint64> tags;
            const auto count = u16(offset);
            require(count < 256, "Unbounded derived TIFF directory");
            for (unsigned i = 0; i < count; ++i) {
                const auto entry = offset + 2 + 12 * i;
                (void)u32(entry + 8);
                require(tags.emplace(u16(entry), entry).second, "Duplicate TIFF field");
            }
            return tags;
        };
        auto fields = directory(u32(4));
        require(fields.contains(330), "Derived output has no LinearRaw SubIFD");
        fields = directory(u32(fields.at(330) + 8));
        auto values = [&](unsigned tag, unsigned type, unsigned count) {
            require(fields.contains(tag), "Missing derived TIFF field");
            const auto entry = fields.at(tag);
            require(u16(entry + 2) == type && u32(entry + 4) == count,
                    "Incorrect derived TIFF field representation");
            const unsigned size = type == 3 ? 2 : 4;
            const quint64 start = size * count > 4 ? u32(entry + 8) : entry + 8;
            std::vector<quint32> result;
            for (unsigned i = 0; i < count; ++i)
                result.push_back(type == 3 ? u16(start + i * size) : u32(start + i * size));
            return result;
        };
        require(values(258, 3, 3) == std::vector<quint32>{32, 32, 32} &&
                    values(339, 3, 1).front() == 3 && values(262, 3, 1).front() == 34892 &&
                    values(259, 3, 1).front() == 1 && values(50717, 4, 1).front() == 1,
                "Derived DNG lost floating-point LinearRaw representation");
        width = static_cast<int>(values(256, 4, 1).front());
        height = static_cast<int>(values(257, 4, 1).front());
        require(width > 0 && width <= 1024 && height > 0 && height <= 1024,
                "Unexpected merge fixture dimensions");
        const quint64 start = values(273, 4, 1).front();
        const quint64 length = values(279, 4, 1).front();
        require(length == static_cast<quint64>(width) * height * 12 &&
                    start + length <= static_cast<quint64>(bytes.size()),
                "Truncated or incorrectly sized Float DNG pixels");
        for (quint64 i = 0; i < length; i += 4) {
            const float value = std::bit_cast<float>(u32(start + i));
            require(std::isfinite(value) && value >= 0, "Invalid radiometric merge sample");
            maximum = std::max(maximum, static_cast<double>(value));
            pixels.push_back(value);
        }
    }
};
void unchanged(Context &c, const std::vector<luma::AssetRecord> &sources) {
    luma::Catalog catalog(c.catalogFile.toStdString());
    for (const auto &before : sources) {
        const auto after = catalog.assetByID(before.id);
        require(after && after->path == before.path && after->checksum == before.checksum &&
                    after->edits == before.edits &&
                    luma::macos::sha256File(before.path) == before.checksum,
                "Merge changed a source file or its Develop edits");
    }
}
} // namespace
void prepareOfflineModelFixture(const QString &root) {
    // Verify the process-level network sandbox, without changing the host's
    // network settings or contacting an external service.
    const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket < 0)
        throw std::runtime_error("Cannot create the offline network-denial probe");
    sockaddr_in address{};
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_port = htons(9);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const auto result =
        ::connect(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    const int failure = errno;
    ::close(socket);
    require(result == -1 && failure == EPERM, "Offline workflow requires actual network denial");
    require(!QFileInfo::exists(root + "/preview.ini"), "Offline settings fixture is not fresh");
    // Owned persisted configuration models a custom provider that is no longer
    // available. The real Core ML loader must report the missing model itself.
    QSettings settings(root + "/preview.ini", QSettings::IniFormat);
    settings.setValue("denoiseModel", root + "/unavailable.mlmodelc");
    settings.sync();
    require(settings.status() == QSettings::NoError, "Cannot persist offline model fixture");
}
bool offlineDailyScenario(Context &c) {
    auto &d = *c.driver;
    const auto sources = importSources(c, false);
    const auto source = std::find_if(sources.begin(), sources.end(), [](const auto &asset) {
        return asset.path.filename() == "Frame02.dng";
    });
    require(source != sources.end(), "Offline workflow lacks its RAW source");
    const QString card = "library.card." + QString::fromStdString(source->id);
    d.doubleClick(card);
    c.loaded();
    c.saved();
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto baseline = catalog.assetSnapshot(source->id).value();
    require(c.editor.denoiseModelConfigured() && !c.editor.denoiseModelBundled() &&
                c.editor.denoiseModelName() == "unavailable.mlmodelc",
            "Offline fixture did not load the owned unavailable provider configuration");
    c.reveal("denoise.create");
    d.click("denoise.create");
    const auto job = MergeJobProbe::job(c.editor);
    d.wait([&] { return !c.editor.aiBusy() && !c.editor.error().isEmpty(); },
           "Actual unavailable Core ML provider reports its error");
    d.wait(
        [&] {
            return job.state() != luma::JobState::queued && job.state() != luma::JobState::running;
        },
        "Unavailable provider worker exits");
    const auto providerError = c.editor.error();
    d.frame();
    require(d.item("main.editorError")->isVisible() &&
                c.editor.currentSettings() == baseline.asset.edits &&
                catalog.assetSnapshot(source->id)->revision == baseline.revision &&
                catalog.libraryCount() == 3 && catalog.pendingDerived().empty(),
            "Provider failure changed committed edits or left a derived operation");
    d.screenshot(c.root + "/provider-unavailable.png");
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
    const auto edited = c.editor.currentSettings();
    d.wait([&] { return !c.editor.saving() && !c.editor.busy(); },
           "Non-AI edit commits while the provider remains unavailable");
    require(edited.exposure > 1 && catalog.assetSnapshot(source->id)->asset.edits == edited,
            "Unavailable AI prevented non-AI editing or durable save");
    d.click("main.undoButton");
    d.wait([&] { return !c.editor.saving(); }, "Offline Undo commits");
    require(c.editor.currentSettings() == baseline.asset.edits,
            "Offline Undo did not restore the complete document");
    d.click("main.redoButton");
    d.wait([&] { return !c.editor.saving(); }, "Offline Redo commits");
    require(c.editor.currentSettings() == edited &&
                catalog.assetSnapshot(source->id)->asset.edits == edited,
            "Offline Redo did not commit the complete document");
    const auto decoded = luma::PhotoDecoder().decode(source->path);
    const auto reference =
        luma::renderCPU(decoded.image, edited, decoded.metadata.orientation, {}, {},
                        decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
    const QImage output(c.exportPNG("Offline-daily"));
    const auto pixels = output.convertToFormat(QImage::Format_RGBA8888);
    require(!pixels.isNull() && pixels.size() == QSize(reference.width, reference.height) &&
                output.colorSpace() == QColorSpace(QColorSpace::SRgb),
            "Offline export lost full resolution or sRGB profile");
    for (int y = 0; y < pixels.height(); ++y)
        require(std::equal(pixels.constScanLine(y), pixels.constScanLine(y) + pixels.width() * 4,
                           reference.rgba.data() + y * reference.width * 4),
                "Offline export differs from the independent full-source CPU document");
    // Ordinary navigation must recover the visible error and retain both photos'
    // own documents; no direct controller error clearing is used.
    d.click("main.libraryButton");
    idle(c);
    const auto other = std::find_if(sources.begin(), sources.end(),
                                    [&](const auto &asset) { return asset.id != source->id; });
    require(other != sources.end(), "Offline workflow lacks another unchanged original");
    d.doubleClick("library.card." + QString::fromStdString(other->id));
    c.loaded();
    d.click("main.libraryButton");
    idle(c);
    d.doubleClick(card);
    c.loaded();
    c.saved();
    require(c.editor.currentSettings() == edited && catalog.pendingDerived().empty() &&
                catalog.libraryCount() == 3,
            "Offline Library return lost the edited document");
    for (const auto &original : sources) {
        const auto stored = catalog.assetByID(original.id);
        require(stored && stored->edits == (original.id == source->id ? edited : original.edits) &&
                    luma::macos::sha256File(original.path) == original.checksum,
                "Offline workflow changed an original or another photo's document");
    }
    d.log({{"action", "offline_non_ai_daily_workflow"},
           {"network_denial_verified", true},
           {"provider_error", providerError},
           {"export_cpu_pixels_exact", true},
           {"native_model_picker_navigation", false}});
    return true;
}
luma::JobHandle startMergeForUpdate(Context &c) {
    importSources(c, false);
    auto &d = *c.driver;
    d.click("library.photoActions");
    d.click("library.hdr");
    d.wait([&] { return d.object("merge.hdr")->property("opened").toBool(); },
           "HDR dialog opened before update");
    c.storageFault.holdWrites(true);
    d.click("merge.hdr.create");
    const auto job = MergeJobProbe::job(c.editor);
    d.wait([&] { return c.storageFault.writeBlocked() || !c.editor.aiBusy(); },
           "Real HDR worker reached catalog staging before update", 60000);
    require(c.editor.aiBusy() && c.storageFault.writeBlocked(),
            "HDR update fixture failed before staging: " + c.editor.error());
    return job;
}
bool denoiseWorkflowScenario(Context &c, const QString &id) {
    if (id != "UX-P07-DNS-001" && id != "UX-P07-DNS-002")
        return false;
    const bool cancel = id == "UX-P07-DNS-002";
    auto &d = *c.driver;
    QElapsedTimer elapsed;
    elapsed.start();
    const auto checkpoint = [&](const QString &phase) {
        d.log({{"action", "denoise_checkpoint"},
               {"phase", phase},
               {"elapsed_ms", elapsed.elapsed()},
               {"memory_pressure", static_cast<int>(MemoryPressureProbe::current(c.editor))},
               {"ai_busy", c.editor.aiBusy()},
               {"ai_progress", c.editor.aiProgress()},
               {"job_state", static_cast<int>(MergeJobProbe::job(c.editor).state())},
               {"write_blocked", c.storageFault.writeBlocked()},
               {"error", c.editor.error()}});
    };
    // Observe actual OS notifications and production state without injecting
    // pressure, changing cancellation or writing substitute heartbeat files.
    QObject observer;
    auto previousPressure = MemoryPressureProbe::current(c.editor);
    QObject::connect(&c.editor, &EditorController::stateChanged, &observer, [&] {
        const auto pressure = MemoryPressureProbe::current(c.editor);
        if (pressure != previousPressure) {
            previousPressure = pressure;
            checkpoint("memory_pressure_changed");
        }
    });
    checkpoint("started");
    auto sources = importSources(c, false);
    const auto source = std::find_if(sources.begin(), sources.end(), [](const auto &asset) {
        return asset.path.filename() == "Frame02.dng";
    });
    require(source != sources.end(), "Missing owned RAW for denoise");
    d.doubleClick("library.card." + QString::fromStdString(source->id));
    c.loaded();
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
    c.saved();
    source->edits = c.editor.currentSettings();
    require(source->edits.exposure > 1 && c.editor.canUndo(),
            "Denoise fixture requires a committed edited source");
    require(c.editor.denoiseModelBundled(), "AI Denoise must use the actual bundled model");
    c.reveal("denoise.create");
    checkpoint("before_create");
    WriteHold hold(c.storageFault);
    d.click("denoise.create");
    checkpoint("created");
    const auto job = MergeJobProbe::job(c.editor);
    d.wait([&] { return c.storageFault.writeBlocked() || !c.editor.aiBusy(); },
           "Actual CoreML denoise reached durable staging", 60000);
    checkpoint("staging_wait_finished");
    require(c.storageFault.writeBlocked(), "Denoise failed before staging: " + c.editor.error());
    c.reveal("denoise.cancel");
    d.frame();
    require(c.editor.aiBusy() && c.editor.aiOperation() == "AI Denoise" &&
                d.item("denoise.progress")->isVisible() && d.item("denoise.cancel")->isEnabled() &&
                !d.item("denoise.create")->isEnabled() &&
                d.object("denoise.progress")->property("value").toDouble() >= .85 &&
                d.object("denoise.progress")->property("value").toDouble() <= 1 &&
                c.editor.currentSettings() == source->edits,
            "AI Denoise processing/progress/Cancel or source state is incorrect");
    d.screenshot(c.root + "/denoise-processing.png");
    checkpoint("processing_captured");
    if (cancel)
        d.click("denoise.cancel");
    hold.release();
    checkpoint("staging_released");
    d.wait(
        [&] {
            return job.state() != luma::JobState::queued && job.state() != luma::JobState::running;
        },
        "Actual denoise worker terminated");
    checkpoint("worker_finished");
    if (cancel) {
        d.frame();
        luma::Catalog catalog(c.catalogFile.toStdString());
        const auto files = QDir(c.catalogFile + ".luma-derived")
                               .entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        require(!c.editor.aiBusy() && c.editor.error().isEmpty() &&
                    c.editor.photoID().toStdString() == source->id &&
                    c.editor.currentSettings() == source->edits && c.editor.canUndo() &&
                    catalog.libraryCount() == 3 && catalog.pendingDerived().empty() &&
                    files.empty(),
                "Denoise Cancel changed committed source or left partial output");
        d.log({{"action", "denoise_cancel_before_install"},
               {"catalog_count", 3},
               {"files", QJsonArray::fromStringList(files)}});
    } else {
        d.wait([&] { return !c.editor.aiBusy() && c.editor.photoID().toStdString() != source->id; },
               "Denoised DNG became the selected asset");
        checkpoint("result_selected");
        c.loaded();
        c.saved();
        idle(c);
        checkpoint("result_loaded");
        luma::Catalog catalog(c.catalogFile.toStdString());
        const auto result = catalog.activeAsset();
        require(result && catalog.libraryCount() == 4 && c.library.count() == 4 &&
                    catalog.pendingDerived().empty() &&
                    c.editor.currentSettings() == luma::EditDocument{},
                "Denoise did not publish exactly one unedited result");
        const FloatDng output(result->path);
        require(output.width == 256 && output.height == 192 &&
                    output.bytes.contains("luma:Operation=\"AI Denoise\"") &&
                    output.bytes.contains(QByteArray::fromStdString(source->id)) &&
                    output.bytes.contains(QByteArray::fromStdString(source->checksum)) &&
                    output.bytes.contains("luma:Amount=\"100\"") &&
                    output.bytes.contains("luma:Provider="),
                "Denoised Float DNG lost dimensions or provenance");
        luma::EditDocument neutral;
        neutral.enabledCorrections = 0;
        const auto input =
            luma::renderLinearOutput(luma::PhotoDecoder().decode(source->path), neutral);
        double difference = 0;
        for (std::size_t i = 0; i < output.pixels.size(); ++i) {
            const double delta = output.pixels[i] - input.rgba[i / 3 * 4 + i % 3];
            difference += delta * delta;
        }
        difference /= output.pixels.size();
        d.log({{"action", "actual_bundled_denoise_output"},
               {"linear_mse_from_input", difference},
               {"width", output.width},
               {"height", output.height},
               {"model", c.editor.denoiseModelName()},
               {"image_quality_qualification", false}});
        require(difference > 1e-8, "Denoise output is an unchanged input copy");
        const auto before = c.editor.currentSettings();
        c.reveal("develop.contrastSlider");
        d.drag("develop.contrastSlider", d.sliderThumb("develop.contrastSlider"), {.75, .5});
        c.saved();
        require(c.editor.currentSettings().contrast > 1 && c.editor.canUndo(),
                "Denoised Float DNG cannot be edited");
        const auto expected = fixtureReference(c);
        d.wait(
            [&] {
                return previewPixelsAgree(c.provider->requestImage({}, nullptr, {}), expected,
                                          c.editor.metalPreview());
            },
            "Denoised Develop preview matches independent CPU output");
        d.click("main.undoButton");
        c.saved();
        require(c.editor.currentSettings() == before && c.editor.canRedo() &&
                    luma::macos::sha256File(result->path) == result->checksum,
                "Denoised Undo changed its initial document or file");
    }
    unchanged(c, sources);
    return true;
}
bool mergeWorkflowScenario(Context &c, const QString &id) {
    const QStringList supported{
        "UX-P08-MRG-001", "UX-P08-MRG-003", "UX-P08-MRG-004", "UX-P08-MRG-005", "UX-P08-MRG-006",
        "UX-P08-MRG-009", "UX-P08-MRG-010", "UX-P08-HDR-001", "UX-P08-HDR-009", "UX-P08-PAN-001",
        "UX-P08-PAN-002", "UX-P08-FLT-002", "UX-P08-PAN-004", "UX-P08-ALG-002", "UX-P08-ALG-001"};
    if (!supported.contains(id))
        return false;
    const bool reject = id == "UX-P08-PAN-004" || id == "UX-P08-ALG-002";
    const bool panorama = id.startsWith("UX-P08-PAN-") || id == "UX-P08-ALG-001" || reject;
    const bool cancel = id == "UX-P08-MRG-004" || id == "UX-P08-MRG-005";
    auto &d = *c.driver;
    const auto sources = importSources(c, panorama);
    if (id == "UX-P08-ALG-001") {
        // Exercise the actual platform API separately, then verify its real UI
        // consumer below. These observations do not replace the merge worker.
        QImage reference(c.fixture + "/Frame01.png"), floating(c.fixture + "/Frame02.png");
        reference = reference.convertToFormat(QImage::Format_RGBA8888);
        floating = floating.convertToFormat(QImage::Format_RGBA8888);
        require(!reference.isNull() && !floating.isNull(), "Missing alignment ground truth");
        const auto transform = luma::macos::alignImages(
            {reference.width(),
             reference.height(),
             {reference.constBits(), static_cast<std::size_t>(reference.sizeInBytes())}},
            {floating.width(),
             floating.height(),
             {floating.constBits(), static_cast<std::size_t>(floating.sizeInBytes())}},
            {}, luma::macos::AlignmentMode::panorama);
        QJsonArray coefficients;
        for (const auto value : transform.values) {
            require(std::isfinite(value) && std::abs(value) <= 1e6,
                    "Non-finite or excessive alignment coefficient");
            coefficients.append(value);
        }
        const auto inverse = transform.inverse();
        double maximumError = 0;
        for (int y = 0; y <= 10; ++y)
            for (int x = 0; x <= 10; ++x) {
                const double u = x / 10., v = y / 10.;
                const auto point = transform.point(u, v);
                const auto restored = inverse.point(point[0], point[1]);
                require(std::isfinite(point[0]) && std::isfinite(point[1]) &&
                            std::abs(restored[0] - u) < 1e-9 && std::abs(restored[1] - v) < 1e-9,
                        "Alignment grid is non-finite or non-invertible");
                maximumError = std::max({maximumError, std::abs(point[0] - u - .25) * 256,
                                         std::abs(point[1] - v) * 192});
            }
        d.log({{"action", "platform_alignment_bounds"},
               {"coefficients", coefficients},
               {"maximum_ground_truth_pixel_error", maximumError},
               {"pixel_budget", 1.0}});
        require(maximumError <= 1, "Alignment exceeds one pixel of the known translation");
    }
    d.click("library.photoActions");
    d.click(panorama ? "library.panorama" : "library.hdr");
    const QString dialog = panorama ? "merge.panorama" : "merge.hdr";
    d.wait([&] { return d.object(dialog)->property("opened").toBool(); },
           "Selected merge dialog opened");
    if (panorama)
        d.choose("merge.projection", 0);
    // Real catalog writes are held only at the storage boundary, after actual
    // decode, Vision alignment, merge and Float DNG encoding have completed.
    WriteHold hold(c.storageFault);
    d.click(dialog + ".create");
    const auto job = MergeJobProbe::job(c.editor);
    d.wait([&] { return c.storageFault.writeBlocked() || !c.editor.aiBusy(); },
           "Actual merge reached durable catalog staging", 60000);
    if (reject) {
        require(!c.storageFault.writeBlocked(), "Unrelated originals produced a staged panorama");
        hold.release();
        d.wait(
            [&] {
                return job.state() != luma::JobState::queued &&
                       job.state() != luma::JobState::running;
            },
            "Refused alignment worker terminated");
        d.frame();
        luma::Catalog catalog(c.catalogFile.toStdString());
        require(!c.editor.aiBusy() && c.editor.error().contains("align") &&
                    d.item("main.editorError")->isVisible() &&
                    d.object("main.editorError")->property("text").toString() == c.editor.error() &&
                    catalog.libraryCount() == static_cast<std::int64_t>(sources.size()) &&
                    catalog.pendingDerived().empty() &&
                    QDir(c.catalogFile + ".luma-derived")
                        .entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)
                        .empty(),
                "Alignment refusal did not explain failure or preserve clean storage");
        d.log({{"action", "unrelated_panorama_refused"}, {"visible_error", c.editor.error()}});
        d.screenshot(c.root + "/alignment-refused.png");
        unchanged(c, sources);
        // The runner requires a recovered, error-free final UI. Open an intact
        // source through its card instead of suppressing the expected error.
        d.doubleClick(c.card(0));
        c.loaded();
        c.saved();
        return true;
    }
    require(c.storageFault.writeBlocked(), "Merge failed before staging: " + c.editor.error());
    d.frame();
    require(c.editor.aiBusy() && d.item("merge.progress")->isVisible() &&
                d.item("merge.cancel")->isEnabled() &&
                d.object("merge.operation")->property("text").toString() ==
                    (panorama ? "Panorama merge" : "HDR merge") &&
                d.object("merge.progress")->property("value").toDouble() > 0 &&
                d.object("merge.progress")->property("value").toDouble() <= 1,
            "Active merge phase/progress/Cancel is not visible");
    require(d.item("merge.phase")->isVisible() &&
                d.object("merge.phase")->property("text").toString() == "Writing Float DNG…",
            "Merge did not expose its actual encoding phase");
    d.screenshot(c.root + "/merge-progress.png");
    if (cancel)
        d.click("merge.cancel");
    hold.release();
    d.wait(
        [&] {
            return job.state() != luma::JobState::queued && job.state() != luma::JobState::running;
        },
        "Actual merge worker reached its terminal state");
    if (cancel) {
        require(!c.editor.aiBusy() && c.editor.error().isEmpty(),
                "Cancel left merge UI busy/error");
        luma::Catalog catalog(c.catalogFile.toStdString());
        const auto folder = c.catalogFile + ".luma-derived";
        const auto files =
            QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        d.log({{"action", "merge_cancel_before_install"},
               {"files", QJsonArray::fromStringList(files)},
               {"catalog_count", static_cast<qint64>(catalog.libraryCount())},
               {"pending_records", static_cast<qint64>(catalog.pendingDerived().size())}});
        require(catalog.libraryCount() == static_cast<std::int64_t>(sources.size()) &&
                    catalog.pendingDerived().empty() && files.empty(),
                "Cancellation before install left a derived file, journal or catalog result");
    } else {
        d.wait([&] { return !c.editor.aiBusy() && c.editor.hasPhoto(); },
               "Merged Float DNG opened in Develop");
        c.loaded();
        c.saved();
        idle(c);
        require(c.editor.error().isEmpty(), c.editor.error());
        luma::Catalog catalog(c.catalogFile.toStdString());
        const auto result = catalog.assetByID(c.editor.photoID().toStdString());
        require(result && c.library.count() == static_cast<int>(sources.size() + 1) &&
                    catalog.libraryCount() == static_cast<std::int64_t>(sources.size() + 1) &&
                    catalog.pendingDerived().empty(),
                "Successful merge did not enter Library exactly once");
        const FloatDng output(result->path);
        for (const auto &source : sources)
            require(
                output.bytes.contains(QByteArray::fromStdString(source.id + ':' + source.checksum)),
                "Derived provenance omitted a source identity/checksum");
        if (!panorama)
            require(output.width == 256 && output.height == 192 && output.maximum > 1.1,
                    "HDR did not preserve source geometry and values above SDR white");
        else if (id.endsWith("001"))
            require(std::abs(output.width - 320) <= 5 && std::abs(output.height - 192) <= 5,
                    "Horizontal panorama lost the known 64-pixel overlap displacement");
        else
            require(std::abs(output.width - 256) <= 5 && std::abs(output.height - 240) <= 5,
                    "Vertical panorama lost the known 48-pixel overlap displacement");
        if (panorama) {
            const QImage first(c.fixture + "/Frame01.png"), second(c.fixture + "/Frame02.png");
            require(!first.isNull() && !second.isNull(), "Cannot read panorama pixel ground truth");
            double maximumError = 0;
            for (int y = 10; y < output.height - 10; y += 5)
                for (int x = 10; x < output.width - 10; x += 5) {
                    const auto color = x < 256 && y < 192
                                           ? first.pixelColor(x, y)
                                           : second.pixelColor(x - (id.endsWith("001") ? 64 : 0),
                                                               y - (id.endsWith("002") ? 48 : 0));
                    const double encoded = color.red() / 255.0;
                    const double expected = encoded <= .04045
                                                ? encoded / 12.92
                                                : std::pow((encoded + .055) / 1.055, 2.4);
                    const auto offset = (static_cast<std::size_t>(y) * output.width + x) * 3;
                    for (int channel = 0; channel < 3; ++channel)
                        maximumError = std::max(
                            maximumError, std::abs(output.pixels[offset + channel] - expected));
                }
            d.log({{"action", "panorama_known_world_pixels"},
                   {"maximum_error", maximumError},
                   {"linear_rgb_budget", .001}});
            require(maximumError <= .001,
                    "Panorama pixels differ from the independently reconstructed source scene");
        }
        d.log({{"action", "merged_float_dng"},
               {"width", output.width},
               {"height", output.height},
               {"maximum_linear_value", output.maximum},
               {"source_count", static_cast<int>(sources.size())}});
        const auto original = c.editor.currentSettings();
        const auto before = c.editor.previewUrl();
        c.reveal("develop.exposureSlider");
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {1., .5});
        c.saved();
        d.wait([&] { return c.editor.previewUrl() != before && !c.editor.histogramPending(); },
               "Derived-only exposure preview completed");
        require(c.editor.currentSettings().exposure == 5 && c.editor.canUndo(),
                "Float DNG could not be independently edited");
        const auto decoded = luma::PhotoDecoder().decode(result->path);
        if (!panorama)
            require(*std::max_element(decoded.image.rgb.begin(), decoded.image.rgb.end()) > 1.1F,
                    "Reopening Float DNG clipped its HDR samples");
        auto verifyPixels = [&] {
            const auto document = c.editor.currentSettings();
            document.validate();
            const auto linear = luma::renderLinearOutput(decoded, document);
            require(std::all_of(linear.rgba.begin(), linear.rgba.end(),
                                [](float value) { return std::isfinite(value); }),
                    "Develop adjustment produced non-finite Float DNG output");
            const auto expected = fixtureReference(c, document, c.editor.sourcePath());
            d.wait(
                [&] {
                    return previewPixelsAgree(c.provider->requestImage({}, nullptr, {}), expected,
                                              c.editor.metalPreview());
                },
                "Edited Float DNG preview matches saved Develop settings");
        };
        verifyPixels();
        if (id == "UX-P08-FLT-002") {
            for (const auto *parameter : {"contrast", "highlights", "shadows", "saturation"}) {
                const auto control = QStringLiteral("develop.") + parameter + "Slider";
                c.reveal(control);
                d.drag(control, d.sliderThumb(control), {.75, .5});
                c.saved();
                require(std::abs(c.editor.currentSettings().parameter(parameter)) > 1,
                        "Float DNG slider did not change its parameter");
                verifyPixels();
                d.click("main.undoButton");
                c.saved();
            }
        }
        d.click("main.undoButton");
        c.saved();
        require(c.editor.currentSettings() == original && c.editor.canRedo() &&
                    luma::macos::sha256File(result->path) == result->checksum,
                "Derived Undo or file preservation failed");
    }
    unchanged(c, sources);
    return true;
}
bool mergeDailyScenario(Context &c, const QString &variant) {
    require(variant == "hdr" || variant == "panorama", "Unknown daily merge variant");
    require(mergeWorkflowScenario(c, variant == "hdr" ? "UX-P08-HDR-001" : "UX-P08-PAN-001"),
            "Daily workflow requires a real completed merge");
    auto &d = *c.driver;
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto result = catalog.activeAsset();
    require(result.has_value(), "Daily merge has no active derived result");
    std::vector<luma::AssetRecord> originals;
    for (const auto &photo : catalog.libraryIDs({}))
        if (photo != result->id)
            originals.push_back(catalog.assetByID(photo).value());
    require(originals.size() == (variant == "hdr" ? 3 : 2), "Daily merge lost its source set");
    const QString derivedCard = "library.card." + QString::fromStdString(result->id);
    d.click("main.libraryButton");
    idle(c);
    d.wait([&] { return d.find(derivedCard) != nullptr; }, "Derived PhotoCard populated");
    d.click(derivedCard);
    require(c.library.selectionIDs() == std::vector<std::string>{result->id},
            "Derived card is not independently selectable in Library");
    d.doubleClick(derivedCard);
    c.loaded();
    require(!c.window->property("libraryMode").toBool() &&
                c.editor.photoID().toStdString() == result->id,
            "Derived card did not open its own Develop document");
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.3, .5});
    c.saved();
    const auto edited = c.editor.currentSettings();
    const auto committed = catalog.assetSnapshot(result->id);
    require(committed && committed->asset.edits == edited && edited.exposure < -1,
            "Daily derived edit did not commit the complete document");
    const auto decoded = luma::PhotoDecoder().decode(result->path);
    const auto reference =
        luma::renderCPU(decoded.image, edited, decoded.metadata.orientation, {}, {},
                        decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
    const QImage output(c.exportPNG("Daily-" + variant));
    const auto pixels = output.convertToFormat(QImage::Format_RGBA8888);
    require(!pixels.isNull() && pixels.size() == QSize(reference.width, reference.height) &&
                output.colorSpace() == QColorSpace(QColorSpace::SRgb),
            "Derived export lost full resolution or sRGB profile");
    for (int y = 0; y < pixels.height(); ++y)
        require(std::equal(pixels.constScanLine(y), pixels.constScanLine(y) + pixels.width() * 4,
                           reference.rgba.data() + y * reference.width * 4),
                "Derived export differs from the independent full-source CPU document");
    d.wait([&] { return !d.object("exportPanel")->property("visible").toBool(); },
           "Starting the actual export dismissed its settings panel");
    d.click("main.libraryButton");
    idle(c);
    const QString sourceCard = "library.card." + QString::fromStdString(originals.front().id);
    d.wait([&] { return d.find(sourceCard) != nullptr; }, "Original PhotoCard populated");
    d.doubleClick(sourceCard);
    c.loaded();
    require(c.editor.photoID().toStdString() == originals.front().id &&
                c.editor.currentSettings() == originals.front().edits,
            "Derived editing crossed into an original's document");
    d.click("main.libraryButton");
    idle(c);
    d.wait([&] { return d.find(derivedCard) != nullptr; }, "Derived PhotoCard repopulated");
    d.doubleClick(derivedCard);
    c.loaded();
    c.saved();
    const auto returned = catalog.assetSnapshot(result->id);
    require(returned && c.editor.photoID().toStdString() == result->id &&
                c.editor.currentSettings() == edited && returned->revision == committed->revision &&
                catalog.pendingDerived().empty() &&
                catalog.libraryCount() == static_cast<qint64>(originals.size() + 1) &&
                luma::macos::sha256File(result->path) == result->checksum,
            "Library round trip lost the derived document or duplicated/mutated its file");
    unchanged(c, originals);
    d.log({{"action", "derived_daily_library_edit_export"},
           {"variant", variant},
           {"result", QString::fromStdString(result->id)},
           {"export_width", reference.width},
           {"export_height", reference.height},
           {"export_cpu_pixels_exact", true},
           {"derived_sha256", QString::fromStdString(result->checksum)}});
    return true;
}
bool mergeOptionsScenario(Context &c, const QString &id) {
    if (id != "UX-P08-HDR-006" && id != "UX-P08-HDR-007" && id != "UX-P08-DGH-001")
        return false;
    const bool alignment = id == "UX-P08-HDR-006";
    auto &d = *c.driver;
    const auto sources = importSources(c, false);
    const auto middle = std::find_if(sources.begin(), sources.end(), [](const auto &source) {
        return source.path.filename() == "Frame02.dng";
    });
    require(middle != sources.end(), "Missing reference exposure ground truth");
    luma::EditDocument neutral;
    neutral.enabledCorrections = 0;
    const auto reference =
        luma::renderLinearOutput(luma::PhotoDecoder().decode(middle->path), neutral);
    std::vector<std::vector<float>> pixels;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass) {
            d.click("main.libraryButton");
            idle(c);
            d.click("library.clearSelection");
            for (std::size_t i = 0; i < sources.size(); ++i)
                d.click("library.card." + QString::fromStdString(sources[i].id),
                        i ? Qt::ControlModifier : Qt::NoModifier);
            idle(c);
        }
        require(c.library.selectedCount() == 3, "HDR option run lost original multi-selection");
        d.click("library.photoActions");
        d.click("library.hdr");
        d.wait([&] { return d.object("merge.hdr")->property("opened").toBool(); },
               "HDR options dialog opened");
        const bool enabled = alignment && pass == 1;
        if (d.object("merge.alignment")->property("checked").toBool() != enabled)
            d.click("merge.alignment");
        d.drag("merge.deghost", d.sliderThumb("merge.deghost"),
               {alignment || pass == 0 ? 0. : 1., .5});
        const double strength = alignment || pass == 0 ? 0 : 100;
        require(d.object("merge.alignment")->property("checked").toBool() == enabled &&
                    d.object("merge.deghost")->property("value").toDouble() == strength,
                "Actual HDR controls did not retain the requested options");
        const auto previous = c.editor.photoID();
        d.click("merge.hdr.create");
        const auto job = MergeJobProbe::job(c.editor);
        d.wait([&] { return !c.editor.aiBusy(); }, "HDR options worker completed", 60000);
        require(c.editor.error().isEmpty(), c.editor.error());
        d.wait(
            [&] {
                return c.editor.photoID() != previous && job.state() != luma::JobState::running;
            },
            "HDR option result became the active derived asset");
        c.loaded();
        c.saved();
        idle(c);
        luma::Catalog catalog(c.catalogFile.toStdString());
        const auto result = catalog.activeAsset();
        require(result && catalog.libraryCount() == 4 + pass && catalog.pendingDerived().empty(),
                "HDR option run did not publish exactly one distinct result");
        const FloatDng output(result->path);
        require(
            output.width == reference.width && output.height == reference.height &&
                output.bytes.contains("luma:AlignFrames=\"" + QByteArray::number(enabled) + "\"") &&
                output.bytes.contains("luma:Deghost=\"" + QByteArray::number(strength / 100) +
                                      "\""),
            "HDR output geometry/provenance differs from the chosen options");
        const auto expected = fixtureReference(c);
        d.wait(
            [&] {
                return previewPixelsAgree(c.provider->requestImage({}, nullptr, {}), expected,
                                          c.editor.metalPreview());
            },
            "HDR option result preview matches its actual Float DNG");
        pixels.push_back(output.pixels);
        unchanged(c, sources);
    }
    std::array<double, 2> errors{};
    double difference = 0;
    std::size_t samples = 0;
    for (int y = 20; y < 172; ++y)
        for (int x = 20; x < 200; ++x) {
            if (!alignment &&
                !(y >= 62 && y < 90 && ((x >= 52 && x < 76) || (x >= 116 && x < 140))))
                continue;
            const auto offset = static_cast<std::size_t>(y) * reference.width + x;
            for (int channel = 0; channel < 3; ++channel) {
                const double expected = reference.rgba[offset * 4 + channel];
                for (int pass = 0; pass < 2; ++pass) {
                    const double error = pixels[pass][offset * 3 + channel] - expected;
                    errors[pass] += error * error;
                }
                const double delta =
                    pixels[0][offset * 3 + channel] - pixels[1][offset * 3 + channel];
                difference += delta * delta;
                ++samples;
            }
        }
    for (auto &error : errors)
        error /= samples;
    difference /= samples;
    d.log({{"action", alignment ? "hdr_alignment_ground_truth" : "hdr_motion_ground_truth"},
           {"disabled_mse", errors[0]},
           {"enabled_mse", errors[1]},
           {"option_difference_mse", difference},
           {"maximum_error_ratio", .6}});
    require(errors[0] > 1e-6 && difference > 1e-6 && errors[1] < errors[0] * .6,
            "HDR option did not improve the known shifted/moving-object scene");
    return true;
}
bool mergeRestartScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-P08-PAN-012" && id != "UX-P08-FLT-001" && id != "UX-P07-DNS-008")
        return false;
    auto &d = *c.driver;
    if (stage == "prepare") {
        require(id == "UX-P07-DNS-008"
                    ? denoiseWorkflowScenario(c, "UX-P07-DNS-001")
                    : mergeWorkflowScenario(c, id == "UX-P08-PAN-012" ? "UX-P08-PAN-001"
                                                                      : "UX-P08-HDR-001"),
                "Merge restart requires a complete successful creation workflow");
        c.reveal("develop.exposureSlider");
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.3, .5});
        c.saved();
        const auto document = c.editor.currentSettings();
        require(document.exposure < -1,
                "Restart fixture needs a range-revealing negative exposure");
        luma::Catalog catalog(c.catalogFile.toStdString());
        const auto active = catalog.activeAsset();
        require(active.has_value(), "Derived restart asset is missing");
        luma::Catalog expected((c.stateRoot + "/expected.sqlite").toStdString());
        const auto stored = expected.addAndSelect(active->path, active->checksum, active->metadata);
        expected.saveEdits(stored.id, document);
        QJsonArray sources;
        for (const auto &sourceID : catalog.libraryIDs({})) {
            const auto source = catalog.assetByID(sourceID);
            require(source.has_value(), "Restart source oracle disappeared");
            sources.append(QJsonObject{{"id", QString::fromStdString(sourceID)},
                                       {"path", QString::fromStdString(source->path.string())},
                                       {"checksum", QString::fromStdString(source->checksum)}});
        }
        writeJson(
            c.stateRoot + "/expected-merge.json",
            {{"id", c.editor.photoID()}, {"path", c.editor.sourcePath()}, {"sources", sources}});
        d.quitShortcut();
        d.wait([&] { return !c.window->isVisible() && !c.editor.saving() && !c.editor.busy(); },
               "Derived session closed before fresh process restart");
        c.checkData();
        return true;
    }
    require(stage == "verify", "Invalid merge restart stage");
    QFile file(c.stateRoot + "/expected-merge.json");
    require(file.open(QIODevice::ReadOnly), "Merge restart oracle is missing");
    const auto expected = QJsonDocument::fromJson(file.readAll()).object();
    const auto saved =
        luma::Catalog((c.stateRoot + "/expected.sqlite").toStdString()).activeAsset();
    require(saved.has_value(), "Complete expected derived edit document is missing");
    c.editor.restore();
    c.loaded();
    c.saved();
    require(c.editor.photoID() == expected["id"].toString() &&
                c.editor.sourcePath() == expected["path"].toString() &&
                c.editor.currentSettings() == saved->edits && !c.editor.canUndo() &&
                !c.editor.canRedo(),
            "Restart lost derived identity/edits or restored session-local Undo");
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto sources = expected["sources"].toArray();
    require(catalog.libraryCount() == sources.size() && catalog.pendingDerived().empty(),
            "Restart duplicated a derived result or left its registration pending");
    for (const auto &value : sources) {
        const auto source = value.toObject();
        const auto current = catalog.assetByID(source["id"].toString().toStdString());
        require(current && QString::fromStdString(current->path.string()) == source["path"] &&
                    QString::fromStdString(current->checksum) == source["checksum"] &&
                    luma::macos::sha256File(current->path) == current->checksum,
                "Restart changed an original or converted a derived Float DNG");
    }
    const FloatDng stored(saved->path);
    const auto decoded = luma::PhotoDecoder().decode(saved->path);
    if (id == "UX-P08-FLT-001")
        require(stored.maximum > 1.1 &&
                    *std::max_element(decoded.image.rgb.begin(), decoded.image.rgb.end()) > 1.1F,
                "Fresh process clipped the stored/decoded HDR dynamic range");
    const auto reference = fixtureReference(c, saved->edits, c.editor.sourcePath());
    d.wait(
        [&] {
            return previewPixelsAgree(c.provider->requestImage({}, nullptr, {}), reference,
                                      c.editor.metalPreview());
        },
        "Fresh process preview shows the complete saved Float DNG document");
    c.reveal("develop.contrastSlider");
    d.drag("develop.contrastSlider", d.sliderThumb("develop.contrastSlider"), {.75, .5});
    c.saved();
    require(c.editor.currentSettings().contrast > 1 && c.editor.canUndo(),
            "Restored derived asset is not editable");
    d.click("main.undoButton");
    c.saved();
    require(c.editor.currentSettings() == saved->edits && c.editor.canRedo() &&
                luma::macos::sha256File(saved->path) == saved->checksum,
            "Post-restart Undo changed the saved derived document or file");
    return true;
}
bool mergeCrashScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-P09-FLT-006")
        return false;
    auto &d = *c.driver;
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto expectedPath = (c.stateRoot + "/expected-merge-crash.sqlite").toStdString();
    if (stage == "prepare") {
        (void)importSources(c, false);
        d.doubleClick(c.card(0));
        c.loaded();
        c.reveal("develop.exposureSlider");
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.6, .5});
        c.saved();
        d.click("main.libraryButton");
        d.click(c.card(0));
        d.click(c.card(2), Qt::ShiftModifier);
        idle(c);
        catalog.backupTo(expectedPath);
        d.click("library.photoActions");
        d.click("library.hdr");
        d.wait([&] { return d.object("merge.hdr")->property("opened").toBool(); },
               "Crash HDR dialog opened");
        PersistenceGate gate(c.catalogFile.toStdString(), {"derived.before-register"});
        d.click("merge.hdr.create");
        d.wait([&] { return gate.snapshot().held || !c.editor.aiBusy(); },
               "Actual HDR installed before registration", 60000);
        const auto observed = gate.snapshot();
        require(observed.held && !observed.hookFailed && observed.events.size() == 1 &&
                    c.editor.aiBusy(),
                "HDR did not reach registration checkpoint: " + c.editor.error());
        const auto &event = observed.events.front();
        const auto pending = catalog.pendingDerived();
        require(pending.size() == 1 && pending.front().path == event.target &&
                    catalog.libraryCount() == 3 &&
                    luma::macos::sha256File(event.target) == pending.front().checksum &&
                    std::filesystem::equivalent(event.target, event.temporary),
                "Installed HDR is not recoverably journaled before registration");
        const FloatDng stored(event.target);
        require(stored.maximum > 1.1, "HDR crash fixture lost radiometric range");
        writeJson(c.stateRoot + "/merge-crash-expected.json",
                  {{"prepare_pid", QCoreApplication::applicationPid()},
                   {"path", QString::fromStdString(event.target.string())},
                   {"checksum", QString::fromStdString(pending.front().checksum)}});
        killCheckpoint(c, id, stage, "derived.before-register");
    }
    require(stage == "verify", "Invalid merge crash stage");
    const auto saved = crashExpectation(c, "merge-crash-expected.json");
    idle(c);
    luma::Catalog expected(expectedPath);
    require(c.library.error().isEmpty() && catalog.libraryCount() == 4 &&
                catalog.pendingDerived().empty(),
            "Startup did not register installed HDR exactly once: " + c.library.error());
    std::vector<luma::AssetRecord> sources;
    for (const auto &photo : expected.libraryIDs({}))
        sources.push_back(expected.assetByID(photo).value());
    unchanged(c, sources);
    std::string derivedID;
    for (const auto &photo : catalog.libraryIDs({})) {
        const auto asset = catalog.assetByID(photo).value();
        if (QString::fromStdString(asset.path.string()) == saved["path"].toString()) {
            require(derivedID.empty() &&
                        asset.checksum == saved["checksum"].toString().toStdString(),
                    "HDR recovery duplicated identity or changed checksum");
            derivedID = photo;
            require(luma::macos::sha256File(asset.path) == asset.checksum &&
                        FloatDng(asset.path).maximum > 1.1,
                    "HDR recovery changed installed Float DNG");
        }
    }
    require(!derivedID.empty(), "Recovered HDR missing from Library");
    d.doubleClick("library.card." + QString::fromStdString(derivedID));
    c.loaded();
    c.saved();
    const auto original = c.editor.currentSettings();
    const auto reference = fixtureReference(c);
    d.wait(
        [&] {
            return previewPixelsAgree(c.provider->requestImage({}, nullptr, {}), reference,
                                      c.editor.metalPreview());
        },
        "Recovered HDR preview matches independent CPU rendering");
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.3, .5});
    c.saved();
    require(c.editor.currentSettings() != original && c.editor.canUndo(),
            "Recovered HDR is not editable");
    d.click("main.undoButton");
    c.saved();
    require(c.editor.currentSettings() == original && c.editor.canRedo() &&
                catalog.libraryCount() == 4 && catalog.pendingDerived().empty(),
            "Recovered HDR Undo changed membership or complete document");
    unchanged(c, sources);
    require(QDir(c.catalogFile + ".luma-derived")
                .entryList({".luma-derived-*"}, QDir::Files | QDir::Hidden)
                .empty(),
            "HDR recovery left an owned crash staging file");
    d.log({{"action", "merge_registration_kill_recovered"}, {"registered_results", 1}});
    return true;
}

} // namespace ux
