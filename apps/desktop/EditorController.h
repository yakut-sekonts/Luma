#pragma once
#include "apps/desktop/LocalizedText.h"
#include "apps/desktop/PreviewProvider.h"
#include "apps/shared/SessionService.h"
#include "core/ai/Providers.h"
#include "core/edits/EditHistory.h"
#include "core/jobs/JobManager.h"
#include "core/render/CropGeometry.h"
#include "core/render/DevelopAccelerator.h"
#include "core/render/LocalAdjustments.h"
#include "core/render/PerspectiveCorrection.h"
#include "core/render/Sharpening.h"
#include "platform/macos/MemoryPressure.h"
#include <QObject>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <array>

#ifdef LUMA_UX_MERGE_PROBE
namespace ux {
class MergeJobProbe;
}
#endif
#ifdef LUMA_UX_MEMORY_PRESSURE_PROBE
namespace ux {
class MemoryPressureProbe;
}
#endif
#ifdef LUMA_UX_PREVIEW_RESULT_PROBE
namespace ux {
class MetalPreviewProbe;
}
#endif
class EditorController final : public QObject {
    Q_OBJECT
#ifdef LUMA_UX_MERGE_PROBE
    friend class ux::MergeJobProbe;
#endif
#ifdef LUMA_UX_MEMORY_PRESSURE_PROBE
    friend class ux::MemoryPressureProbe;
#endif
#ifdef LUMA_UX_PREVIEW_RESULT_PROBE
    friend class ux::MetalPreviewProbe;
#endif
    Q_PROPERTY(QVariantMap comparison READ comparison NOTIFY stateChanged)
    Q_PROPERTY(QVariantList localComponents READ localComponents NOTIFY stateChanged)
    Q_PROPERTY(bool localSampling READ localSampling NOTIFY stateChanged)
    Q_PROPERTY(bool localGestureActive READ localGestureActive NOTIFY stateChanged)
    Q_PROPERTY(
        double localBrushSize READ localBrushSize WRITE setLocalBrushSize NOTIFY stateChanged)
    Q_PROPERTY(double localBrushOpacity READ localBrushOpacity WRITE setLocalBrushOpacity NOTIFY
                   stateChanged)
    Q_PROPERTY(
        bool localBrushErase READ localBrushErase WRITE setLocalBrushErase NOTIFY stateChanged)
    Q_PROPERTY(QVariantList localMasks READ localMasks NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap localState READ localState NOTIFY stateChanged)
    Q_PROPERTY(QVariantList localHandles READ localHandles NOTIFY stateChanged)
    Q_PROPERTY(bool localMode READ localMode WRITE setLocalMode NOTIFY stateChanged)
    Q_PROPERTY(bool localCanvasReady READ localCanvasReady NOTIFY previewChanged)
    Q_PROPERTY(bool localOverlay READ localOverlay WRITE setLocalOverlay NOTIFY stateChanged)
    Q_PROPERTY(QString localOverlayColor READ localOverlayColor WRITE setLocalOverlayColor NOTIFY
                   stateChanged)
    Q_PROPERTY(double localOverlayOpacity READ localOverlayOpacity WRITE setLocalOverlayOpacity
                   NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap transformState READ transformState NOTIFY stateChanged)
    Q_PROPERTY(QVariantList transformGuides READ transformGuides NOTIFY stateChanged)
    Q_PROPERTY(int transformGuideLayout READ transformGuideLayout NOTIFY stateChanged)
    Q_PROPERTY(bool guidedMode READ guidedMode WRITE setGuidedMode NOTIFY stateChanged)
    Q_PROPERTY(bool guidedCanvasReady READ guidedCanvasReady NOTIFY previewChanged)
    Q_PROPERTY(bool transformAutoBusy READ transformAutoBusy NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap lensState READ lensState NOTIFY stateChanged)
    Q_PROPERTY(QVariantList lensOptions READ lensOptions NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap cropState READ cropState NOTIFY stateChanged)
    Q_PROPERTY(bool cropCanvasReady READ cropCanvasReady NOTIFY previewChanged)
    Q_PROPERTY(bool cropMode READ cropMode WRITE setCropMode NOTIFY stateChanged)
    Q_PROPERTY(QString cropOverlay READ cropOverlay WRITE setCropOverlay NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap parameters READ parameters NOTIFY stateChanged)
    Q_PROPERTY(QString skyModelName READ skyModelName NOTIFY stateChanged)
    Q_PROPERTY(bool skyModelConfigured READ skyModelConfigured NOTIFY stateChanged)
    Q_PROPERTY(bool skyModelBundled READ skyModelBundled NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap correctionStates READ correctionStates NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap curvePoints READ curvePoints NOTIFY stateChanged)
    Q_PROPERTY(QString targetedTool READ targetedTool WRITE setTargetedTool NOTIFY stateChanged)
    Q_PROPERTY(bool editingLocked READ editingLocked NOTIFY stateChanged)
    Q_PROPERTY(bool presetPreview READ presetPreview NOTIFY stateChanged)
    Q_PROPERTY(QString detailDiagnostic READ detailDiagnostic NOTIFY stateChanged)
    Q_PROPERTY(bool customWhiteBalance READ customWhiteBalance NOTIFY stateChanged)
    Q_PROPERTY(QVariantList creativeParameters READ creativeParameters CONSTANT)
    Q_PROPERTY(bool creativeBusy READ creativeBusy NOTIFY stateChanged)
    Q_PROPERTY(bool pointColorPicking READ pointColorPicking WRITE setPointColorPicking NOTIFY
                   stateChanged)
    Q_PROPERTY(
        bool pointColorRange READ pointColorRange WRITE setPointColorRange NOTIFY stateChanged)
    Q_PROPERTY(bool pointColorCanvasReady READ pointColorCanvasReady NOTIFY previewChanged)
    Q_PROPERTY(bool whiteBalanceBusy READ whiteBalanceBusy NOTIFY stateChanged)
    Q_PROPERTY(bool whiteBalancePicking READ whiteBalancePicking WRITE setWhiteBalancePicking NOTIFY
                   stateChanged)
    Q_PROPERTY(bool whiteBalanceCanvasReady READ whiteBalanceCanvasReady NOTIFY previewChanged)
    Q_PROPERTY(QString whiteBalanceMessage READ whiteBalanceMessage NOTIFY stateChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY stateChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY stateChanged)
    Q_PROPERTY(bool hasPhoto READ hasPhoto NOTIFY stateChanged)
    Q_PROPERTY(bool original READ original WRITE setOriginal NOTIFY stateChanged)
    Q_PROPERTY(double exposure READ exposure WRITE setExposure NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString filename READ filename NOTIFY stateChanged)
    Q_PROPERTY(QString camera READ camera NOTIFY stateChanged)
    Q_PROPERTY(QString lens READ lens NOTIFY stateChanged)
    Q_PROPERTY(QString iso READ iso NOTIFY stateChanged)
    Q_PROPERTY(QString shutter READ shutter NOTIFY stateChanged)
    Q_PROPERTY(QString aperture READ aperture NOTIFY stateChanged)
    Q_PROPERTY(QString focalLength READ focalLength NOTIFY stateChanged)
    Q_PROPERTY(QString dimensions READ dimensions NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap decoderDiagnostics READ decoderDiagnostics NOTIFY stateChanged)
    Q_PROPERTY(QString decoderSummary READ decoderSummary NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString previewUrl READ previewUrl NOTIFY previewChanged)
    Q_PROPERTY(bool oneToOne READ oneToOne WRITE setOneToOne NOTIFY stateChanged)
    Q_PROPERTY(QString zoomMode READ zoomMode WRITE setZoomMode NOTIFY stateChanged)
    Q_PROPERTY(double zoomFactor READ zoomFactor WRITE setZoomFactor NOTIFY stateChanged)
    Q_PROPERTY(bool zoomAvailable READ zoomAvailable NOTIFY stateChanged)
    Q_PROPERTY(double minimumZoomFactor READ minimumZoomFactor CONSTANT)
    Q_PROPERTY(double maximumZoomFactor READ maximumZoomFactor CONSTANT)
    Q_PROPERTY(QSize previewPixelSize READ previewPixelSize NOTIFY stateChanged)
    Q_PROPERTY(
        int previewCacheMiB READ previewCacheMiB WRITE setPreviewCacheMiB NOTIFY stateChanged)
    Q_PROPERTY(bool metalPreview READ metalPreview WRITE setMetalPreview NOTIFY stateChanged)
    Q_PROPERTY(bool hdrToneMapping READ hdrToneMapping WRITE setHDRToneMapping NOTIFY stateChanged)
    Q_PROPERTY(bool aiBusy READ aiBusy NOTIFY stateChanged)
    Q_PROPERTY(bool aiObjectPicking READ aiObjectPicking NOTIFY stateChanged)
    Q_PROPERTY(QString aiPickKind READ aiPickKind NOTIFY stateChanged)
    Q_PROPERTY(double aiProgress READ aiProgress NOTIFY stateChanged)
    Q_PROPERTY(QString aiOperation READ aiOperation NOTIFY stateChanged)
    Q_PROPERTY(QString retouchMode READ retouchMode WRITE setRetouchMode NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap retouchState READ retouchState NOTIFY stateChanged)
    Q_PROPERTY(QVariantList retouchItems READ retouchItems NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap retouchOverlay READ retouchOverlay NOTIFY stateChanged)
    Q_PROPERTY(bool retouchBusy READ retouchBusy NOTIFY stateChanged)
    Q_PROPERTY(QString denoiseModelName READ denoiseModelName NOTIFY stateChanged)
    Q_PROPERTY(bool denoiseModelConfigured READ denoiseModelConfigured NOTIFY stateChanged)
    Q_PROPERTY(bool denoiseModelBundled READ denoiseModelBundled NOTIFY stateChanged)
    Q_PROPERTY(QString catalogPath READ catalogPath CONSTANT)
    Q_PROPERTY(QVariantList processingVersions READ processingVersions NOTIFY stateChanged)
    Q_PROPERTY(bool versionsBusy READ versionsBusy NOTIFY stateChanged)
    Q_PROPERTY(QString versionsError READ versionsError NOTIFY stateChanged)
    Q_PROPERTY(QString versionsStatus READ versionsStatus NOTIFY stateChanged)
    Q_PROPERTY(QString selectedVersionID READ selectedVersionID NOTIFY stateChanged)
    Q_PROPERTY(QVariantList processingHistory READ processingHistory NOTIFY stateChanged)
    Q_PROPERTY(QString historyRevision READ historyRevision NOTIFY stateChanged)
    Q_PROPERTY(QString historyError READ historyError NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap histogram READ histogram NOTIFY histogramChanged)
    Q_PROPERTY(bool histogramPending READ histogramPending NOTIFY histogramChanged)
    Q_PROPERTY(bool shadowClipping READ shadowClipping WRITE setShadowClipping NOTIFY stateChanged)
    Q_PROPERTY(bool highlightClipping READ highlightClipping WRITE setHighlightClipping NOTIFY
                   stateChanged)
    Q_PROPERTY(int previewClipping READ previewClipping NOTIFY stateChanged)

