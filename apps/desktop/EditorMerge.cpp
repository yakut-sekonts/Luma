#include "apps/desktop/EditorController.h"
#include "apps/shared/MergeService.h"
#include <QPointer>
#include <atomic>
#include <cmath>

namespace {
struct MergeProgress {
    std::atomic<double> value{0};
    std::atomic<bool> queued{false};
};
} // namespace
void EditorController::mergeHDRPhotos(const QStringList &ids, double deghost, bool alignFrames) {
    if (!std::isfinite(deghost) || deghost < 0 || deghost > 100)
        return;
    std::vector<std::string> sources;
    for (const auto &id : ids)
        sources.push_back(id.toStdString());
    runMerge(QStringLiteral("HDR merge"),
             [catalog = catalog_, sources, deghost, alignFrames](auto stop, const auto &progress) {
                 return luma::MergeService(catalog).hdr(sources, {deghost / 100, alignFrames}, stop,
                                                        progress);
             });
}

void EditorController::mergePanoramaPhotos(const QStringList &ids, int projection,
                                           double fieldOfView, bool boundaryWarp) {
    if (projection < 0 || projection > 2 || !std::isfinite(fieldOfView) || fieldOfView < 10 ||
        fieldOfView > 140)
        return;
    std::vector<std::string> sources;
    for (const auto &id : ids)
        sources.push_back(id.toStdString());
    const luma::PanoramaOptions options{static_cast<luma::PanoramaProjection>(projection),
                                        fieldOfView, boundaryWarp};
    runMerge(QStringLiteral("Panorama merge"),
             [catalog = catalog_, sources, options](auto stop, const auto &progress) {
                 return luma::MergeService(catalog).panorama(sources, options, stop, progress);
             });
}

void EditorController::runMerge(const QString &operation,
                                std::function<std::filesystem::path(
                                    luma::CancellationToken, const std::function<void(double)> &)>
                                    work) {
    if (busy_ || closing_ || editingLocked_ || aiBusy_)
        return;
    cancelAI();
    setRetouchMode(QStringLiteral("off"));
    endEdit();
    const auto revision = ++aiRevision_;
    const QPointer<EditorController> self(this);
    aiBusy_ = true;
    aiProgress_ = 0;
    aiOperation_ = operation;
    error_.clear();
    emit stateChanged();
    try {
        const auto task = std::make_shared<decltype(work)>(std::move(work));
        aiJob_ = aiJobs_.submit(
            luma::JobPriority::background, [self, revision, task](const luma::JobContext &context) {
                try {
                    const auto progress = std::make_shared<MergeProgress>();
                    const auto created =
                        (*task)(context.stopToken(), [self, revision, progress](double value) {
                            progress->value.store(value);
                            // Per-row callbacks must not enqueue thousands of full
                            // QML state refreshes ahead of Cancel and GUI heartbeats.
                            if (self && !progress->queued.exchange(true))
                                QMetaObject::invokeMethod(
                                    self,
                                    [self, revision, progress] {
                                        progress->queued.store(false);
                                        if (self && self->aiRevision_ == revision) {
                                            self->aiProgress_ = progress->value.load();
                                            emit self->stateChanged();
                                        }
                                    },
                                    Qt::QueuedConnection);
                        });
                    if (self)
                        QMetaObject::invokeMethod(
                            self,
                            [self, revision, created] {
                                if (!self || self->aiRevision_ != revision)
                                    return;
                                self->aiBusy_ = false;
                                self->aiProgress_ = 1;
                                emit self->stateChanged();
                                self->openFile(
                                    QUrl::fromLocalFile(QString::fromStdString(created.string())));
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
        cancelAI();
        advancedFailure(failure);
    }
}
