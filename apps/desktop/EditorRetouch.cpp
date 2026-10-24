#include "apps/desktop/EditorController.h"
#include "core/catalog/Identity.h"
#include "core/image/Bayer.h"
#include "core/lens/LensCorrection.h"
#include "core/render/RetouchPass.h"
#ifdef LUMA_UX_RETOUCH_RESULT_PROBE
#include "tests/ux/RetouchResultProbe.h"
#endif
#include <QPointer>
#include <algorithm>
#include <cmath>

namespace {
QString modeKey(luma::RetouchMode mode) {
    return mode == luma::RetouchMode::clone  ? QStringLiteral("clone")
           : mode == luma::RetouchMode::heal ? QStringLiteral("heal")
                                             : QStringLiteral("remove");
}
} // namespace
int EditorController::retouchIndex() const {
    const auto &strokes = history_.current().retouch;
    const auto found = std::find_if(strokes.begin(), strokes.end(), [&](const auto &s) {
        return s.id == retouchSelected_.toStdString();
    });
    return found == strokes.end() ? -1 : static_cast<int>(found - strokes.begin());
}
QVariantMap EditorController::retouchState() const {
    const int index = retouchIndex();
    const auto *stroke =
        index >= 0 ? &history_.current().retouch[static_cast<std::size_t>(index)] : nullptr;
    return {{"selected", stroke ? retouchSelected_ : QString()},
            {"radius", (stroke ? stroke->radius : retouchRadius_) * 100},
            {"feather", (stroke ? stroke->feather : retouchFeather_) * 100},
            {"opacity", (stroke ? stroke->opacity : retouchOpacity_) * 100},
            {"sourceSet", retouchSource_.has_value()},
            {"drawing", retouchDraft_.has_value()},
            {"enabled", index >= 0
                            ? history_.current().retouch[static_cast<std::size_t>(index)].enabled
                            : true}};
}
QVariantList EditorController::retouchItems() const {
    QVariantList result;
    int index = 0;
    for (const auto &stroke : history_.current().retouch)
        result.append(QVariantMap{{"id", QString::fromStdString(stroke.id)},
                                  {"mode", modeKey(stroke.mode)},
                                  {"number", ++index}});
    return result;
}
void EditorController::cancelRetouch() {
    retouchJob_.cancel();
    ++retouchRevision_;
    const bool changed = retouchBusy_ || retouchDraft_.has_value();
    retouchBusy_ = false;
    retouchDraft_.reset();
    if (changed)
        emit stateChanged();
}
void EditorController::setRetouchMode(const QString &mode) {
    if (mode != "off" && mode != "clone" && mode != "heal" && mode != "remove")
        return;
    if (mode == retouchMode_ || (mode != "off" && (!editable() || original_)))
        return;
    cancelRetouch();
    endEdit();
    if (mode != "off") {
        cancelAI();
        setLocalMode(false);
        setCropMode(false);
        closeTransformTool();
        targetedTool_ = QStringLiteral("none");
        clearPreviewSettings();
        setOneToOne(false);
    }
    retouchMode_ = mode;
    retouchSelected_.clear();
    emit stateChanged();
}
void EditorController::selectRetouch(const QString &id) {
    if (!editable())
        return;
    cancelRetouch();
    endEdit();
    const auto &strokes = history_.current().retouch;
    const auto it = std::find_if(strokes.begin(), strokes.end(),
                                 [&](const auto &s) { return s.id == id.toStdString(); });
    if (!id.isEmpty() && it == strokes.end())
        return;
    retouchSelected_ = id;
    if (it != strokes.end()) {
        retouchMode_ = modeKey(it->mode);
        retouchRadius_ = it->radius;
        retouchFeather_ = it->feather;
        retouchOpacity_ = it->opacity;
        retouchSource_ = std::array{it->points.front()[0] + it->sourceOffsetX,
                                    it->points.front()[1] + it->sourceOffsetY};
    }
    emit stateChanged();
}
void EditorController::removeRetouch() {
    if (!editable() || retouchIndex() < 0)
        return;
    auto next = history_.current();
    next.retouch.erase(next.retouch.begin() + retouchIndex());
    retouchSelected_.clear();
    endEdit();
    applyAdvanced(std::move(next));
}
void EditorController::setRetouchEnabled(bool value) {
    if (!editable() || retouchIndex() < 0)
        return;
    auto next = history_.current();
    next.retouch[static_cast<std::size_t>(retouchIndex())].enabled = value;
    endEdit();
    applyAdvanced(std::move(next));
}
void EditorController::setRetouchControl(const QString &name, double value) {
    if (!std::isfinite(value) || !editable())
        return;
    double *setting = name == "radius"    ? &retouchRadius_
                      : name == "feather" ? &retouchFeather_
                      : name == "opacity" ? &retouchOpacity_
                                          : nullptr;
    if (!setting || value < (name == "radius" ? .1 : 0) || value > (name == "radius" ? 25 : 100))
        return;
    *setting = value / 100;
    const int index = retouchIndex();
    if (index >= 0) {
        auto next = history_.current();
        auto &stroke = next.retouch[static_cast<std::size_t>(index)];
        if (name == "radius")
            stroke.radius = retouchRadius_;
        if (name == "feather")
            stroke.feather = retouchFeather_;
        if (name == "opacity")
            stroke.opacity = retouchOpacity_;
        applyAdvanced(std::move(next));
    }
    emit stateChanged();
}
std::optional<std::array<double, 2>> EditorController::retouchPoint(double x, double y) const {
    if (!session_ || !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
        return {};
    const auto &image = *session_->preview;
    const luma::CropGeometry geometry(image.width, image.height,
                                      session_->asset.metadata.orientation, history_.current());
    const double px = x * geometry.width() - .5, py = y * geometry.height() - .5;
    if (!geometry.covered(px, py))
        return {};
    const auto source = geometry.source(px, py);
    return std::array{std::clamp((source[0] + .5) / image.width, 0.0, 1.0),
                      std::clamp((source[1] + .5) / image.height, 0.0, 1.0)};
}
void EditorController::pickRetouchSource(double x, double y) {
    if (!editable() || original_ || retouchMode_ == "off" || retouchBusy_)
        return;
    try {
        const auto point = retouchPoint(x, y);
        if (!point)
            return;
        retouchSource_ = point;
        const int index = retouchIndex();
        if (index >= 0) {
            endEdit();
            auto next = history_.current();
            auto &stroke = next.retouch[static_cast<std::size_t>(index)];
            stroke.sourceOffsetX = (*point)[0] - stroke.points.front()[0];
            stroke.sourceOffsetY = (*point)[1] - stroke.points.front()[1];
            applyAdvanced(std::move(next));
        }
        emit stateChanged();
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
void EditorController::beginRetouch(double x, double y) {
    if (!editable() || original_ || retouchMode_ == "off" || retouchBusy_)
        return;
    try {
        const auto point = retouchPoint(x, y);
        if (!point)
            return;
        if (retouchMode_ != "remove" && !retouchSource_)
            throw std::runtime_error("Option-click a source area before drawing.");
        cancelRetouch();
        endEdit();
        luma::RetouchStroke stroke;
        stroke.id = luma::newID();
        stroke.mode = retouchMode_ == "clone"  ? luma::RetouchMode::clone
                      : retouchMode_ == "heal" ? luma::RetouchMode::heal
                                               : luma::RetouchMode::remove;
        stroke.radius = retouchRadius_;
        stroke.feather = retouchFeather_;
        stroke.opacity = retouchOpacity_;
        stroke.points.push_back(*point);
        if (retouchSource_) {
            stroke.sourceOffsetX = (*retouchSource_)[0] - (*point)[0];
            stroke.sourceOffsetY = (*retouchSource_)[1] - (*point)[1];
        }
        retouchSelected_.clear();
        retouchDraft_ = std::move(stroke);
        error_.clear();
        emit stateChanged();
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
void EditorController::continueRetouch(double x, double y) {
    if (!retouchDraft_ || !editable())
        return;
    try {
        const auto point = retouchPoint(x, y);
        if (!point)
            return;
        auto &points = retouchDraft_->points;
        const auto previous = points.back();
        if (std::hypot((*point)[0] - previous[0], (*point)[1] - previous[1]) < .0005)
            return;
        if (points.size() >= 4096)
            throw std::runtime_error("Retouch stroke is too long. Start another stroke.");
        points.push_back(*point);
        emit stateChanged();
    } catch (const std::exception &failure) {
        cancelRetouch();
        advancedFailure(failure);
    }
}
void EditorController::endRetouch() {
    if (!retouchDraft_ || !editable()) {
        cancelRetouch();
        return;
    }
    auto stroke = *retouchDraft_;
    retouchDraft_.reset();
    if (stroke.mode != luma::RetouchMode::remove) {
        try {
            auto next = history_.current();
            next.retouch.push_back(stroke);
            next.validate();
            retouchSelected_ = QString::fromStdString(stroke.id);
            applyAdvanced(std::move(next));
        } catch (const std::exception &failure) {
            advancedFailure(failure);
        }
        return;
    }
    retouchBusy_ = true;
    const auto revision = ++retouchRevision_;
    const auto source =
        session_->interactivePreview ? session_->interactivePreview : session_->preview;
    const auto metadata = session_->asset.metadata;
    const auto edits = history_.current();
    const QPointer<EditorController> self(this);
    emit stateChanged();
    try {
        retouchJob_ = retouchJobs_.submit(
            luma::JobPriority::background, [self, revision, source, metadata, edits,
                                            stroke](const luma::JobContext &context) mutable {
                try {
                    const auto stop = context.stopToken();
                    const auto small = luma::standardPreview(*source, 768, stop);
                    const luma::LensCorrectionPass optics(small, edits, metadata, stop);
                    const luma::RetouchPass prior(optics.image(), edits.retouch, stop);
                    const auto donor = luma::findRetouchSource(prior.image(), stroke, stop);
                    stroke.sourceOffsetX = donor[0];
                    stroke.sourceOffsetY = donor[1];
                    luma::checkCancellation(stop);
                    const auto publish = [self, revision, stroke] {
#ifdef LUMA_UX_RETOUCH_RESULT_PROBE
                        ux::observeRetouchDelivery(self.data(), revision,
                                                   !self || self->retouchRevision_ != revision);
#endif
                        if (!self || self->retouchRevision_ != revision)
                            return;
                        self->retouchBusy_ = false;
                        if (!self->editable()) {
                            emit self->stateChanged();
                            return;
                        }
                        try {
                            auto next = self->history_.current();
                            next.retouch.push_back(stroke);
                            next.validate();
                            self->retouchSelected_ = QString::fromStdString(stroke.id);
                            self->retouchSource_ =
                                std::array{stroke.points.front()[0] + stroke.sourceOffsetX,
                                           stroke.points.front()[1] + stroke.sourceOffsetY};
                            self->applyAdvanced(std::move(next));
                        } catch (const std::exception &failure) {
                            self->advancedFailure(failure);
                        }
                        emit self->stateChanged();
                    };
#ifdef LUMA_UX_RETOUCH_RESULT_PROBE
                    if (ux::deferRetouchSuccess(self.data(), revision, stroke, [self, publish] {
                            if (self)
                                QMetaObject::invokeMethod(self, publish, Qt::QueuedConnection);
                        }))
                        return;
#endif
                    if (self)
                        QMetaObject::invokeMethod(self, publish, Qt::QueuedConnection);
                } catch (const std::exception &failure) {
                    const auto message = QString::fromUtf8(failure.what());
                    const bool cancelled = context.stopToken().isCancelled();
                    if (self)
                        QMetaObject::invokeMethod(
                            self,
                            [self, revision, message, cancelled] {
                                if (!self || self->retouchRevision_ != revision)
                                    return;
                                self->retouchBusy_ = false;
                                if (!cancelled)
                                    self->error_ = message;
                                emit self->stateChanged();
                            },
                            Qt::QueuedConnection);
                }
            });
    } catch (const std::exception &failure) {
        cancelRetouch();
        advancedFailure(failure);
    }
}
QVariantMap EditorController::retouchOverlay() const {
    if (!session_ || retouchMode_ == "off")
        return {};
    try {
        const auto &image = *session_->preview;
        const luma::CropGeometry geometry(image.width, image.height,
                                          session_->asset.metadata.orientation, history_.current());
        const int index = retouchIndex();
        const luma::RetouchStroke *stroke =
            retouchDraft_ ? &*retouchDraft_
            : index >= 0  ? &history_.current().retouch[static_cast<std::size_t>(index)]
                          : nullptr;
        const auto map = [&](const std::array<double, 2> &p) {
            const auto output = geometry.output(p[0] * image.width - .5, p[1] * image.height - .5);
            return QVariantMap{{"x", (output[0] + .5) / geometry.width()},
                               {"y", (output[1] + .5) / geometry.height()}};
        };
        QVariantList points;
        if (stroke)
            for (const auto &point : stroke->points)
                points.append(map(point));
        QVariantMap result{{"points", points},
                           {"radius", (stroke ? stroke->radius : retouchRadius_) *
                                          std::min(image.width, image.height) / geometry.width()}};
        if (stroke && (stroke->mode != luma::RetouchMode::remove || !retouchDraft_))
            result["source"] = map({stroke->points.front()[0] + stroke->sourceOffsetX,
                                    stroke->points.front()[1] + stroke->sourceOffsetY});
        else if (retouchSource_ && !stroke)
            result["source"] = map(*retouchSource_);
        return result;
    } catch (const std::exception &) {
        return {};
    }
}