  public:
    EditorController(std::filesystem::path catalog, PreviewProvider *provider,
                     QObject *parent = nullptr);
    ~EditorController() override;
    bool busy() const {
        return busy_ || versionsBusy_;
    }
    bool saving() const {
        return pendingSave_ || pendingWrites_ > 0 || saveFailed_;
    }
    bool readyForUpdate() const;
    bool hasPhoto() const {
        return session_.has_value();
    }
    bool original() const {
        return original_;
    }
    double exposure() const {
        return history_.current().exposure;
    }
    double progress() const {
        return progress_;
    }
    QVariantList localMasks() const;
    QVariantList localComponents() const;
    bool localGestureActive() const {
        return localGesture_.has_value();
    }
    bool localSampling() const {
        return localSampling_;
    }
    double localBrushSize() const {
        return localBrushSize_;
    }
    double localBrushOpacity() const {
        return localBrushOpacity_;
    }
    bool localBrushErase() const {
        return localBrushErase_;
    }
    void setLocalBrushSize(double value);
    void setLocalBrushOpacity(double value);
    void setLocalBrushErase(bool value);
    Q_INVOKABLE QString addLocalComponent(const QString &shape, const QString &operation);
    Q_INVOKABLE void selectLocalComponent(const QString &id);
    Q_INVOKABLE void removeLocalComponent();
    Q_INVOKABLE void moveLocalComponent(int direction);
    Q_INVOKABLE void setLocalComponentEnabled(bool value);
    Q_INVOKABLE void setLocalComponentInverted(bool value);
    Q_INVOKABLE void setLocalComponentOperation(const QString &value);
    Q_INVOKABLE void clearLocalBrush();
    Q_INVOKABLE void pickLocalRange(double x, double y);
    Q_INVOKABLE QVariantList localBrushOutline(double x, double y) const;
    QVariantMap localState() const;
    QVariantList localHandles() const;
    bool localMode() const {
        return localMode_;
    }
    bool localCanvasReady() const {
        return localMode_ && localCanvasReady_;
    }
    bool localOverlay() const {
        return localOverlay_;
    }
    QString localOverlayColor() const {
        return localOverlayColor_;
    }
    double localOverlayOpacity() const {
        return localOverlayOpacity_;
    }
    void setLocalMode(bool value);
    void setLocalOverlay(bool value);
    void setLocalOverlayColor(const QString &value);
    void setLocalOverlayOpacity(double value);
    Q_INVOKABLE QString addLocalMask(const QString &shape);
    Q_INVOKABLE QString duplicateLocalMask();
    Q_INVOKABLE void selectLocalMask(const QString &id);
    Q_INVOKABLE void removeLocalMask();
    Q_INVOKABLE void moveLocalMask(int direction);
    Q_INVOKABLE void renameLocalMask(const QString &name);
    Q_INVOKABLE void setLocalEnabled(bool enabled);
    Q_INVOKABLE void setLocalInverted(bool inverted);
    Q_INVOKABLE void setLocalParameter(const QString &name, double value);
    Q_INVOKABLE bool beginLocalGesture(int handle, double x, double y, bool proxy = false);
    Q_INVOKABLE void adjustLocalGesture(double x, double y);
    QVariantMap transformState() const;
    QVariantList transformGuides() const;
    int transformGuideLayout() const {
        return guideLayout_;
    }
    bool guidedMode() const {
        return guidedMode_;
    }
    bool guidedCanvasReady() const {
        return guidedMode_ && guidedCanvasReady_;
    }
    bool transformAutoBusy() const {
        return transformAutoBusy_;
    }
    void setGuidedMode(bool enabled);
    Q_INVOKABLE void setGuideLayout(int layout);
    Q_INVOKABLE void moveGuidePoint(int line, int endpoint, double x, double y);
    Q_INVOKABLE void applyGuides();
    Q_INVOKABLE void autoTransform();
    Q_INVOKABLE void cancelAutoTransform();
    Q_INVOKABLE void resetTransform();
    QVariantMap lensState() const;
    QVariantList lensOptions() const;
    Q_INVOKABLE void setLensProfile(const QString &id);
    Q_INVOKABLE void resetLens();
    QVariantMap cropState() const;
    bool cropCanvasReady() const {
        return cropMode_ && cropCanvasReady_;
    }
    bool cropMode() const {
        return cropMode_;
    }
    QString cropOverlay() const {
        return cropOverlay_;
    }
    void setCropMode(bool value);
    void setCropOverlay(const QString &value);
    Q_INVOKABLE void setCropRect(double x, double y, double width, double height);
    Q_INVOKABLE void rotateCrop(int turns);
    Q_INVOKABLE void resetCrop();
    QVariantMap parameters() const;
    QVariantMap correctionStates() const;
    QVariantMap curvePoints() const;
    QString targetedTool() const {
        return targetedTool_;
    }
    void setTargetedTool(const QString &tool);
    Q_INVOKABLE int addCurvePoint(const QString &channel, double x, double y);
    Q_INVOKABLE void moveCurvePoint(const QString &channel, int index, double x, double y);
    Q_INVOKABLE void removeCurvePoint(const QString &channel, int index);
    Q_INVOKABLE void resetCurve(const QString &channel);
    Q_INVOKABLE QVariantList curveSamples(const QString &channel, bool parametric) const;
    Q_INVOKABLE bool beginTargeted(double x, double y);
    Q_INVOKABLE void adjustTargeted(double delta);
    Q_INVOKABLE void endTargeted();
    bool customWhiteBalance() const {
        return history_.current().customWhiteBalance;
    }
#ifdef LUMA_UX_HISTORY_READBACK
    // C++ readback only; never exposed to QML or the shipping target.
    bool uxHistoryGestureActive() const noexcept {
        return history_.gestureActive();
    }
#endif
    bool canUndo() const {
        return history_.canUndo();
    }
    bool canRedo() const {
        return history_.canRedo();
    }
    bool exporting() const {
        return exporting_;
    }
    double exportProgress() const {
        return exportProgress_;
    }
    QString filename() const;
    QString camera() const;
    QString lens() const;
    QString iso() const;
    QString shutter() const;
    QString aperture() const;
    QString focalLength() const;
    QString dimensions() const;
    QVariantMap decoderDiagnostics() const;
    QString decoderSummary() const;
    QString status() const {
        return status_.text();
    }
    QString error() const {
        return error_;
    }
    QString previewUrl() const;
    bool oneToOne() const {
        return zoomMode() == QStringLiteral("manual") && zoomFactor_ == 1;
    }
    void setOneToOne(bool value);
    QString zoomMode() const;
    void setZoomMode(const QString &mode);
    double zoomFactor() const {
        return zoomFactor_;
    }
    void setZoomFactor(double factor);
    bool zoomAvailable() const;
    static constexpr double minimumZoomFactor() {
        return 0.01;
    }
    static constexpr double maximumZoomFactor() {
        return 16;
    }
    QSize previewPixelSize() const;
    int previewCacheMiB() const {
        return previewCacheMiB_;
    }
    void setPreviewCacheMiB(int value);
    bool metalPreview() const {
        return metalPreview_;
    }
    void setMetalPreview(bool value);
    bool hdrToneMapping() const {
        return hdrToneMapping_;
    }
    void setHDRToneMapping(bool value);
    bool aiBusy() const {
        return aiBusy_;
    }
    bool aiObjectPicking() const {
        return aiObjectPicking_;
    }
    QString aiPickKind() const {
        return aiPickKind_;
    }
    double aiProgress() const {
        return aiProgress_;
    }
    QString aiOperation() const {
        return aiOperation_;
    }
    Q_INVOKABLE void mergeHDRPhotos(const QStringList &ids, double deghost = 50,
                                    bool alignFrames = true);
    Q_INVOKABLE void mergePanoramaPhotos(const QStringList &ids, int projection, double fieldOfView,
                                         bool boundaryWarp = false);
    Q_INVOKABLE void generateAIMask(const QString &kind, double x = .5, double y = .5);
    Q_INVOKABLE void beginAIObjectSelection();
    Q_INVOKABLE void beginAIMaskSelection(const QString &kind);
    Q_INVOKABLE void pickAIMask(double x, double y);
    Q_INVOKABLE void cancelAI();
    QString denoiseModelName() const;
    bool denoiseModelConfigured() const {
        return !denoiseModelPath_.isEmpty();
    }
    bool denoiseModelBundled() const;
    Q_INVOKABLE void setDenoiseModel(const QUrl &url);
    QString skyModelName() const;
    bool skyModelConfigured() const {
        return !skyModelPath_.isEmpty();
    }
    bool skyModelBundled() const;
    Q_INVOKABLE void setSkyModel(const QUrl &url);
    Q_INVOKABLE void denoisePhoto(double amount = 100);
    QString retouchMode() const {
        return retouchMode_;
    }
    void setRetouchMode(const QString &mode);
    bool retouchBusy() const {
        return retouchBusy_;
    }
    QVariantMap retouchState() const;
    QVariantList retouchItems() const;
    QVariantMap retouchOverlay() const;
    Q_INVOKABLE void selectRetouch(const QString &id);
    Q_INVOKABLE void removeRetouch();
    Q_INVOKABLE void setRetouchEnabled(bool value);
    Q_INVOKABLE void setRetouchControl(const QString &name, double value);
    Q_INVOKABLE void pickRetouchSource(double x, double y);
    Q_INVOKABLE void beginRetouch(double x, double y);
    Q_INVOKABLE void continueRetouch(double x, double y);
    Q_INVOKABLE void endRetouch();
    Q_INVOKABLE void cancelRetouch();
    Q_INVOKABLE void clearPreviewCache();
    std::array<luma::CacheStatistics, 3> cacheStatistics() const {
        return {service_.previewCacheStatistics(), service_.sourceCacheStatistics(),
                renderedCache_->statistics()};
    }
    QString catalogPath() const;
    QVariantList processingVersions() const {
        return processingVersions_;
    }
    bool versionsBusy() const {
        return versionsBusy_;
    }
    QString versionsError() const {
        return versionsError_;
    }
    QString versionsStatus() const {
        return versionsStatus_.text();
    }
    QString selectedVersionID() const {
        return selectedVersionID_;
    }
    Q_INVOKABLE void refreshVersions();
    Q_INVOKABLE void saveVersion(const QString &name);
    Q_INVOKABLE void restoreVersion(const QString &id, const QString &revision);
    Q_INVOKABLE void renameVersion(const QString &id, const QString &revision, const QString &name);
    Q_INVOKABLE void deleteVersion(const QString &id, const QString &revision);
    QVariantList processingHistory() const;
    QString historyRevision() const;
    QString historyError() const {
        return historyError_;
    }
    Q_INVOKABLE void prepareHistory();
    Q_INVOKABLE void jumpToHistory(const QString &id);
    Q_INVOKABLE void clearHistory(const QString &revision);
    Q_INVOKABLE void saveHistoryVersion(const QString &id, const QString &name);
    QVariantMap histogram() const {
        return histogram_;
    }
    bool histogramPending() const {
        return histogramPending_;
    }
    bool shadowClipping() const {
        return shadowClipping_;
    }
    bool highlightClipping() const {
        return highlightClipping_;
    }
    int previewClipping() const {
        return static_cast<int>(clippingPreview_);
    }
    void setShadowClipping(bool value);
    void setHighlightClipping(bool value);
    Q_INVOKABLE bool beginHistogramEdit(const QString &parameter);
    Q_INVOKABLE void updateHistogramEdit(double delta);
    Q_INVOKABLE void endHistogramEdit();
    QString sourcePath() const;
    bool presetPreview() const {
        return presetPreview_.has_value();
    }
    luma::EditDocument currentSettings() const {
        return history_.current();
    }
    QString detailDiagnostic() const;
    Q_INVOKABLE void setDetailControl(const QString &parameter);
    void applySettings(const luma::EditDocument &settings, std::uint32_t groups,
                       std::span<const std::string> parameters = {});
    void previewSettings(const luma::EditDocument &settings, std::uint32_t groups,
                         std::span<const std::string> parameters = {});
    void clearPreviewSettings();
    Q_INVOKABLE void applyQuickLook(const QString &recipe);
    Q_INVOKABLE void setQuickLookAmount(const QString &id, double amount);
    Q_INVOKABLE void setQuickLookEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void removeQuickLook(const QString &id);
    Q_INVOKABLE void moveQuickLook(const QString &id, int direction);
    Q_INVOKABLE void clearQuickLooks();
    Q_INVOKABLE void applyEnhance();
    Q_INVOKABLE void applyMatchLook(const QStringList &referenceIDs, double amount = 100,
                                    bool includeExposure = false, bool includeWhiteBalance = false);
    bool editingLocked() const {
        return editingLocked_;
    }
    bool canPrepareBatch() const;
    QString photoID() const;
    void prepareExternalEdits();
    void finishExternalEdits(const std::optional<luma::EditDocument> &edits);
    void restore();
    void setWindow(QQuickWindow *window);
    Q_INVOKABLE void openFile(const QUrl &url);
    Q_INVOKABLE bool closePhoto();
    Q_INVOKABLE void beginEdit();
    Q_INVOKABLE void endEdit();
    Q_INVOKABLE void setParameter(const QString &name, double value);
    Q_INVOKABLE void setCorrectionEnabled(const QString &name, bool enabled);
    Q_INVOKABLE void setWhiteBalancePreset(const QString &name);
    bool whiteBalanceBusy() const {
        return whiteBalanceBusy_;
    }
    bool whiteBalancePicking() const {
        return whiteBalancePicking_;
    }
    bool whiteBalanceCanvasReady() const {
        return whiteBalancePicking_ && whiteBalanceCanvasReady_ && !whiteBalanceBusy_ && !busy();
    }
    QString whiteBalanceMessage() const {
        return whiteBalanceMessage_.text();
    }
    void setWhiteBalancePicking(bool enabled);
    QVariantList creativeParameters() const;
    bool creativeBusy() const {
        return creativeBusy_;
    }
    bool pointColorPicking() const {
        return pointColorPicking_;
    }
    bool pointColorRange() const {
        return pointColorRange_;
    }
    bool pointColorCanvasReady() const {
        return pointColorPicking_ && pointColorCanvasReady_ && !creativeBusy_ && !busy();
    }
    void setPointColorPicking(bool value);
    void setPointColorRange(bool value);
    QVariantMap comparison() const;
    Q_INVOKABLE void prepareCompare(const QStringList &ids, bool fullResolution = false);
    Q_INVOKABLE void closeCompare();
    Q_INVOKABLE void autoAdjust();
    Q_INVOKABLE void samplePointColor(double x, double y);
    Q_INVOKABLE void cancelCreative();
    Q_INVOKABLE void autoWhiteBalance();
    Q_INVOKABLE void sampleWhiteBalance(double x, double y);
    Q_INVOKABLE void cancelWhiteBalance();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void resetEdits();
    Q_INVOKABLE void exportPhoto(const QUrl &destination, int format, int quality, bool overwrite);
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE void setExposure(double value);
    Q_INVOKABLE void setOriginal(bool value);
    Q_INVOKABLE bool requestClose();
    Q_INVOKABLE void retrySave();
    Q_INVOKABLE void cancelLoad();
  signals:
    void progressChanged();
    void histogramChanged();
    void stateChanged();
    void previewChanged();
    void photoLoaded(const QString &id);
    void editsSaved(const QString &id);
    void closeReady();

