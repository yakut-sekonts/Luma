#include "apps/desktop/EditorController.h"
#ifdef LUMA_UX_AUTO_TRANSFORM_PROBE
#include "tests/ux/AutoTransformProbe.h"
#endif
#include "core/render/TransformMapping.h"
#include <QPointer>
#include <algorithm>

void EditorController::validateGeometry(const luma::EditDocument &edits) const {
    if (session_) {
        const auto &meta = session_->asset.metadata;
        (void)luma::TransformMapping(meta.width, meta.height, meta.orientation, edits.transform,
                                     edits.enabled(luma::Correction::transform));
    }
}
QVariantMap EditorController::transformState() const {
    if (!session_)
        return {};
    const auto &meta = session_->asset.metadata;
    const auto &edits = history_.current();
    const luma::TransformMapping mapping(meta.width, meta.height, meta.orientation, edits.transform,
                                         edits.enabled(luma::Correction::transform));
    return {{"effectiveScale", edits.transform.scale * mapping.extraZoom()},
            {"guided", edits.transform.guided},
            {"guideCount", static_cast<int>(guides_.size())}};
}
QVariantList EditorController::transformGuides() const {
    QVariantList result;
    for (const auto &guide : guides_)
        result.append(QVariantMap{{"vertical", guide.axis == luma::GuideAxis::vertical},
                                  {"x1", guide.first[0]},
                                  {"y1", guide.first[1]},
                                  {"x2", guide.second[0]},
                                  {"y2", guide.second[1]}});
    return result;
}
void EditorController::setGuideLayout(int layout) {
    if (!guidedMode_ || layout < 0 || layout > 2)
        return;
    guideLayout_ = layout;
    guides_.clear();
    if (layout != 1) {
        guides_.push_back({luma::GuideAxis::vertical, {0.25, 0.15}, {0.25, 0.85}});
        guides_.push_back({luma::GuideAxis::vertical, {0.75, 0.15}, {0.75, 0.85}});
    }
    if (layout != 0) {
        guides_.push_back({luma::GuideAxis::horizontal, {0.15, 0.25}, {0.85, 0.25}});
        guides_.push_back({luma::GuideAxis::horizontal, {0.15, 0.75}, {0.85, 0.75}});
    }
    emit stateChanged();
}
void EditorController::setGuidedMode(bool enabled) {
    if (enabled == guidedMode_ || (enabled && (!editable() || original_)))
        return;
    cancelAutoTransform();
    endEdit();
    if (enabled) {
        setRetouchMode(QStringLiteral("off"));
        cancelAI();
        setLocalMode(false);
        setCropMode(false);
        targetedTool_ = QStringLiteral("none");
    }
    guidedMode_ = enabled;
    guidedCanvasReady_ = false;
    if (enabled && guides_.empty())
        setGuideLayout(guideLayout_);
    if (!enabled)
        guides_.clear();
    render();
    emit stateChanged();
}
void EditorController::moveGuidePoint(int line, int endpoint, double x, double y) {
    if (!guidedCanvasReady() || !editable() || line < 0 ||
        static_cast<std::size_t>(line) >= guides_.size() || endpoint < 0 || endpoint > 1 ||
        !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
        return;
    auto &guide = guides_[static_cast<std::size_t>(line)];
    (endpoint == 0 ? guide.first : guide.second) = {x, y};
    emit stateChanged();
}
void EditorController::applyTransform(luma::TransformSettings settings) {
    auto next = history_.current();
    next.transform = std::move(settings);
    next.setEnabled(luma::Correction::transform, true);
    validateGeometry(next);
    setGuidedMode(false);
    endEdit();
    error_.clear();
    applyAdvanced(std::move(next));
}
void EditorController::applyGuides() {
    if (!guidedCanvasReady() || !editable())
        return;
    try {
        const auto &meta = session_->asset.metadata;
        const double ratio = meta.orientation & 4
                                 ? static_cast<double>(meta.height - 1) / (meta.width - 1)
                                 : static_cast<double>(meta.width - 1) / (meta.height - 1);
        applyTransform(luma::solvePerspectiveGuides(guides_, ratio));
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
void EditorController::cancelAutoTransform() {
    transformJob_.cancel();
    ++transformAutoRevision_;
    if (transformAutoBusy_) {
        transformAutoBusy_ = false;
        emit stateChanged();
    }
}
void EditorController::closeTransformTool() {
    cancelAutoTransform();
    setGuidedMode(false);
}
void EditorController::resetTransform() {
    if (!editable())
        return;
    try {
        closeTransformTool();
        applyTransform({});
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
void EditorController::autoTransform() {
    if (!editable() || original_ || transformAutoBusy_)
        return;
    setGuidedMode(false);
    setCropMode(false);
    endEdit();
    error_.clear();
    transformAutoBusy_ = true;
    const auto revision = ++transformAutoRevision_, editRevision = editRevision_,
               loadRevision = loadRevision_;
    const auto image = session_->preview;
    auto edits = history_.current();
    edits.setEnabled(luma::Correction::transform, false);
    edits.setEnabled(luma::Correction::crop, false);
    const auto calibration = session_->calibration;
    const auto metadata = session_->asset.metadata;
    const double scale = session_->previewScale;
    const QPointer<EditorController> self(this);
#ifdef LUMA_UX_AUTO_TRANSFORM_PROBE
    const ux::AutoTransformOperation uxOperation{this,
                                                 revision,
                                                 editRevision,
                                                 loadRevision,
                                                 session_->asset.id,
                                                 session_->asset.path.string(),
                                                 {}};
#endif
    transformJob_ = transformJobs_.submit(
        luma::JobPriority::interactive,
        [self, image, edits, calibration, metadata, scale, revision, editRevision, loadRevision
#ifdef LUMA_UX_AUTO_TRANSFORM_PROBE
         ,
         uxOperation
#endif
    ](const luma::JobContext &context) {
            try {
                const auto small = luma::standardPreview(*image, 512, context.stopToken());
                const double smallScale =
                    std::min(static_cast<double>(small.width) / image->width,
                             static_cast<double>(small.height) / image->height);
                const auto rendered = luma::renderCPU(
                    small, edits, metadata.orientation, {}, context.stopToken(), calibration,
                    luma::DetailDiagnostic::none, scale * smallScale, metadata);
                const auto guides = luma::detectPerspectiveGuides(rendered, context.stopToken());
                const auto solved = luma::solvePerspectiveGuides(
                    guides, static_cast<double>(rendered.width - 1) / (rendered.height - 1));
#ifdef LUMA_UX_AUTO_TRANSFORM_PROBE
                auto uxSuccess = uxOperation;
                uxSuccess.job = context.data;
                ux::holdAutoTransformSuccess(uxSuccess, solved);
#endif
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self, solved, revision, editRevision, loadRevision
#ifdef LUMA_UX_AUTO_TRANSFORM_PROBE
                         ,
                         uxSuccess
#endif
                    ] {
#ifdef LUMA_UX_AUTO_TRANSFORM_PROBE
                            ux::observeAutoTransformSuccessDelivery(uxSuccess);
#endif
                            if (!self || self->transformAutoRevision_ != revision) {
#ifdef LUMA_UX_AUTO_TRANSFORM_PROBE
                                ux::observeAutoTransformRevisionRejection(
                                    uxSuccess, static_cast<bool>(self),
                                    self ? self->transformAutoRevision_ : 0,
                                    self ? self->editRevision_ : 0, self ? self->loadRevision_ : 0);
#endif
                                return;
                            }
                            self->transformAutoBusy_ = false;
                            if (self->editRevision_ == editRevision &&
                                self->loadRevision_ == loadRevision && self->editable()) {
                                try {
                                    self->applyTransform(solved);
                                } catch (const std::exception &failure) {
                                    self->advancedFailure(failure);
                                }
                            }
                            emit self->stateChanged();
                        },
                        Qt::QueuedConnection);
            } catch (const std::exception &failure) {
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self, revision, message = QString::fromUtf8(failure.what())] {
                            if (!self || self->transformAutoRevision_ != revision)
                                return;
                            self->transformAutoBusy_ = false;
                            self->error_ = message;
                            emit self->stateChanged();
                        },
                        Qt::QueuedConnection);
            }
        });
    emit stateChanged();
}
