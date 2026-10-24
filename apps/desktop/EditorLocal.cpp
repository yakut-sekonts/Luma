#include "apps/desktop/EditorController.h"
#include "core/catalog/Identity.h"
#include "core/render/MaskReference.h"
#include <QColor>
#include <QPointer>
#include <algorithm>
#include <cmath>
#include <numbers>

int EditorController::localIndex() const {
    const auto &masks = history_.current().localMasks;
    const auto found = std::find_if(masks.begin(), masks.end(), [&](const auto &mask) {
        return mask.id == localSelected_.toStdString();
    });
    return found == masks.end() ? -1 : static_cast<int>(found - masks.begin());
}
QVariantList EditorController::localMasks() const {
    QVariantList result;
    for (const auto &mask : history_.current().localMasks)
        result.append(QVariantMap{{"id", QString::fromStdString(mask.id)},
                                  {"name", QString::fromStdString(mask.name)},
                                  {"enabled", mask.enabled},
                                  {"shape", localShapeKey(mask.shape)}});
    return result;
}
QVariantMap EditorController::localState() const {
    const int index = localIndex();
    if (index < 0)
        return {};
    const auto &mask = history_.current().localMasks[static_cast<std::size_t>(index)];
    const auto &geometry = localGeometry(mask);
    const int component = localComponentIndex(mask);
    QVariantMap state{
        {"id", QString::fromStdString(mask.id)},
        {"name", QString::fromStdString(mask.name)},
        {"shape", localShapeKey(geometry.shape)},
        {"enabled", mask.enabled},
        {"inverted", mask.inverted},
        {"feather", geometry.feather},
        {"opacity", mask.opacity},
        {"angle", geometry.angle},
        {"rangeLow", geometry.rangeLow * 100},
        {"rangeHigh", geometry.rangeHigh * 100},
        {"colorTolerance", geometry.colorTolerance * 100},
        {"referenceColor", QColor::fromRgbF(geometry.referenceColor[0], geometry.referenceColor[1],
                                            geometry.referenceColor[2])
                               .name()},
        {"strokeCount", static_cast<int>(geometry.strokes.size())},
        {"componentID",
         component < 0
             ? QString()
             : QString::fromStdString(mask.components[static_cast<std::size_t>(component)].id)},
        {"componentIndex", component},
        {"componentEnabled",
         component < 0 ? true : mask.components[static_cast<std::size_t>(component)].enabled},
        {"componentInverted",
         component < 0 ? false : mask.components[static_cast<std::size_t>(component)].inverted},
        {"componentOperation",
         component < 0
             ? 0
             : static_cast<int>(mask.components[static_cast<std::size_t>(component)].operation)},
        {"index", index}};
    for (std::size_t i = 0; i < luma::localParameters.size(); ++i)
        state[QString::fromUtf8(luma::localParameters[i])] = mask.adjustments[i];
    return state;
}
luma::MaskOverlay EditorController::localRenderOverlay() const {
    luma::MaskOverlay result;
    if (!localMode_ || !localOverlay_ || original_ || presetPreview_ ||
        diagnostic_ != luma::DetailDiagnostic::none)
        return result;
    const QColor color(localOverlayColor_);
    result.id = localSelected_.toStdString();
    result.color = {color.redF(), color.greenF(), color.blueF()};
    result.opacity = localOverlayOpacity_;
    return result;
}
void EditorController::setLocalMode(bool value) {
    if (value == localMode_ || (value && (!editable() || original_)))
        return;
    endEdit();
    if (value) {
        setRetouchMode(QStringLiteral("off"));
        cancelAI();
        setCropMode(false);
        closeTransformTool();
        targetedTool_ = QStringLiteral("none");
        clearPreviewSettings();
    }
    localMode_ = value;
    localCanvasReady_ = false;
    render();
    emit stateChanged();
}
void EditorController::setLocalOverlay(bool value) {
    if (localOverlay_ == value)
        return;
    localOverlay_ = value;
    render();
    emit stateChanged();
}
void EditorController::setLocalOverlayColor(const QString &value) {
    const QColor color(value);
    if (!color.isValid())
        return;
    localOverlayColor_ = color.name();
    render();
    emit stateChanged();
}
void EditorController::setLocalOverlayOpacity(double value) {
    if (!std::isfinite(value) || value < 0 || value > 1)
        return;
    localOverlayOpacity_ = value;
    render();
    emit stateChanged();
}
QString EditorController::addLocalMask(const QString &shape) {
    if (!editable() || original_)
        return {};
    try {
        endEdit();
        auto next = history_.current();
        luma::LocalMask mask;
        static_cast<luma::MaskGeometry &>(mask) = makeLocalGeometry(shape);
        mask.id = luma::newID();
        const std::array<const char *, 5> names{"Linear gradient", "Radial gradient", "Brush",
                                                "Luminance range", "Color range"};
        mask.name = QCoreApplication::translate("Luma", names[static_cast<std::size_t>(mask.shape)])
                        .toStdString();
        next.localMasks.push_back(mask);
        next.validate();
        localSelected_ = QString::fromStdString(mask.id);
        localComponentSelected_.clear();
        setLocalMode(true);
        applyAdvanced(std::move(next));
        return localSelected_;
    } catch (const std::exception &failure) {
        advancedFailure(failure);
        return {};
    }
}
void EditorController::selectLocalMask(const QString &id) {
    if (!editable())
        return;
    const auto &masks = history_.current().localMasks;
    if (std::none_of(masks.begin(), masks.end(),
                     [&](const auto &mask) { return mask.id == id.toStdString(); }))
        return;
    endEdit();
    localSelected_ = id;
    localComponentSelected_.clear();
    render();
    emit stateChanged();
}
void EditorController::removeLocalMask() {
    if (!editable() || localIndex() < 0)
        return;
    endEdit();
    auto next = history_.current();
    next.localMasks.erase(next.localMasks.begin() + localIndex());
    localSelected_ =
        next.localMasks.empty() ? QString() : QString::fromStdString(next.localMasks.back().id);
    applyAdvanced(std::move(next));
}
void EditorController::moveLocalMask(int direction) {
    if (!editable() || localIndex() < 0 || (direction != -1 && direction != 1))
        return;
    const int index = localIndex(), target = index + direction;
    if (target < 0 || static_cast<std::size_t>(target) >= history_.current().localMasks.size())
        return;
    endEdit();
    auto next = history_.current();
    std::swap(next.localMasks[static_cast<std::size_t>(index)],
              next.localMasks[static_cast<std::size_t>(target)]);
    applyAdvanced(std::move(next));
}
void EditorController::renameLocalMask(const QString &name) {
    if (!editable() || localIndex() < 0)
        return;
    try {
        endEdit();
        auto next = history_.current();
        next.localMasks[static_cast<std::size_t>(localIndex())].name = name.trimmed().toStdString();
        applyAdvanced(std::move(next));
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
void EditorController::setLocalEnabled(bool enabled) {
    if (!editable() || localIndex() < 0)
        return;
    endEdit();
    auto next = history_.current();
    next.localMasks[static_cast<std::size_t>(localIndex())].enabled = enabled;
    applyAdvanced(std::move(next));
}
void EditorController::setLocalInverted(bool inverted) {
    if (!editable() || localIndex() < 0)
        return;
    endEdit();
    auto next = history_.current();
    next.localMasks[static_cast<std::size_t>(localIndex())].inverted = inverted;
    applyAdvanced(std::move(next));
}
void EditorController::setLocalParameter(const QString &name, double value) {
    if (!editable() || localIndex() < 0)
        return;
    try {
        auto next = history_.current();
        auto &mask = next.localMasks[static_cast<std::size_t>(localIndex())];
        const auto key = name.toStdString();
        if (key == "opacity" ||
            std::find(luma::localParameters.begin(), luma::localParameters.end(), key) !=
                luma::localParameters.end())
            mask.setParameter(key, value);
        else
            localGeometry(mask).setParameter(key, value);
        applyAdvanced(std::move(next));
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
namespace {
bool viewCovered(const luma::CropGeometry &geometry, double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
        return false;
    return geometry.covered(std::clamp(x * geometry.width() - .5, 0.0, geometry.width() - 1.0),
                            std::clamp(y * geometry.height() - .5, 0.0, geometry.height() - 1.0));
}
std::array<double, 2> sourcePoint(const luma::CropGeometry &geometry, int width, int height,
                                  double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
        throw std::runtime_error("Mask gesture is outside the photo.");
    const auto px = std::clamp(x * geometry.width() - 0.5, 0.0, geometry.width() - 1.0),
               py = std::clamp(y * geometry.height() - 0.5, 0.0, geometry.height() - 1.0);
    if (!geometry.covered(px, py))
        throw std::runtime_error("Mask gesture is outside image coverage.");
    const auto point = geometry.source(px, py);
    return {(point[0] + 0.5) / width, (point[1] + 0.5) / height};
}
std::array<double, 2> proxySourcePoint(const luma::CropGeometry &geometry, int width, int height,
                                       double x, double y) {
    const auto point = geometry.sourceUnbounded(x, y);
    const std::array<double, 2> normalized{(point[0] + 0.5) / width, (point[1] + 0.5) / height};
    if (!std::isfinite(normalized[0]) || !std::isfinite(normalized[1]))
        throw std::runtime_error("Non-finite mask gesture point.");
    return normalized;
}
} // namespace
bool EditorController::beginLocalGesture(int handle, double x, double y, bool proxy) {
    if (!editable() || !localCanvasReady() || localIndex() < 0 || handle < -1 || handle > 2)
        return false;
    try {
        endEdit();
        const auto &selected =
            localGeometry(history_.current().localMasks[static_cast<std::size_t>(localIndex())]);
        if (selected.shape == luma::MaskShape::color ||
            selected.shape == luma::MaskShape::luminance ||
            selected.shape == luma::MaskShape::bitmap)
            return false;
        // Only an existing gradient handle may use the expanded overlay.
        // Brush, range sampling and a new gradient retain photo/coverage bounds.
        if (proxy && (handle < 0 || (selected.shape != luma::MaskShape::linear &&
                                     selected.shape != luma::MaskShape::radial)))
            return false;
        if (!std::isfinite(x) || !std::isfinite(y))
            return false;
        const auto &image = *session_->preview;
        luma::CropGeometry geometry(image.width, image.height, session_->asset.metadata.orientation,
                                    history_.current());
        if (selected.shape == luma::MaskShape::brush && !viewCovered(geometry, x, y))
            return false;
        // A proxy can be in the padding around a very thin photo. Anchor its
        // inverse inside the output, then preserve the full raw pointer delta.
        // Clamping each move instead would create an unresponsive padding area.
        const std::array<double, 2> outputAnchor{
            std::clamp(x * geometry.width() - 0.5, 0.0, geometry.width() - 1.0),
            std::clamp(y * geometry.height() - 0.5, 0.0, geometry.height() - 1.0)};
        const auto point = proxy ? proxySourcePoint(geometry, image.width, image.height,
                                                    outputAnchor[0], outputAnchor[1])
                                 : sourcePoint(geometry, image.width, image.height, x, y);
        localGesture_ =
            LocalGesture{history_.current().localMasks[static_cast<std::size_t>(localIndex())],
                         geometry,
                         handle,
                         point,
                         {},
                         proxy,
                         {x, y},
                         outputAnchor};
        history_.begin();
        if (selected.shape == luma::MaskShape::brush) {
            localGesture_->stroke = luma::BrushStroke{localBrushErase_,
                                                      localBrushSize_ / 200,
                                                      localBrushOpacity_,
                                                      {{point[0], point[1]}}};
            auto next = history_.current();
            localGeometry(next.localMasks[static_cast<std::size_t>(localIndex())])
                .strokes.push_back(*localGesture_->stroke);
            error_.clear();
            applyAdvanced(std::move(next));
        }
        return true;
    } catch (const std::exception &failure) {
        localGesture_.reset();
        history_.end();
        advancedFailure(failure);
        return false;
    }
}
void EditorController::adjustLocalGesture(double x, double y) {
    if (!editable() || !localGesture_ || localIndex() < 0)
        return;
    try {
        auto &gesture = *localGesture_;
        const auto &image = *session_->preview;
        if (gesture.stroke && !viewCovered(gesture.geometry, x, y)) {
            endEdit();
            return;
        }
        const auto point =
            gesture.proxy
                ? proxySourcePoint(gesture.geometry, image.width, image.height,
                                   gesture.outputAnchor[0] +
                                       (x - gesture.inputAnchor[0]) * gesture.geometry.width(),
                                   gesture.outputAnchor[1] +
                                       (y - gesture.inputAnchor[1]) * gesture.geometry.height())
                : sourcePoint(gesture.geometry, image.width, image.height, x, y);
        auto mask = gesture.base;
        auto &primitive = localGeometry(mask);
        if (gesture.stroke) {
            auto &stroke = *gesture.stroke;
            const auto &last = stroke.points.back();
            if (std::hypot(point[0] - last.x, point[1] - last.y) < 1e-6)
                return;
            if (stroke.points.size() >= 16384)
                throw std::runtime_error("Brush point limit exceeded.");
            stroke.points.push_back({point[0], point[1]});
            primitive.strokes.push_back(stroke);
            auto next = history_.current();
            localGeometry(next.localMasks[static_cast<std::size_t>(localIndex())]) = primitive;
            applyAdvanced(std::move(next));
            return;
        }
        const double dx = point[0] - gesture.anchor[0], dy = point[1] - gesture.anchor[1];
        if (gesture.handle == -1) {
            primitive.x1 = gesture.anchor[0];
            primitive.y1 = gesture.anchor[1];
            if (primitive.shape == luma::MaskShape::linear) {
                primitive.x2 = point[0];
                primitive.y2 = point[1];
            } else {
                primitive.x2 = std::abs(dx);
                primitive.y2 = std::abs(dy);
                primitive.angle = 0;
            }
        } else if (primitive.shape == luma::MaskShape::linear) {
            if (gesture.handle == 0 || gesture.handle == 2) {
                primitive.x1 += dx;
                primitive.y1 += dy;
            }
            if (gesture.handle == 1 || gesture.handle == 2) {
                primitive.x2 += dx;
                primitive.y2 += dy;
            }
        } else if (gesture.handle == 0) {
            primitive.x1 += dx;
            primitive.y1 += dy;
        } else {
            const double radians = primitive.angle * std::numbers::pi / 180;
            if (gesture.proxy) {
                // A displayed proxy is not the stored radius endpoint. Apply
                // the source delta along its original local axis without a snap.
                if (gesture.handle == 1)
                    primitive.x2 =
                        std::abs(primitive.x2 + std::cos(radians) * dx + std::sin(radians) * dy);
                else
                    primitive.y2 =
                        std::abs(primitive.y2 - std::sin(radians) * dx + std::cos(radians) * dy);
            } else {
                const double px = point[0] - primitive.x1, py = point[1] - primitive.y1;
                if (gesture.handle == 1)
                    primitive.x2 = std::abs(std::cos(radians) * px + std::sin(radians) * py);
                else
                    primitive.y2 = std::abs(-std::sin(radians) * px + std::cos(radians) * py);
            }
        }
        // Near-zero drags keep the previous valid geometry, without partial writes.
        if ((primitive.shape == luma::MaskShape::linear &&
             std::hypot(primitive.x2 - primitive.x1, primitive.y2 - primitive.y1) < 0.001) ||
            (primitive.shape == luma::MaskShape::radial &&
             (primitive.x2 < 0.001 || primitive.y2 < 0.001)))
            return;
        mask.validate();
        auto next = history_.current();
        localGeometry(next.localMasks[static_cast<std::size_t>(localIndex())]) = primitive;
        applyAdvanced(std::move(next));
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
QVariantList EditorController::localHandles() const {
    QVariantList result;
    if (!session_ || !localMode_ || localIndex() < 0)
        return result;
    try {
        const auto &mask =
            localGeometry(history_.current().localMasks[static_cast<std::size_t>(localIndex())]);
        if (mask.shape != luma::MaskShape::linear && mask.shape != luma::MaskShape::radial)
            return {};
        const auto &image = *session_->preview;
        const luma::CropGeometry geometry(image.width, image.height,
                                          session_->asset.metadata.orientation, history_.current());
        std::array<std::array<double, 2>, 3> points;
        if (mask.shape == luma::MaskShape::linear)
            points = {{{mask.x1, mask.y1},
                       {mask.x2, mask.y2},
                       {(mask.x1 + mask.x2) / 2, (mask.y1 + mask.y2) / 2}}};
        else {
            const double r = mask.angle * std::numbers::pi / 180;
            points = {{{mask.x1, mask.y1},
                       {mask.x1 + std::cos(r) * mask.x2, mask.y1 + std::sin(r) * mask.x2},
                       {mask.x1 - std::sin(r) * mask.y2, mask.y1 + std::cos(r) * mask.y2}}};
        }
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto point = geometry.output(points[i][0] * image.width - 0.5,
                                               points[i][1] * image.height - 0.5);
            const bool covered =
                std::isfinite(point[0]) && std::isfinite(point[1]) && point[0] >= 0 &&
                point[1] >= 0 && point[0] <= geometry.width() - 1 &&
                point[1] <= geometry.height() - 1 && geometry.covered(point[0], point[1]);
            result.append(QVariantMap{{"x", (point[0] + 0.5) / geometry.width()},
                                      {"y", (point[1] + 0.5) / geometry.height()},
                                      {"covered", covered},
                                      {"handle", static_cast<int>(i)}});
        }
    } catch (const std::exception &) {
        return {};
    }
    return result;
}
void EditorController::pickLocalRange(double x, double y) {
    if (!editable() || original_ || !localCanvasReady() || localIndex() < 0)
        return;
    try {
        endEdit();
        const auto &mask = history_.current().localMasks[static_cast<std::size_t>(localIndex())];
        const auto &primitive = localGeometry(mask);
        if (primitive.shape != luma::MaskShape::color &&
            primitive.shape != luma::MaskShape::luminance)
            return;
        const auto image = session_->preview;
        const auto edits = history_.current();
        const auto metadata = session_->asset.metadata;
        const luma::CropGeometry geometry(image->width, image->height, metadata.orientation, edits);
        const auto point = sourcePoint(geometry, image->width, image->height, x, y);
        const auto calibration = session_->calibration;
        const auto scale = session_->previewScale;
        const auto id = session_->asset.id;
        const auto selection = localSelected_, component = localComponentSelected_;
        const auto revision = ++localSampleRevision_;
        localSampling_ = true;
        error_.clear();
        emit stateChanged();
        const QPointer<EditorController> self(this);
        localSampleJob_ = renderJobs_.submit(
            luma::JobPriority::interactive,
            [self, image, edits, metadata, point, calibration, scale, id, selection, component,
             revision](const luma::JobContext &context) {
                try {
                    const auto color =
                        luma::sampleMaskReference(*image, edits, metadata, calibration, point[0],
                                                  point[1], scale, context.stopToken());
                    if (self)
                        QMetaObject::invokeMethod(
                            self,
                            [self, edits, id, selection, component, revision, color] {
                                if (!self || self->localSampleRevision_ != revision)
                                    return;
                                self->localSampling_ = false;
                                if (!self->session_ || self->session_->asset.id != id ||
                                    self->localSelected_ != selection ||
                                    self->localComponentSelected_ != component ||
                                    self->history_.current() != edits) {
                                    emit self->stateChanged();
                                    return;
                                }
                                try {
                                    auto next = edits;
                                    auto &g = self->localGeometry(
                                        next.localMasks[static_cast<std::size_t>(
                                            self->localIndex())]);
                                    if (g.shape == luma::MaskShape::color)
                                        g.referenceColor = luma::maskReferenceColor(color);
                                    else {
                                        const double value = luma::maskReferenceLuminance(color),
                                                     half = std::max(
                                                         .025, (g.rangeHigh - g.rangeLow) / 2);
                                        g.rangeLow = std::max(0.0, value - half);
                                        g.rangeHigh = std::min(1.0, value + half);
                                    }
                                    self->error_.clear();
                                    self->applyAdvanced(std::move(next));
                                } catch (const std::exception &failure) {
                                    self->advancedFailure(failure);
                                }
                                emit self->stateChanged();
                            },
                            Qt::QueuedConnection);
                } catch (const std::exception &failure) {
                    if (context.stopToken().isCancelled())
                        return;
                    const QString message = QString::fromUtf8(failure.what());
                    if (self)
                        QMetaObject::invokeMethod(
                            self,
                            [self, revision, message] {
                                if (self && self->localSampleRevision_ == revision) {
                                    self->localSampling_ = false;
                                    self->error_ = message;
                                    emit self->stateChanged();
                                }
                            },
                            Qt::QueuedConnection);
                }
            });
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
QVariantList EditorController::localBrushOutline(double x, double y) const {
    QVariantList result;
    if (!session_ || !localMode_ || localIndex() < 0 ||
        localGeometry(history_.current().localMasks[static_cast<std::size_t>(localIndex())])
                .shape != luma::MaskShape::brush)
        return result;
    try {
        const auto &image = *session_->preview;
        const luma::CropGeometry geometry(image.width, image.height,
                                          session_->asset.metadata.orientation, history_.current());
        const auto point = sourcePoint(geometry, image.width, image.height, x, y);
        const auto &metadata = session_->asset.metadata;
        const double aspect = metadata.width > 0 && metadata.height > 0
                                  ? static_cast<double>(metadata.width) / metadata.height
                                  : static_cast<double>(image.width) / image.height;
        const double rx = localBrushSize_ / 200 / std::max(1.0, aspect),
                     ry = localBrushSize_ / 200 / std::max(1.0, 1 / aspect);
        for (int i = 0; i < 32; ++i) {
            const double angle = i * 2 * std::numbers::pi / 32;
            const auto target =
                geometry.output(point[0] * image.width - .5 + std::cos(angle) * rx * image.width,
                                point[1] * image.height - .5 + std::sin(angle) * ry * image.height);
            result.append(QVariantMap{{"x", (target[0] + .5) / geometry.width()},
                                      {"y", (target[1] + .5) / geometry.height()}});
        }
    } catch (const std::exception &) {
        return {};
    }
    return result;
}
