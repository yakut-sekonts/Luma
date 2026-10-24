#pragma once
#ifndef LUMA_UX_PREVIEW_RESULT_PROBE
#error "Preview result probe is restricted to the UX executable"
#endif
#include "core/jobs/JobManager.h"
#include <QImage>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class EditorController;
namespace ux {
struct PreviewOperation {
    const EditorController *controller = nullptr;
    std::uint64_t revision = 0;
    int resolution = 0;
    std::string assetID;
    std::shared_ptr<const luma::JobData> job;
    bool sameAs(const PreviewOperation &other) const noexcept;
};
struct PreviewResultSnapshot {
    std::optional<PreviewOperation> held, delivered;
    QImage pixels;
    unsigned deliveries = 0, rejections = 0;
    std::uint64_t rejectionRevision = 0;
    bool released = false, deliveryBeforeRelease = false, hookFailed = false;
};
struct PreviewResultState;
// Retains the first matching actual success dispatch, after cancellation was
// checked. It does not occupy the single render worker or fabricate a result.
// Later successes are dispatched normally; release posts the retained original
// lambda through the same QueuedConnection as production. Destruction releases it.
class PreviewResultGate final {
  public:
    PreviewResultGate(EditorController &editor, int resolution);
    ~PreviewResultGate() noexcept;
    PreviewResultGate(const PreviewResultGate &) = delete;
    PreviewResultGate &operator=(const PreviewResultGate &) = delete;
    PreviewResultSnapshot snapshot() const;
    void release() noexcept;

  private:
    std::shared_ptr<PreviewResultState> state_;
};
bool deferPreviewSuccess(const PreviewOperation &operation, const QImage &pixels,
                         std::function<void()> dispatch) noexcept;
void observePreviewDelivery(const PreviewOperation &operation) noexcept;
void observePreviewRejection(const PreviewOperation &operation,
                             std::uint64_t currentRevision) noexcept;
} // namespace ux
