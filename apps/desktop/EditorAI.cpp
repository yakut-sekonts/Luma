#include "apps/desktop/BundledModels.h"
#include "apps/desktop/EditorController.h"
#include "core/catalog/Identity.h"
#include "core/image/Bayer.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/CoreMLSky.h"
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <algorithm>
#include <cmath>

QString EditorController::skyModelName() const {
    return QFileInfo(skyModelPath_).fileName();
}
bool EditorController::skyModelBundled() const {
    return !skyModelPath_.isEmpty() &&
           skyModelPath_ == luma::desktop::bundledModelPath(luma::desktop::BundledModel::sky);
}
void EditorController::setSkyModel(const QUrl &url) {
    if (!url.isEmpty() && !url.isLocalFile())
        return;
    cancelAI();
    const auto overridePath = url.isEmpty() ? QString() : url.toLocalFile();
    skyModelPath_ = overridePath.isEmpty()
                        ? luma::desktop::bundledModelPath(luma::desktop::BundledModel::sky)
                        : overridePath;
    QSettings settings(QString::fromStdString(catalog_.parent_path().string()) + "/preview.ini",
                       QSettings::IniFormat);
    settings.setValue("skyModel", overridePath);
    emit stateChanged();
}
void EditorController::cancelAI() {
    aiJob_.cancel();
    ++aiRevision_;
    if (!aiBusy_ && !aiObjectPicking_)
        return;
    aiBusy_ = false;
    aiObjectPicking_ = false;
    aiProgress_ = 0;
    emit stateChanged();
}
void EditorController::beginAIObjectSelection() {
    if (!editable() || original_)
        return;
    cancelAI();
    setRetouchMode(QStringLiteral("off"));
    endEdit();
    setLocalMode(false);
    setCropMode(false);
    closeTransformTool();
    targetedTool_ = QStringLiteral("none");
    setOneToOne(false);
    aiObjectPicking_ = true;
    emit stateChanged();
}
void EditorController::generateAIMask(const QString &kind, double x, double y) {
    if (!editable() || original_ || aiBusy_)
        return;
    try {
        if (kind != "subject" && kind != "background" && kind != "object" && kind != "sky")
            throw std::runtime_error("Unknown AI selection category.");
        const auto selection = kind == QStringLiteral("subject") ? luma::ai::Selection::subject
                               : kind == QStringLiteral("background")
                                   ? luma::ai::Selection::background
                               : kind == QStringLiteral("object") ? luma::ai::Selection::object
                                                                  : luma::ai::Selection::sky;
        if ((selection == luma::ai::Selection::sky && !skyModelConfigured()) ||
            (selection != luma::ai::Selection::sky && !segmentation_->supports(selection)))
            throw std::runtime_error("The requested AI selection is not available locally.");
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x > 1 || y > 1)
            return;
        const auto source = session_->preview;
        const auto metadata = session_->asset.metadata;
        const auto calibration = session_->calibration;
        const luma::CropGeometry displayed(source->width, source->height, metadata.orientation,
                                           history_.current());
        if (selection == luma::ai::Selection::object &&
            !displayed.covered(x * displayed.width() - .5, y * displayed.height() - .5))
            return;
        const auto point =
            displayed.source(x * displayed.width() - .5, y * displayed.height() - .5);
        const std::array<double, 2> sourcePoint{
            std::clamp((point[0] + .5) / source->width, 0.0, 1.0),
            std::clamp((point[1] + .5) / source->height, 0.0, 1.0)};
        cancelAI();
        setRetouchMode(QStringLiteral("off"));
        endEdit();
        aiObjectPicking_ = false;
        aiBusy_ = true;
        aiOperation_ = QStringLiteral("AI selection");
        aiProgress_ = 0;
        error_.clear();
        const auto revision = ++aiRevision_;
        const auto provider = segmentation_;
        const auto skyModel = std::filesystem::path(skyModelPath_.toStdString());
        auto referenceEdits = history_.current();
        referenceEdits.localMasks.clear();
        referenceEdits.setEnabled(luma::Correction::crop, false);
        referenceEdits.setEnabled(luma::Correction::transform, false);
        const double previewScale = session_->previewScale;
        const QPointer<EditorController> self(this);
        emit stateChanged();
        aiJob_ = aiJobs_.submit(
            luma::JobPriority::background,
            [self, source, metadata, calibration, revision, provider, referenceEdits, selection,
             sourcePoint, previewScale, skyModel](const luma::JobContext &context) {
                try {
                    const auto stop = context.stopToken();
                    const auto input = luma::standardPreview(*source, 1024, stop);
                    const double scale =
                        previewScale * std::min(static_cast<double>(input.width) / source->width,
                                                static_cast<double>(input.height) / source->height);
                    const auto pixels =
                        luma::renderCPU(input, referenceEdits, metadata.orientation, {}, stop,
                                        calibration, luma::DetailDiagnostic::none, scale, metadata);
                    const luma::CropGeometry geometry(input.width, input.height,
                                                      metadata.orientation, referenceEdits);
                    const auto picked = geometry.output(sourcePoint[0] * input.width - .5,
                                                        sourcePoint[1] * input.height - .5);
                    const std::array<double, 2> uprightPoint{
                        std::clamp((picked[0] + .5) / geometry.width(), 0.0, 1.0),
                        std::clamp((picked[1] + .5) / geometry.height(), 0.0, 1.0)};
                    const auto activeProvider = selection == luma::ai::Selection::sky
                                                    ? luma::macos::makeCoreMLSky(skyModel)
                                                    : provider;
                    luma::checkCancellation(stop);
                    const auto bitmap = activeProvider->segment(
                        {pixels.width, pixels.height, pixels.rgba}, selection, uprightPoint, stop,
                        [self, revision](double value) {
                            if (self)
                                QMetaObject::invokeMethod(
                                    self,
                                    [self, revision, value] {
                                        if (self && self->aiRevision_ == revision) {
                                            self->aiProgress_ = value;
                                            emit self->stateChanged();
                                        }
                                    },
                                    Qt::QueuedConnection);
                        });
                    std::vector<std::uint8_t> values(luma::pixelCount(input.width, input.height));
                    for (int row = 0; row < input.height; ++row) {
                        luma::checkCancellation(stop);
                        for (int column = 0; column < input.width; ++column) {
                            const auto point = geometry.output(column, row);
                            values[static_cast<std::size_t>(row) * input.width + column] =
                                static_cast<std::uint8_t>(std::lround(
                                    255 * bitmap.coverage((point[0] + .5) / geometry.width(),
                                                          (point[1] + .5) / geometry.height())));
                        }
                    }
                    luma::BitmapMask anchored(input.width, input.height, std::move(values));
                    luma::checkCancellation(stop);
                    if (self)
                        QMetaObject::invokeMethod(
                            self,
                            [self, revision, selection, anchored] {
                                if (!self || self->aiRevision_ != revision)
                                    return;
                                self->aiBusy_ = false;
                                if (!self->editable()) {
                                    emit self->stateChanged();
                                    return;
                                }
                                try {
                                    auto next = self->history_.current();
                                    luma::LocalMask mask;
                                    mask.id = luma::newID();
                                    const char *name =
                                        selection == luma::ai::Selection::background ? "Background"
                                        : selection == luma::ai::Selection::object   ? "Object"
                                        : selection == luma::ai::Selection::sky      ? "Sky"
                                                                                     : "Subject";
                                    mask.name =
                                        QCoreApplication::translate("Luma", name).toStdString();
                                    mask.shape = luma::MaskShape::bitmap;
                                    mask.bitmap = anchored;
                                    next.localMasks.push_back(mask);
                                    next.validate();
                                    // Check the serialized storage budget before changing history.
                                    luma::encodeLocalMasks(next.localMasks);
                                    self->localSelected_ = QString::fromStdString(mask.id);
                                    self->localComponentSelected_.clear();
                                    self->setLocalMode(true);
                                    self->localOverlay_ = true;
                                    self->applyAdvanced(std::move(next));
                                    emit self->stateChanged();
                                } catch (const std::exception &failure) {
                                    self->advancedFailure(failure);
                                }
                            },
                            Qt::QueuedConnection);
                } catch (const std::exception &failure) {
                    const auto message = QString::fromUtf8(failure.what());
                    const bool cancelled = context.stopToken().isCancelled();
                    if (self)
                        QMetaObject::invokeMethod(
                            self,
                            [self, revision, message, cancelled] {
                                if (!self || self->aiRevision_ != revision)
                                    return;
                                self->aiBusy_ = false;
                                if (!cancelled)
                                    self->error_ = message;
                                emit self->stateChanged();
                            },
                            Qt::QueuedConnection);
                }
            });
    } catch (const std::exception &failure) {
        aiBusy_ = false;
        advancedFailure(failure);
    }
}
