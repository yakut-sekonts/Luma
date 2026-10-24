#include "apps/desktop/EditorController.h"
#include "core/render/CropGeometry.h"

QVariantMap EditorController::cropState() const {
    if (!session_)
        return {};
    const auto &edits = history_.current();
    const auto &crop = edits.crop;
    const auto &meta = session_->asset.metadata;
    const luma::CropGeometry geometry(meta.width, meta.height, meta.orientation, edits);
    const auto frame = geometry.frame();
    auto canvasEdits = edits;
    canvasEdits.crop.x = 0;
    canvasEdits.crop.y = 0;
    canvasEdits.crop.width = 1;
    canvasEdits.crop.height = 1;
    canvasEdits.crop.aspect = 0;
    canvasEdits.crop.angle = 0;
    const luma::CropGeometry original(meta.width, meta.height, meta.orientation, canvasEdits);
    const auto ratio =
        crop.aspect == -1 ? static_cast<double>(original.width()) / original.height() : crop.aspect;
    return {{"x", frame[0]},
            {"y", frame[1]},
            {"width", frame[2]},
            {"height", frame[3]},
            {"angle", crop.angle},
            {"aspect", crop.aspect},
            {"quarterTurns", crop.quarterTurns},
            {"flipHorizontal", crop.flipHorizontal},
            {"flipVertical", crop.flipVertical},
            {"canvasWidth", geometry.canvasWidth()},
            {"canvasHeight", geometry.canvasHeight()},
            {"outputWidth", geometry.width()},
            {"outputHeight", geometry.height()},
            {"normalizedRatio",
             ratio > 0 ? ratio * geometry.canvasHeight() / geometry.canvasWidth() : 0}};
}
void EditorController::setCropMode(bool value) {
    if (value == cropMode_ || (value && (!editable() || original_ ||
                                         !history_.current().enabled(luma::Correction::crop))))
        return;
    if (value) {
        setRetouchMode(QStringLiteral("off"));
        cancelAI();
        setLocalMode(false);
        closeTransformTool();
    }
    endEdit();
    cropMode_ = value;
    cropCanvasReady_ = false;
    if (value)
        targetedTool_ = QStringLiteral("none");
    render();
    emit stateChanged();
}
void EditorController::setCropOverlay(const QString &value) {
    if (value != QStringLiteral("thirds") && value != QStringLiteral("grid") &&
        value != QStringLiteral("diagonal") && value != QStringLiteral("golden"))
        return;
    if (cropOverlay_ == value)
        return;
    cropOverlay_ = value;
    emit stateChanged();
}
void EditorController::setCropRect(double x, double y, double width, double height) {
    if (!editable() || !history_.current().enabled(luma::Correction::crop))
        return;
    try {
        auto next = history_.current();
        next.crop.x = x;
        next.crop.y = y;
        next.crop.width = width;
        next.crop.height = height;
        next.validate();
        if (next == history_.current())
            return;
        history_.apply(next);
        editsChanged(!cropMode_);
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
void EditorController::rotateCrop(int turns) {
    if (!editable() || !history_.current().enabled(luma::Correction::crop))
        return;
    endEdit();
    auto next = history_.current();
    next.crop.quarterTurns = ((next.crop.quarterTurns + turns % 4) % 4 + 4) % 4;
    if (next == history_.current())
        return;
    history_.apply(next);
    editsChanged();
}
void EditorController::resetCrop() {
    if (!editable())
        return;
    endEdit();
    auto next = history_.current();
    next.crop = {};
    next.setEnabled(luma::Correction::crop, true);
    if (next == history_.current())
        return;
    history_.apply(next);
    editsChanged();
}
