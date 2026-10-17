#include "apps/desktop/EditorController.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/DisplayProfile.h"
#include <QGuiApplication>
#include <QMetaObject>
#include <QPointer>
#include <QScreen>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
QString display(std::optional<double> value, const QString &suffix = {}) {
    return value ? QString::number(*value, 'g', 4) + suffix : QStringLiteral("—");
}
} // namespace

EditorController::EditorController(std::filesystem::path catalog, PreviewProvider *provider,
                                   QObject *parent)
    : QObject(parent), catalog_(std::move(catalog)), service_(catalog_), provider_(provider) {
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(180);
    connect(&saveTimer_, &QTimer::timeout, this, &EditorController::flushSave);
    QGuiApplication::instance()->installEventFilter(this);
}
EditorController::~EditorController() {
    QGuiApplication::instance()->removeEventFilter(this);
    exportCancellation_.cancel();
    loadCancellation_.cancel();
    transformJob_.cancel();
    transformJobs_.shutdown();
    ioJobs_.shutdown();
    renderJobs_.shutdown();
}
QString EditorController::filename() const {
    return session_ ? QString::fromStdString(session_->asset.path.filename().string()) : QString();
}
QString EditorController::camera() const {
    return session_ ? QString::fromStdString(session_->asset.metadata.camera) : QStringLiteral("—");
}
QString EditorController::lens() const {
    return session_ && !session_->asset.metadata.lens.empty()
               ? QString::fromStdString(session_->asset.metadata.lens)
               : QCoreApplication::translate("Luma", "Not specified");
}
QString EditorController::iso() const {
    return session_ ? display(session_->asset.metadata.iso) : QStringLiteral("—");
}
QString EditorController::shutter() const {
    if (!session_ || !session_->asset.metadata.shutterSeconds) {
        return QStringLiteral("—");
    }
    const double seconds = *session_->asset.metadata.shutterSeconds;
    return seconds < 1 ? QStringLiteral("1/%1 s").arg(std::round(1 / seconds))
                       : display(seconds, QStringLiteral(" s"));
}
QString EditorController::aperture() const {
    return session_ ? QStringLiteral("f/") + display(session_->asset.metadata.aperture)
                    : QStringLiteral("—");
}
QString EditorController::focalLength() const {
    return session_ ? display(session_->asset.metadata.focalLength, QStringLiteral(" mm"))
                    : QStringLiteral("—");
}
QString EditorController::dimensions() const {
    if (!session_) {
        return {};
    }
    const auto &m = session_->asset.metadata;
    return QStringLiteral("%1 × %2")
        .arg(m.orientation & 4 ? m.height : m.width)
        .arg(m.orientation & 4 ? m.width : m.height);
}
QString EditorController::catalogPath() const {
    return QString::fromStdString(catalog_.string());
}
QString EditorController::sourcePath() const {
    return session_ ? QString::fromStdString(session_->asset.path.string()) : QString();
}
QString EditorController::previewUrl() const {
    return previewReady_ ? QStringLiteral("image://preview/current?v=%1").arg(imageRevision_)
                         : QString();
}

