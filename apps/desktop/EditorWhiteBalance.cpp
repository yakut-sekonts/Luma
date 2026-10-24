#include "apps/desktop/EditorController.h"
#include "core/render/WhiteBalanceSampler.h"
#include <QPointer>
#include <cmath>

void EditorController::cancelWhiteBalance() {
    whiteBalanceJob_.cancel();
    ++whiteBalanceRevision_;
    const bool changed =
        whiteBalanceBusy_ || whiteBalancePicking_ || !whiteBalanceMessage_.text().isEmpty();
    whiteBalanceBusy_ = false;
    whiteBalancePicking_ = false;
    whiteBalanceCanvasReady_ = false;
    whiteBalanceMessage_ = QString();
    if (changed)
        emit stateChanged();
}
void EditorController::setWhiteBalancePicking(bool enabled) {
    if (!enabled) {
        cancelWhiteBalance();
        return;
    }
    if (!editable() || original_ || exporting_ || !deferredOpen_.isEmpty() ||
        !history_.current().enabled(luma::Correction::whiteBalance))
        return;
    finishHistoryTools();
    targetedTool_ = QStringLiteral("none");
    setOneToOne(false);
    whiteBalancePicking_ = true;
    whiteBalanceCanvasReady_ = false;
    render(true);
    emit stateChanged();
}
void EditorController::autoWhiteBalance() {
    if (!editable() || original_ || exporting_ || !deferredOpen_.isEmpty() ||
        !history_.current().enabled(luma::Correction::whiteBalance))
        return;
    finishHistoryTools();
    targetedTool_ = QStringLiteral("none");
    runWhiteBalance(std::nullopt);
}
void EditorController::sampleWhiteBalance(double x, double y) {
    if (!whiteBalanceCanvasReady() || !editable() || original_ || !std::isfinite(x) ||
        !std::isfinite(y) || x < 0 || y < 0 || x > 1 || y > 1)
        return;
    runWhiteBalance(std::array{x, y});
}
void EditorController::runWhiteBalance(std::optional<std::array<double, 2>> point) {
    if (whiteBalanceBusy_ || !editable())
        return;
    whiteBalanceBusy_ = true;
    whiteBalanceMessage_ = QString();
    const auto revision = ++whiteBalanceRevision_, editRevision = editRevision_,
               loadRevision = loadRevision_, historyRevision = history_.revision(),
               generation = historyGeneration_;
    const auto image = session_->preview;
    const auto edits = history_.current();
    const auto metadata = session_->asset.metadata;
    const auto calibration = session_->calibration;
    const QPointer<EditorController> self(this);
    try {
        whiteBalanceJob_ = whiteBalanceJobs_.submit(
            luma::JobPriority::interactive,
            [self, image, edits, metadata, calibration, point, revision, editRevision, loadRevision,
             historyRevision, generation](const luma::JobContext &context) {
                std::optional<luma::WhiteBalanceEstimate> estimate;
                QString error;
                try {
                    estimate = luma::sampleWhiteBalance(*image, edits, metadata, calibration, point,
                                                        context.stopToken());
                } catch (const std::exception &failure) {
                    error = QString::fromUtf8(failure.what());
                }
                if (context.stopToken().isCancelled() || !self)
                    return;
                QMetaObject::invokeMethod(
                    self,
                    [self, estimate, error, point, revision, editRevision, loadRevision,
                     historyRevision, generation] {
                        if (!self || self->whiteBalanceRevision_ != revision)
                            return;
                        self->whiteBalanceBusy_ = false;
                        if (!self->editable() || self->original_ ||
                            self->editRevision_ != editRevision ||
                            self->loadRevision_ != loadRevision ||
                            self->historyGeneration_ != generation ||
                            self->history_.revision() != historyRevision ||
                            self->history_.gestureActive()) {
                            self->cancelWhiteBalance();
                            emit self->stateChanged();
                            return;
                        }
                        if (!estimate) {
                            self->whiteBalanceMessage_ = error;
                            emit self->stateChanged();
                            return;
                        }
                        try {
                            self->endEdit();
                            auto next = self->history_.current();
                            next.temperature = estimate->temperature;
                            next.tint = estimate->tint;
                            next.customWhiteBalance = true;
                            next.validate();
                            if (next != self->history_.current()) {
                                self->history_.apply(next, point ? "White balance eyedropper"
                                                                 : "Auto white balance");
                                self->editsChanged();
                            }
                            self->whiteBalanceMessage_ =
                                estimate->limited
                                    ? QStringLiteral("Applied the closest white balance within the "
                                                     "supported Temperature/Tint range.")
                                : point
                                    ? QStringLiteral("White balance set from the neutral sample.")
                                    : QStringLiteral("Auto white balance applied. Review "
                                                     "Temperature and Tint.");
                        } catch (const std::exception &failure) {
                            self->whiteBalanceMessage_ = QString::fromUtf8(failure.what());
                        }
                        emit self->stateChanged();
                    },
                    Qt::QueuedConnection);
            });
    } catch (const std::exception &failure) {
        whiteBalanceBusy_ = false;
        whiteBalanceMessage_ = QString::fromUtf8(failure.what());
    }
    emit stateChanged();
}
