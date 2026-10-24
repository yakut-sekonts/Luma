#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QFileInfo>
#include <QQuickItemGrabResult>
#include <QSettings>
#include <QSignalSpy>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace ux {
// Observes the controller's genuine accelerator. It never substitutes pixels,
// changes eligibility, suppresses fallback or simulates a Metal completion.
class MetalPreviewProbe final : public luma::DevelopAccelerator {
  public:
    struct Snapshot {
        unsigned calls = 0, completed = 0;
        unsigned exceptionsWithCancelledToken = 0, otherExceptions = 0, emptyResults = 0,
                 invalidSizes = 0;
        std::size_t samples = 0;
        double maximumLinearError = 0, maximumLimitRatio = 0;
        bool finite = true;
    };
    explicit MetalPreviewProbe(std::shared_ptr<luma::DevelopAccelerator> native)
        : native_(std::move(native)) {}
    static std::shared_ptr<MetalPreviewProbe> install(EditorController &editor) {
        require(QThread::currentThread() == editor.thread() && !editor.hasPhoto() &&
                    !editor.busy() && editor.accelerator_,
                "Actual Metal observation requires an empty owned controller");
        auto probe = std::make_shared<MetalPreviewProbe>(editor.accelerator_);
        editor.accelerator_ = probe;
        return probe;
    }
    static bool renderSettled(const EditorController &editor) {
        require(QThread::currentThread() == editor.thread(),
                "Render state must be read on GUI thread");
        return !editor.refineTimer_.isActive() && !editor.histogramPending_ &&
               editor.renderJob_.state() == luma::JobState::completed;
    }
    Snapshot snapshot() const {
        std::lock_guard lock(mutex_);
        return state_;
    }
    std::vector<float> develop(const luma::FloatImage &input, const luma::DevelopPipeline &pipeline,
                               luma::CancellationToken stop) override {
        {
            std::lock_guard lock(mutex_);
            ++state_.calls;
        }
        std::vector<float> result;
        try {
            result = native_->develop(input, pipeline, stop);
        } catch (...) {
            std::lock_guard lock(mutex_);
            if (stop.isCancelled())
                ++state_.exceptionsWithCancelledToken;
            else
                ++state_.otherExceptions;
            throw;
        }
        if (result.empty() || result.size() != input.rgb.size()) {
            std::lock_guard lock(mutex_);
            if (result.empty())
                ++state_.emptyResults;
            else
                ++state_.invalidSizes;
        }
        if (!result.empty() && result.size() == input.rgb.size()) {
            Snapshot observed;
            observed.completed = 1;
            observed.samples = result.size();
            for (std::size_t i = 0; i < result.size(); i += 3) {
                const auto reference =
                    pipeline.pixel({input.rgb[i], input.rgb[i + 1], input.rgb[i + 2]});
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    const double actual = result[i + channel], expected = reference[channel];
                    observed.finite =
                        observed.finite && std::isfinite(actual) && std::isfinite(expected);
                    const double difference = std::abs(actual - expected);
                    // Existing MetalDevelopTests budget, unchanged before execution.
                    const double limit = 2e-5 + 2e-5 * std::abs(expected);
                    observed.maximumLinearError = std::max(observed.maximumLinearError, difference);
                    observed.maximumLimitRatio =
                        std::max(observed.maximumLimitRatio, difference / limit);
                }
            }
            std::lock_guard lock(mutex_);
            state_.completed += observed.completed;
            state_.samples += observed.samples;
            state_.maximumLinearError =
                std::max(state_.maximumLinearError, observed.maximumLinearError);
            state_.maximumLimitRatio =
                std::max(state_.maximumLimitRatio, observed.maximumLimitRatio);
            state_.finite = state_.finite && observed.finite;
        }
        return result;
    }

  private:
    std::shared_ptr<luma::DevelopAccelerator> native_;
    mutable std::mutex mutex_;
    Snapshot state_;
};
namespace {
void ready(Context &c, const luma::EditDocument &expected, const QString &previous = {}) {
    const auto pixels = fixtureReference(c, expected, c.editor.sourcePath());
    c.driver->wait(
        [&] {
            auto *image = c.driver->item("develop.preview");
            return !c.editor.busy() && !c.editor.previewUrl().isEmpty() &&
                   c.editor.previewUrl() != previous && image->isVisible() &&
                   image->property("status").toInt() == 1 &&
                   image->property("source").toUrl().toString() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == pixels;
        },
        "Backend toggle produced a new Ready preview matching the independent CPU fixture");
    require(c.editor.currentSettings() == expected && c.editor.error().isEmpty(),
            "Preview backend changed the complete document or reported an error");
}
void neutralBackendAgreement(Context &c) {
    auto &d = *c.driver;
    const auto probe = MetalPreviewProbe::install(c.editor);
    require(c.editor.metalPreview(), "Neutral fixture requires the default Metal preference");
    c.import(1);
    d.doubleClick(c.card(0));
    c.loaded();
    c.saved();
    const auto document = c.editor.currentSettings();
    require(document == luma::EditDocument{}, "Neutral fixture has non-default edits");
    const auto reference = fixtureReference(c, document, c.editor.sourcePath());
    const auto photo = c.editor.photoID(), source = c.editor.sourcePath();
    const auto before =
        luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(photo.toStdString());
    require(before.has_value(), "Neutral fixture has no saved asset");
    const auto members = luma::Catalog(c.catalogFile.toStdString()).libraryIDs({});
    const auto selection = c.library.selectionIDs();
    QSignalSpy saves(&c.editor, &EditorController::editsSaved);
    require(saves.isValid(), "Cannot observe unwanted neutral-preview persistence");
    const auto capture = [&](bool gpu, const QString &previous) {
        try {
            d.wait(
                [&] {
                    auto *image = d.item("develop.preview");
                    return !c.editor.busy() && c.editor.error().isEmpty() &&
                           MetalPreviewProbe::renderSettled(c.editor) &&
                           c.editor.previewUrl() != previous && image->isVisible() &&
                           image->property("status").toInt() == 1 &&
                           image->property("source").toUrl().toString() == c.editor.previewUrl() &&
                           previewPixelsAgree(c.provider->requestImage({}, nullptr, {}), reference,
                                              gpu);
                },
                "Neutral backend preview matches the independent CPU reference");
        } catch (...) {
            recordPreviewDifference(c, reference, "Neutral CPU/actual Metal agreement");
            throw;
        }
        const auto observed = probe->snapshot();
        d.log({{"action", "neutral_backend_probe_before_assert"},
               {"metal_preference", gpu},
               {"actual_metal_calls", static_cast<qint64>(observed.calls)},
               {"actual_metal_completions", static_cast<qint64>(observed.completed)},
               {"linear_samples", static_cast<qint64>(observed.samples)},
               {"linear_finite", observed.finite},
               {"exceptions_with_cancelled_token",
                static_cast<qint64>(observed.exceptionsWithCancelledToken)},
               {"other_exceptions", static_cast<qint64>(observed.otherExceptions)},
               {"empty_results", static_cast<qint64>(observed.emptyResults)},
               {"invalid_sizes", static_cast<qint64>(observed.invalidSizes)},
               {"maximum_linear_error", observed.maximumLinearError},
               {"maximum_linear_limit_ratio", observed.maximumLimitRatio},
               {"histogram_pending", c.editor.histogramPending()},
               {"preview_url", c.editor.previewUrl()}});
        require(observed.calls > 0 && observed.completed == observed.calls &&
                    observed.samples > 0 && observed.finite && observed.maximumLimitRatio <= 1,
                "Neutral preview did not complete actual Metal within the linear-light budget");
        const auto after =
            luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(photo.toStdString());
        require(after && after->revision == before->revision &&
                    after->asset.edits == before->asset.edits &&
                    after->asset.path == before->asset.path &&
                    after->asset.checksum == before->asset.checksum &&
                    luma::Catalog(c.catalogFile.toStdString()).libraryIDs({}) == members &&
                    c.library.selectionIDs() == selection && c.editor.photoID() == photo &&
                    c.editor.sourcePath() == source && c.editor.currentSettings() == document &&
                    !c.editor.canUndo() && !c.editor.canRedo() && !c.editor.saving() &&
                    saves.isEmpty(),
                "Neutral backend comparison changed identity, document, history or selection");
        const auto grab = readyItemGrab(c, *d.item("develop.preview"),
                                        "Neutral backend Image-node readback completed");
        auto scene = grab->image().convertToFormat(QImage::Format_RGBA8888);
        require(!scene.isNull(), "Neutral backend has no displayed pixels");
        const auto name = gpu ? (previous.isEmpty() ? "metal-initial" : "metal-reenabled") : "cpu";
        require(scene.save(c.root + "/neutral-" + name + "-scene.png") &&
                    c.provider->requestImage({}, nullptr, {})
                        .save(c.root + "/neutral-" + name + "-preview.png"),
                "Cannot preserve actual neutral backend pixels");
        d.log({{"action", "neutral_backend_agreement"},
               {"metal_preference", gpu},
               {"actual_metal_calls", static_cast<qint64>(observed.calls)},
               {"actual_metal_completions", static_cast<qint64>(observed.completed)},
               {"linear_samples", static_cast<qint64>(observed.samples)},
               {"maximum_linear_error", observed.maximumLinearError},
               {"maximum_linear_limit_ratio", observed.maximumLimitRatio},
               {"rgba8_rgb_limit", gpu ? 1 : 0},
               {"rgba8_alpha_limit", 0}});
        return scene;
    };
    const auto initialScene = capture(true, {});
    const auto initialCalls = probe->snapshot().calls;
    QImage cpuScene;
    for (const bool enabled : {false, true}) {
        const auto previous = c.editor.previewUrl();
        d.click("settingsButton");
        d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
               "Neutral backend preferences opened");
        d.click("settings.metalPreview");
        require(c.editor.metalPreview() == enabled &&
                    d.object("settings.metalPreview")->property("checked").toBool() == enabled,
                "Actual checkbox did not change the neutral preview backend");
        d.key(Qt::Key_Escape);
        d.wait([&] { return !d.object("preferencesPanel")->property("visible").toBool(); },
               "Neutral backend preferences closed");
        const auto scene = capture(enabled, previous);
        const auto calls = probe->snapshot().calls;
        if (!enabled) {
            require(calls == initialCalls, "Disabled Metal still executed the accelerator");
            cpuScene = scene;
            require(previewPixelsAgree(initialScene, cpuScene, true),
                    "Displayed initial Metal and CPU images differ outside the existing budget");
        } else {
            require(calls > initialCalls, "Re-enabled Metal reused pixels without GPU execution");
            require(previewPixelsAgree(scene, cpuScene, true),
                    "Displayed re-enabled Metal and CPU images differ outside the existing budget");
        }
    }
    require(c.warnings.isEmpty(),
            "Neutral backend comparison produced QML warnings: " + c.warnings.join('\n'));
}
} // namespace
bool previewBackendScenario(Context &c, const QString &id) {
    if (id == "UX-P06-GPU-002") {
        neutralBackendAgreement(c);
        return true;
    }
    if (id != "UX-P06-GPU-001")
        return false;
    auto &d = *c.driver;
    c.openFirst();
    c.saved();
    const auto original = c.editor.currentSettings();
    ready(c, original);
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {1., .5});
    auto edited = original;
    edited.exposure = 5;
    c.saved();
    ready(c, edited);
    require(c.editor.canUndo() && !c.editor.canRedo(), "Backend fixture needs one real edit");
    const auto photo = c.editor.photoID(), source = c.editor.sourcePath();
    const auto before =
        luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(photo.toStdString());
    require(before.has_value(), "Backend fixture has no saved asset");
    const auto members = luma::Catalog(c.catalogFile.toStdString()).libraryIDs({});
    const auto selected = c.library.selectionIDs();
    QSignalSpy saves(&c.editor, &EditorController::editsSaved);
    require(saves.isValid(), "Cannot observe unintended persistence during a backend change");
    for (const bool enabled : {false, true, false, true}) {
        require(c.editor.metalPreview() != enabled,
                "Fixture must exercise an actual backend toggle");
        d.click("settingsButton");
        d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
               "Backend preferences opened");
        const auto previous = c.editor.previewUrl();
        d.click("settings.metalPreview");
        require(c.editor.metalPreview() == enabled &&
                    d.object("settings.metalPreview")->property("checked").toBool() == enabled,
                "Actual Metal checkbox did not acknowledge its new value");
        d.key(Qt::Key_Escape);
        d.wait([&] { return !d.object("preferencesPanel")->property("visible").toBool(); },
               "Backend preferences closed");
        ready(c, edited, previous);
        const auto after =
            luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(photo.toStdString());
        require(after && after->revision == before->revision &&
                    after->asset.edits == before->asset.edits &&
                    after->asset.id == before->asset.id &&
                    after->asset.path == before->asset.path &&
                    after->asset.checksum == before->asset.checksum &&
                    luma::Catalog(c.catalogFile.toStdString()).libraryIDs({}) == members &&
                    c.library.selectionIDs() == selected && c.editor.photoID() == photo &&
                    c.editor.sourcePath() == source && !c.editor.saving() && saves.isEmpty() &&
                    c.editor.canUndo() && !c.editor.canRedo(),
                "Backend preference changed catalog identity, saved edits, selection or history");
        QSettings preferences(QFileInfo(c.catalogFile).absolutePath() + "/preview.ini",
                              QSettings::IniFormat);
        require(preferences.value("metal").toBool() == enabled,
                "Metal preference was not persisted independently of catalog edits");
        d.log({{"action", "preview_backend_toggle"},
               {"metal_preference", enabled},
               {"catalog_revision", static_cast<qint64>(after->revision)},
               {"preview_url", c.editor.previewUrl()},
               {"scope", "UI preference and state preservation; not proof of GPU execution"}});
    }
    d.click("main.undoButton");
    ready(c, original);
    require(!c.editor.canUndo() && c.editor.canRedo(), "Backend toggle polluted Undo history");
    d.click("main.redoButton");
    ready(c, edited);
    require(c.editor.canUndo() && !c.editor.canRedo(), "Backend toggle polluted Redo history");
    require(c.warnings.isEmpty(), "Backend toggle produced QML warnings: " + c.warnings.join('\n'));
    return true;
}
} // namespace ux