  private:
    mutable std::optional<QVariantMap> parametersCache_;
    mutable std::array<std::uint64_t, 3> parametersCacheRevision_{};
    enum class VersionAction { list, create, restore, rename, remove };
    QVariantList processingVersions_;
    bool versionsBusy_ = false;
    QString versionsError_, selectedVersionID_;
    LocalizedText versionsStatus_;
    void runVersionAction(VersionAction action, const QString &id = {},
                          const QString &revision = {}, const QString &name = {},
                          std::optional<luma::EditDocument> snapshot = std::nullopt);
    void finishHistoryTools();
    QString historyHandle(std::uint64_t id) const;
    std::optional<std::uint64_t> resolveHistoryHandle(const QString &id) const;
    std::uint64_t historyGeneration_ = 0;
    QString historyError_;
    QVariantMap histogram_;
    bool histogramPending_ = false, shadowClipping_ = false, highlightClipping_ = false;
    luma::ClippingPreview clippingPreview_ = luma::ClippingPreview::none;
    void publishHistogram(const std::optional<luma::ImageHistogram> &histogram);
    struct HistogramGesture {
        QString parameter;
        double initial;
        std::uint64_t revision;
    };
    std::optional<HistogramGesture> histogramGesture_;
    void runMerge(const QString &operation,
                  std::function<std::filesystem::path(luma::CancellationToken,
                                                      const std::function<void(double)> &)>
                      work);
    bool localMode_ = false, localCanvasReady_ = false, localOverlay_ = true;
    QString localSelected_, localOverlayColor_ = QStringLiteral("#ff2666");
    double localOverlayOpacity_ = 0.45;
    QString localComponentSelected_;
    double localBrushSize_ = 8, localBrushOpacity_ = 100;
    bool localBrushErase_ = false, localSampling_ = false;
    luma::JobHandle localSampleJob_;
    std::uint64_t localSampleRevision_ = 0;
    void cancelLocalSample();
    int localComponentIndex(const luma::LocalMask &mask) const;
    luma::MaskGeometry &localGeometry(luma::LocalMask &mask) const;
    const luma::MaskGeometry &localGeometry(const luma::LocalMask &mask) const;
    static QString localShapeKey(luma::MaskShape shape);
    static luma::MaskGeometry makeLocalGeometry(const QString &shape);
    struct LocalGesture {
        luma::LocalMask base;
        luma::CropGeometry geometry;
        int handle;
        std::array<double, 2> anchor;
        std::optional<luma::BrushStroke> stroke;
        bool proxy = false;
        std::array<double, 2> inputAnchor{};
        std::array<double, 2> outputAnchor{};
    };
    std::optional<LocalGesture> localGesture_;
    int localIndex() const;
    luma::MaskOverlay localRenderOverlay() const;
    std::filesystem::path catalog_;
    luma::SessionService service_;
    std::shared_ptr<luma::ByteLruCache<std::string, RenderedPreview>> renderedCache_ =
        std::make_shared<luma::ByteLruCache<std::string, RenderedPreview>>(64ULL * 1024 * 1024);
    std::unique_ptr<luma::macos::MemoryPressureMonitor> memoryPressure_;
    luma::macos::MemoryPressure pressure_ = luma::macos::MemoryPressure::normal;
    int previewCacheMiB_ = 256;
    enum class PreviewZoom : std::uint8_t { fit, fill, manual };
    PreviewZoom zoomMode_ = PreviewZoom::fit;
    double zoomFactor_ = 1;
    bool zoomToolsCompatible() const;
    luma::EditDocument previewDocument() const;
    void applyPreviewZoom(PreviewZoom mode, double factor);
    void resetPreviewZoom();
    bool metalPreview_ = true;
    bool hdrToneMapping_ = true;
    std::shared_ptr<luma::DevelopAccelerator> accelerator_;
    std::shared_ptr<luma::ai::ISegmentationProvider> segmentation_;
    luma::JobManager aiJobs_;
    luma::JobHandle aiJob_;
    bool aiBusy_ = false, aiObjectPicking_ = false;
    double aiProgress_ = 0;
    std::uint64_t aiRevision_ = 0;
    QString denoiseModelPath_;
    QString skyModelPath_;
    QString aiOperation_, aiPickKind_;
    luma::JobManager retouchJobs_;
    luma::JobHandle retouchJob_;
    std::uint64_t retouchRevision_ = 0;
    bool retouchBusy_ = false;
    QString retouchMode_ = QStringLiteral("off"), retouchSelected_;
    double retouchRadius_ = .025, retouchFeather_ = .75, retouchOpacity_ = 1;
    std::optional<std::array<double, 2>> retouchSource_;
    std::optional<luma::RetouchStroke> retouchDraft_;
    std::optional<std::array<double, 2>> retouchPoint(double x, double y) const;
    int retouchIndex() const;
    PreviewProvider *provider_;
    luma::JobManager ioJobs_;
    luma::JobManager renderJobs_;
    luma::JobManager compareJobs_;
    luma::JobHandle compareJob_;
    std::uint64_t compareRevision_ = 0, compareImageRevision_ = 0;
    bool compareBusy_ = false, compareReady_ = false, compareFull_ = false;
    QString compareError_;
    QStringList compareIDs_, compareNames_;
    luma::JobManager creativeJobs_;
    luma::JobHandle creativeJob_;
    std::uint64_t creativeRevision_ = 0;
    bool creativeBusy_ = false, pointColorPicking_ = false, pointColorCanvasReady_ = false,
         pointColorRange_ = false;
    void runCreative(std::optional<std::array<double, 2>> point);
    luma::JobManager whiteBalanceJobs_;
    luma::JobHandle whiteBalanceJob_;
    std::uint64_t whiteBalanceRevision_ = 0;
    bool whiteBalanceBusy_ = false, whiteBalancePicking_ = false, whiteBalanceCanvasReady_ = false;
    LocalizedText whiteBalanceMessage_;
    void runWhiteBalance(std::optional<std::array<double, 2>> point);
    luma::JobManager transformJobs_;
    luma::JobHandle transformJob_;
    bool guidedMode_ = false, guidedCanvasReady_ = false, transformAutoBusy_ = false;
    std::uint64_t transformAutoRevision_ = 0;
    std::vector<luma::PerspectiveGuide> guides_;
    int guideLayout_ = 0;
    luma::JobHandle loadJob_;
    luma::CancellationSource loadCancellation_;
    luma::JobHandle exportJob_;
    luma::CancellationSource exportCancellation_;
    luma::JobHandle renderJob_;
    std::optional<luma::PhotoSession> session_;
    QTimer saveTimer_;
    QTimer refineTimer_;
    QQuickWindow *window_ = nullptr;
    std::vector<std::uint8_t> displayProfile_;
    bool busy_ = false;
    bool editingLocked_ = false;
    bool cropMode_ = false;
    bool cropCanvasReady_ = false;
    QString cropOverlay_ = QStringLiteral("thirds");
    bool original_ = false;
    bool pendingSave_ = false;
    bool saveFailed_ = false;
    bool closing_ = false;
    int pendingWrites_ = 0;
    luma::EditHistory history_;
    std::optional<luma::EditDocument> presetPreview_;
    QString pendingQuickLook_;
    QString detailControl_;
    bool optionHeld_ = false;
    luma::DetailDiagnostic diagnostic_ = luma::DetailDiagnostic::none;
    bool exporting_ = false;
    double exportProgress_ = 0;
    double progress_ = 0;
    std::uint64_t loadRevision_ = 0;
    std::uint64_t renderRevision_ = 0;
    std::uint64_t imageRevision_ = 0;
    bool previewReady_ = false;
    std::uint64_t editRevision_ = 0;
    QUrl deferredOpen_;
    LocalizedText status_ = QStringLiteral("Ready");
    QString error_;
    QString targetedTool_ = QStringLiteral("none");
    struct TargetGesture {
        luma::EditDocument base;
        std::size_t channel = 0;
        double sample = 0;
        std::array<double, 8> weights{};
        QString parameter;
    };
    std::optional<TargetGesture> targeted_;
    void applyAdvanced(luma::EditDocument next);
    void validateGeometry(const luma::EditDocument &edits) const;
    void closeTransformTool();
    void applyTransform(luma::TransformSettings settings);
    void advancedFailure(const std::exception &failure);
    void load(std::optional<std::filesystem::path> source);
    void render(bool fullQuality = false);
    void applyCacheBudget();
    void handleMemoryPressure(luma::macos::MemoryPressure pressure);
    void editsChanged(bool renderPreview = true);
    bool editable() const;
    void flushSave();
    void checkClose();
    void refreshDisplayProfile();
    bool eventFilter(QObject *watched, QEvent *event) override;
    void refreshDetailDiagnostic();
    void clearDetailDiagnostic();
};
