#include "apps/desktop/EditorController.h"
#include "core/presets/SettingsSubset.h"

void EditorController::applySettings(const luma::EditDocument &settings, std::uint32_t groups,
                                     std::span<const std::string> parameters) {
    if (!editable())
        return;
    try {
        setRetouchMode(QStringLiteral("off"));
        cancelAI();
        setLocalMode(false);
        closeTransformTool();
        setCropMode(false);
        const auto next = luma::mergeSettings(history_.current(), settings, groups, parameters);
        validateGeometry(next);
        endEdit();
        if (next == history_.current())
            return;
        history_.apply(next, "Apply settings");
        editsChanged();
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
void EditorController::previewSettings(const luma::EditDocument &settings, std::uint32_t groups,
                                       std::span<const std::string> parameters) {
    if (!editable() || original_)
        return;
    try {
        setRetouchMode(QStringLiteral("off"));
        cancelAI();
        setLocalMode(false);
        closeTransformTool();
        setCropMode(false);
        const auto next = luma::mergeSettings(history_.current(), settings, groups, parameters);
        validateGeometry(next);
        if (presetPreview_ == next)
            return;
        endEdit();
        presetPreview_ = next;
        render();
        emit stateChanged();
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
void EditorController::clearPreviewSettings() {
    if (!presetPreview_)
        return;
    presetPreview_.reset();
    render();
    emit stateChanged();
}

bool EditorController::canPrepareBatch() const {
    return !transformAutoBusy_ && !busy() && !exporting_ && !closing_ && !editingLocked_ &&
           deferredOpen_.isEmpty();
}
QString EditorController::photoID() const {
    return session_ ? QString::fromStdString(session_->asset.id) : QString();
}
void EditorController::prepareExternalEdits() {
    cancelAI();
    setRetouchMode(QStringLiteral("off"));
    setLocalMode(false);
    closeTransformTool();
    setCropMode(false);
    endEdit();
    editingLocked_ = true;
    flushSave();
    emit stateChanged();
}
void EditorController::finishExternalEdits(const std::optional<luma::EditDocument> &edits) {
    if (edits && session_) {
        edits->validate();
        session_->asset.edits = *edits;
        history_ = luma::EditHistory(*edits, "After batch change");
        ++historyGeneration_;
        historyError_.clear();
        render();
    }
    editingLocked_ = false;
    emit stateChanged();
}
