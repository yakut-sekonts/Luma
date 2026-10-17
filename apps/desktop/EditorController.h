#pragma once
#include "apps/desktop/LocalizedText.h"
#include "apps/desktop/PreviewProvider.h"
#include "apps/shared/SessionService.h"
#include "core/edits/EditHistory.h"
#include "core/jobs/JobManager.h"
#include "core/render/CropGeometry.h"
#include "core/render/LocalAdjustments.h"
#include "core/render/PerspectiveCorrection.h"
#include "core/render/Sharpening.h"
#include <QObject>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

class EditorController final : public QObject {
    Q_OBJECT
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
    Q_PROPERTY(bool localCanvasReady READ localCanvasReady NOTIFY stateChanged)
    Q_PROPERTY(bool localOverlay READ localOverlay WRITE setLocalOverlay NOTIFY stateChanged)
    Q_PROPERTY(QString localOverlayColor READ localOverlayColor WRITE setLocalOverlayColor NOTIFY
                   stateChanged)
    Q_PROPERTY(double localOverlayOpacity READ localOverlayOpacity WRITE setLocalOverlayOpacity
                   NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap transformState READ transformState NOTIFY stateChanged)
    Q_PROPERTY(QVariantList transformGuides READ transformGuides NOTIFY stateChanged)
    Q_PROPERTY(int transformGuideLayout READ transformGuideLayout NOTIFY stateChanged)
    Q_PROPERTY(bool guidedMode READ guidedMode WRITE setGuidedMode NOTIFY stateChanged)
    Q_PROPERTY(bool guidedCanvasReady READ guidedCanvasReady NOTIFY stateChanged)
    Q_PROPERTY(bool transformAutoBusy READ transformAutoBusy NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap lensState READ lensState NOTIFY stateChanged)
    Q_PROPERTY(QVariantList lensOptions READ lensOptions NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap cropState READ cropState NOTIFY stateChanged)
    Q_PROPERTY(bool cropCanvasReady READ cropCanvasReady NOTIFY stateChanged)
    Q_PROPERTY(bool cropMode READ cropMode WRITE setCropMode NOTIFY stateChanged)
    Q_PROPERTY(QString cropOverlay READ cropOverlay WRITE setCropOverlay NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap parameters READ parameters NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap correctionStates READ correctionStates NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap curvePoints READ curvePoints NOTIFY stateChanged)
    Q_PROPERTY(QString targetedTool READ targetedTool WRITE setTargetedTool NOTIFY stateChanged)
    Q_PROPERTY(bool editingLocked READ editingLocked NOTIFY stateChanged)
    Q_PROPERTY(bool presetPreview READ presetPreview NOTIFY stateChanged)
    Q_PROPERTY(QString detailDiagnostic READ detailDiagnostic NOTIFY stateChanged)
    Q_PROPERTY(bool customWhiteBalance READ customWhiteBalance NOTIFY stateChanged)
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
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString previewUrl READ previewUrl NOTIFY stateChanged)
    Q_PROPERTY(QString catalogPath READ catalogPath CONSTANT)

  public:
    EditorController(std::filesystem::path catalog, PreviewProvider *provider,
                     QObject *parent = nullptr);
    ~EditorController() override;
    bool busy() const {
        return busy_;
    }
    bool saving() const {
        return pendingSave_ || pendingWrites_ > 0 || saveFailed_;
    }
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
    Q_INVOKABLE void selectLocalMask(const QString &id);
    Q_INVOKABLE void removeLocalMask();
    Q_INVOKABLE void moveLocalMask(int direction);
    Q_INVOKABLE void renameLocalMask(const QString &name);
    Q_INVOKABLE void setLocalEnabled(bool enabled);
    Q_INVOKABLE void setLocalInverted(bool inverted);
    Q_INVOKABLE void setLocalParameter(const QString &name, double value);
    Q_INVOKABLE bool beginLocalGesture(int handle, double x, double y);
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
    QString status() const {
        return status_.text();
    }
    QString error() const {
        return error_;
    }
    QString previewUrl() const;
    QString catalogPath() const;
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
    Q_INVOKABLE void beginEdit();
    Q_INVOKABLE void endEdit();
    Q_INVOKABLE void setParameter(const QString &name, double value);
    Q_INVOKABLE void setCorrectionEnabled(const QString &name, bool enabled);
    Q_INVOKABLE void setWhiteBalancePreset(const QString &name);
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
    void stateChanged();
    void photoLoaded(const QString &id);
    void editsSaved(const QString &id);
    void closeReady();

  private:
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
    };
    std::optional<LocalGesture> localGesture_;
    int localIndex() const;
    luma::MaskOverlay localRenderOverlay() const;
    std::filesystem::path catalog_;
    luma::SessionService service_;
    PreviewProvider *provider_;
    luma::JobManager ioJobs_;
    luma::JobManager renderJobs_;
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
    void render();
    void editsChanged(bool renderPreview = true);
    bool editable() const;
    void flushSave();
    void checkClose();
    void refreshDisplayProfile();
    bool eventFilter(QObject *watched, QEvent *event) override;
    void refreshDetailDiagnostic();
    void clearDetailDiagnostic();
};