void EditorController::setWindow(QQuickWindow *window) {
    window_ = window;
    connect(window, &QQuickWindow::screenChanged, this, [this] { refreshDisplayProfile(); });
    refreshDisplayProfile();
}
void EditorController::refreshDisplayProfile() {
    displayProfile_ = window_ && QGuiApplication::platformName() == QStringLiteral("cocoa")
                          ? luma::macos::displayProfile(window_->winId())
                          : std::vector<std::uint8_t>{};
    if (session_) {
        render();
    }
}
void EditorController::restore() {
    load(std::nullopt);
}
void EditorController::openFile(const QUrl &url) {
    if (!url.isLocalFile() || busy_ || closing_ || editingLocked_) {
        return;
    }
    if (saving()) {
        deferredOpen_ = url;
        if (saveFailed_) {
            retrySave();
        } else {
            flushSave();
        }
        return;
    }
    load(std::filesystem::path(url.toLocalFile().toStdString()));
}
void EditorController::load(std::optional<std::filesystem::path> source) {
    closeTransformTool();
    guides_.clear();
    localMode_ = false;
    localSelected_.clear();
    localComponentSelected_.clear();
    localGesture_.reset();
    cropMode_ = false;
    clearDetailDiagnostic();
    endEdit();
    loadCancellation_.cancel();
    loadCancellation_ = luma::CancellationSource{};
    const auto stop = loadCancellation_.token();
    renderJob_.cancel();
    const auto revision = ++loadRevision_;
    ++renderRevision_;
    busy_ = true;
    progress_ = 0;
    emit progressChanged();
    error_.clear();
    status_ = source ? QStringLiteral("Decoding image…") : QStringLiteral("Restoring session…");
    emit stateChanged();
    const QPointer<EditorController> self(this);
    loadJob_ = ioJobs_.submit(luma::JobPriority::background, [this, self, source, revision, stop](
                                                                 const luma::JobContext &context) {
        try {
            const auto progress =
                [self, revision, context,
                 last = std::chrono::steady_clock::time_point{}](double value) mutable {
                    context.setProgress(value);
                    if (!std::isfinite(value))
                        return;
                    const auto now = std::chrono::steady_clock::now();
                    if (value < 1 && now - last < std::chrono::milliseconds(50))
                        return;
                    last = now;
                    value = std::clamp(value, 0.0, 1.0);
                    if (self) {
                        QMetaObject::invokeMethod(
                            self,
                            [self, revision, value] {
                                if (self && self->loadRevision_ == revision) {
                                    self->progress_ = value;
                                    emit self->progressChanged();
                                }
                            },
                            Qt::QueuedConnection);
                    }
                };
            auto result =
                source ? std::optional<luma::PhotoSession>(service_.open(*source, stop, progress))
                       : service_.restore(stop, progress);
            if (self) {
                QMetaObject::invokeMethod(
                    self,
                    [self, revision, result = std::move(result)]() mutable {
                        if (!self || self->loadRevision_ != revision) {
                            return;
                        }
                        self->busy_ = false;
                        if (result) {
                            self->session_ = std::move(result);
                            self->previewReady_ = false;
                            self->history_ = luma::EditHistory(self->session_->asset.edits);
                            self->original_ = false;
                            self->render();
                            emit self->photoLoaded(
                                QString::fromStdString(self->session_->asset.id));
                        }
                        self->status_ = self->session_
                                            ? QStringLiteral("Session restored · edits saved")
                                            : QStringLiteral("Open a photo to begin");
                        emit self->stateChanged();
                        self->checkClose();
                    },
                    Qt::QueuedConnection);
            }
        } catch (const std::exception &error) {
            const auto message = QString::fromUtf8(error.what());
            const bool cancelled = stop.isCancelled();
            if (self) {
                QMetaObject::invokeMethod(
                    self,
                    [self, revision, message, cancelled] {
                        if (!self || self->loadRevision_ != revision) {
                            return;
                        }
                        self->busy_ = false;
                        if (!cancelled) {
                            self->error_ = message;
                        }
                        self->status_ =
                            cancelled
                                ? QStringLiteral("Operation cancelled")
                                : QStringLiteral(
                                      "Could not open image; catalog and edits are preserved");
                        if (self->session_) {
                            self->render();
                        }
                        emit self->stateChanged();
                        self->checkClose();
                    },
                    Qt::QueuedConnection);
            }
        }
    });
}
bool EditorController::editable() const {
    return session_ && !busy_ && !closing_ && !editingLocked_;
}
QVariantMap EditorController::parameters() const {
    QVariantMap values;
    for (const auto *name : {"exposure",
                             "temperature",
                             "tint",
                             "contrast",
                             "highlights",
                             "shadows",
                             "whites",
                             "blacks",
                             "vibrance",
                             "saturation",
                             "curve.shadows",
                             "curve.darks",
                             "curve.lights",
                             "curve.highlights",
                             "sharpen.amount",
                             "sharpen.radius",
                             "sharpen.detail",
                             "sharpen.masking",
                             "noise.luminance",
                             "noise.luminanceDetail",
                             "noise.luminanceContrast",
                             "noise.color",
                             "noise.colorDetail",
                             "noise.colorSmoothness"})
        values.insert(QString::fromLatin1(name), history_.current().parameter(name));
    for (const auto band : luma::hslBands) {
        for (const auto *parameter : {"hue", "saturation", "luminance"}) {
            const auto name = "hsl." + std::string(band) + "." + parameter;
            values.insert(QString::fromStdString(name), history_.current().parameter(name));
        }
    }
    for (const auto *name :
         {"crop.x", "crop.y", "crop.width", "crop.height", "crop.angle", "crop.aspect",
          "crop.quarterTurns", "crop.flipHorizontal", "crop.flipVertical", "lens.distortion",
          "lens.vignetting", "lens.chromaticAberration"})
        values.insert(QString::fromLatin1(name), history_.current().parameter(name));
    for (auto name : luma::transformParameters)
        values.insert(QString::fromUtf8(name), history_.current().parameter(name));
    return values;
}
QVariantMap EditorController::correctionStates() const {
    QVariantMap values;
    for (const auto *name :
         {"whiteBalance", "exposure", "contrast", "highlights", "shadows", "whites", "blacks",
          "vibrance", "saturation", "curves", "hsl", "sharpening", "luminanceNoise", "colorNoise",
          "crop", "lens", "transform"})
        values.insert(QString::fromLatin1(name),
                      history_.current().enabled(luma::EditDocument::correction(name)));
    return values;
}
void EditorController::beginEdit() {
    clearDetailDiagnostic();
    clearPreviewSettings();
    if (editable())
        history_.begin();
}
void EditorController::endEdit() {
    cancelLocalSample();
    clearDetailDiagnostic();
    clearPreviewSettings();
    targeted_.reset();
    localGesture_.reset();
    history_.end();
    emit stateChanged();
}
void EditorController::editsChanged(bool renderPreview) {
    cancelLocalSample();
    cancelAutoTransform();
    presetPreview_.reset();
    ++editRevision_;
    pendingSave_ = true;
    saveTimer_.start();
    status_ = QStringLiteral("Saving edit…");
    refreshDetailDiagnostic();
    emit stateChanged();
    if (renderPreview)
        render();
}
void EditorController::setParameter(const QString &name, double value) {
    clearPreviewSettings();
    if (!editable())
        return;
    try {
        auto next = history_.current();
        next.setParameter(name.toStdString(), value);
        validateGeometry(next);
        if (name.startsWith(QStringLiteral("transform.")))
            error_.clear();
        if (next == history_.current())
            return;
        history_.apply(next);
        editsChanged();
    } catch (const std::exception &failure) {
        error_ = QString::fromUtf8(failure.what());
        emit stateChanged();
    }
}
void EditorController::setExposure(double value) {
    setParameter(QStringLiteral("exposure"), value);
}
void EditorController::setCorrectionEnabled(const QString &name, bool enabled) {
    clearPreviewSettings();
    if (!editable())
        return;
    try {
        auto next = history_.current();
        next.setEnabled(luma::EditDocument::correction(name.toStdString()), enabled);
        validateGeometry(next);
        if (name == QStringLiteral("transform") && !enabled)
            closeTransformTool();
        if (name == QStringLiteral("crop") && !enabled)
            cropMode_ = false;
        if (next == history_.current())
            return;
        history_.apply(next);
        editsChanged();
    } catch (const std::exception &failure) {
        error_ = QString::fromUtf8(failure.what());
        emit stateChanged();
    }
}
void EditorController::setWhiteBalancePreset(const QString &name) {
    clearPreviewSettings();
    if (!editable())
        return;
    auto next = history_.current();
    next.customWhiteBalance = name != QStringLiteral("As Shot");
    if (name == QStringLiteral("Daylight"))
        next.temperature = 5500;
    else if (name == QStringLiteral("Cloudy"))
        next.temperature = 6500;
    else if (name == QStringLiteral("Shade"))
        next.temperature = 7500;
    else if (name == QStringLiteral("Tungsten"))
        next.temperature = 2850;
    else if (name == QStringLiteral("Fluorescent"))
        next.temperature = 4000;
    else if (name != QStringLiteral("Custom") && name != QStringLiteral("As Shot"))
        return;
    if (name != QStringLiteral("Custom"))
        next.tint = 0;
    if (next == history_.current())
        return;
    history_.apply(next);
    editsChanged();
}
void EditorController::undo() {
    clearDetailDiagnostic();
    clearPreviewSettings();
    if (editable() && history_.canUndo()) {
        localGesture_.reset();
        targeted_.reset();
        history_.undo();
        editsChanged();
    }
}
void EditorController::redo() {
    clearDetailDiagnostic();
    clearPreviewSettings();
    if (editable() && history_.canRedo()) {
        localGesture_.reset();
        targeted_.reset();
        history_.redo();
        editsChanged();
    }
}
void EditorController::resetEdits() {
    setLocalMode(false);
    clearDetailDiagnostic();
    clearPreviewSettings();
    if (editable() && history_.current() != luma::EditDocument{}) {
        history_.end();
        history_.apply({});
        editsChanged();
    }
}
void EditorController::setOriginal(bool value) {
    if (busy_)
        return;
    setLocalMode(false);
    closeTransformTool();
    if (original_ == value || !session_) {
        return;
    }
    endEdit();
    original_ = value;
    if (value)
        cropMode_ = false;
    emit stateChanged();
    render();
}
void EditorController::render() {
    if (!session_) {
        return;
    }
    renderJob_.cancel();
    const auto revision = ++renderRevision_;
    if (cropMode_ && cropCanvasReady_) {
        cropCanvasReady_ = false;
        emit stateChanged();
    }
    if (guidedMode_ && guidedCanvasReady_) {
        guidedCanvasReady_ = false;
        emit stateChanged();
    }
    if (localCanvasReady_) {
        localCanvasReady_ = false;
        emit stateChanged();
    }
    const auto overlay = localRenderOverlay();
    const auto image = session_->preview;
    const auto metadata = session_->asset.metadata;
    const int orientation = metadata.orientation;
    auto edits = original_ ? luma::EditDocument{} : presetPreview_.value_or(history_.current());
    if (cropMode_ && edits.enabled(luma::Correction::crop)) {
        edits.crop.x = 0;
        edits.crop.y = 0;
        edits.crop.width = 1;
        edits.crop.height = 1;
        edits.crop.aspect = 0;
    }
    if (guidedMode_) {
        edits.setEnabled(luma::Correction::transform, false);
        edits.setEnabled(luma::Correction::crop, false);
    }
    const auto calibration = session_->calibration;
    const auto profile = displayProfile_;
    const auto diagnostic = diagnostic_;
    const auto detailScale = session_->previewScale;
    const QPointer<EditorController> self(this);
    renderJob_ = renderJobs_.submit(
        luma::JobPriority::interactive,
        [self, image, orientation, edits, calibration, profile, revision, diagnostic, detailScale,
         metadata, overlay](const luma::JobContext &context) {
            try {
                const auto pixels =
                    luma::renderCPU(*image, edits, orientation, profile, context.stopToken(),
                                    calibration, diagnostic, detailScale, metadata, overlay);
                QImage output(pixels.rgba.data(), pixels.width, pixels.height, pixels.width * 4,
                              QImage::Format_RGBA8888);
                auto owned = output.copy();
                if (self) {
                    QMetaObject::invokeMethod(
                        self,
                        [self, revision, owned = std::move(owned)]() mutable {
                            if (!self || self->renderRevision_ != revision) {
                                return;
                            }
                            self->provider_->update(std::move(owned));
                            ++self->imageRevision_;
                            self->previewReady_ = true;
                            self->cropCanvasReady_ = self->cropMode_;
                            self->guidedCanvasReady_ = self->guidedMode_;
                            self->localCanvasReady_ = self->localMode_;
                            emit self->stateChanged();
                        },
                        Qt::QueuedConnection);
                }
            } catch (const std::exception &error) {
                if (context.stopToken().isCancelled()) {
                    return;
                }
                const auto message = QString::fromUtf8(error.what());
                if (self) {
                    QMetaObject::invokeMethod(
                        self,
                        [self, revision, message] {
                            if (self && self->renderRevision_ == revision) {
                                self->error_ = message;
                                emit self->stateChanged();
                            }
                        },
                        Qt::QueuedConnection);
                }
            }
        });
}
void EditorController::flushSave() {
    saveTimer_.stop();
    if (!pendingSave_ || !session_) {
        checkClose();
        return;
    }
    pendingSave_ = false;
    ++pendingWrites_;
    const auto id = session_->asset.id;
    const auto edits = history_.current();
    const auto revision = editRevision_;
    const QPointer<EditorController> self(this);
    emit stateChanged();
    ioJobs_.submit(luma::JobPriority::background, [this, self, id, edits,
                                                   revision](const luma::JobContext &) {
        QString error;
        try {
            service_.save(id, edits);
        } catch (const std::exception &failure) {
            error = QString::fromUtf8(failure.what());
        }
        if (self) {
            QMetaObject::invokeMethod(
                self,
                [self, id, edits, revision, error] {
                    if (!self) {
                        return;
                    }
                    --self->pendingWrites_;
                    if (!error.isEmpty()) {
                        self->saveFailed_ = true;
                        self->error_ = error;
                        self->status_ = QStringLiteral("The edit is not saved yet. Retry saving.");
                        self->closing_ = false;
                    } else if (self->editRevision_ == revision) {
                        self->saveFailed_ = false;
                        self->session_->asset.edits = edits;
                        self->status_ = QStringLiteral("Edits saved automatically");
                    }
                    if (error.isEmpty())
                        emit self->editsSaved(QString::fromStdString(id));
                    emit self->stateChanged();
                    if (!self->saving() && !self->deferredOpen_.isEmpty()) {
                        const auto url = self->deferredOpen_;
                        self->deferredOpen_ = QUrl();
                        self->openFile(url);
                    }
                    self->checkClose();
                },
                Qt::QueuedConnection);
        }
    });
}
void EditorController::retrySave() {
    if (!session_ || pendingWrites_ > 0) {
        return;
    }
    pendingSave_ = true;
    saveFailed_ = false;
    error_.clear();
    flushSave();
}
void EditorController::cancelLoad() {
    loadCancellation_.cancel();
}
bool EditorController::requestClose() {
    closeTransformTool();
    if (editingLocked_)
        return false;
    if (!busy_ && !saving() && !exporting_) {
        return true;
    }
    if (saveFailed_) {
        error_ = QStringLiteral("The last edit is not saved. Retry saving before closing.");
        emit stateChanged();
        return false;
    }
    closing_ = true;
    cancelExport();
    loadCancellation_.cancel();
    renderJob_.cancel();
    flushSave();
    return false;
}
void EditorController::checkClose() {
    if (closing_ && !busy_ && !saving() && !exporting_) {
        emit closeReady();
    }
}

void EditorController::exportPhoto(const QUrl &destination, int format, int quality,
                                   bool overwrite) {
    if (!editable() || exporting_ || !destination.isLocalFile())
        return;
    luma::ExportOptions options{static_cast<luma::ExportFormat>(format), quality, overwrite};
    try {
        options.validate();
    } catch (const std::exception &failure) {
        error_ = QString::fromUtf8(failure.what());
        emit stateChanged();
        return;
    }
    const auto asset = session_->asset;
    const auto edits = history_.current();
    const auto path = std::filesystem::path(destination.toLocalFile().toStdString());
    exportCancellation_ = luma::CancellationSource{};
    const auto stop = exportCancellation_.token();
    exporting_ = true;
    exportProgress_ = 0;
    error_.clear();
    closeTransformTool();
    setCropMode(false);
    setLocalMode(false);
    endEdit();
    status_ = QStringLiteral("Exporting photo…");
    emit stateChanged();
    const QPointer<EditorController> self(this);
    // Preserve the immediate cancel/close opportunity before admitting I/O work.
    // The request stays exporting_ until its cancellation/completion is delivered.
    QMetaObject::invokeMethod(
        this,
        [self, asset, edits, path, options, stop] {
            if (!self)
                return;
            self->exportJob_ = self->ioJobs_.submit(
                luma::JobPriority::background,
                [self, asset, edits, path, options, stop](const luma::JobContext &context) {
                    QString error;
                    try {
                        self->service_.exportPhoto(
                            asset, edits, path, options, stop, [self, context](double value) {
                                context.setProgress(value);
                                if (self)
                                    QMetaObject::invokeMethod(
                                        self,
                                        [self, value] {
                                            if (self) {
                                                self->exportProgress_ = value;
                                                emit self->stateChanged();
                                            }
                                        },
                                        Qt::QueuedConnection);
                            });
                    } catch (const std::exception &failure) {
                        error = QString::fromUtf8(failure.what());
                    }
                    const bool cancelled = stop.isCancelled() && !error.isEmpty();
                    if (self)
                        QMetaObject::invokeMethod(
                            self,
                            [self, error, cancelled] {
                                if (!self)
                                    return;
                                self->exporting_ = false;
                                self->status_ = cancelled ? QStringLiteral("Export cancelled")
                                                : error.isEmpty()
                                                    ? QStringLiteral("Photo exported")
                                                    : QStringLiteral("Could not export photo");
                                if (!cancelled)
                                    self->error_ = error;
                                emit self->stateChanged();
                                self->checkClose();
                            },
                            Qt::QueuedConnection);
                });
        },
        Qt::QueuedConnection);
}
void EditorController::cancelExport() {
    exportCancellation_.cancel();
}
