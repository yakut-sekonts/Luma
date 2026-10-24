#include "apps/desktop/EditorController.h"
#include "core/catalog/Identity.h"
#include <algorithm>
#include <cmath>

QString EditorController::localShapeKey(luma::MaskShape shape) {
    const std::array<const char *, 6> keys{"linear",    "radial", "brush",
                                           "luminance", "color",  "bitmap"};
    return QString::fromLatin1(keys.at(static_cast<std::size_t>(shape)));
}
luma::MaskGeometry EditorController::makeLocalGeometry(const QString &shape) {
    luma::MaskGeometry result;
    if (shape == "linear")
        return result;
    if (shape == "radial") {
        result.shape = luma::MaskShape::radial;
        result.x1 = result.y1 = .5;
        result.x2 = .3;
        result.y2 = .25;
    } else if (shape == "brush") {
        result.shape = luma::MaskShape::brush;
        result.feather = 75;
    } else if (shape == "luminance") {
        result.shape = luma::MaskShape::luminance;
        result.feather = 50;
    } else if (shape == "color") {
        result.shape = luma::MaskShape::color;
        result.feather = 50;
    } else
        throw std::runtime_error("Unsupported mask shape.");
    return result;
}
int EditorController::localComponentIndex(const luma::LocalMask &mask) const {
    const auto id = localComponentSelected_.toStdString();
    const auto found = std::find_if(mask.components.begin(), mask.components.end(),
                                    [&](const auto &component) { return component.id == id; });
    return found == mask.components.end() ? -1 : static_cast<int>(found - mask.components.begin());
}
luma::MaskGeometry &EditorController::localGeometry(luma::LocalMask &mask) const {
    const int index = localComponentIndex(mask);
    return index < 0 ? static_cast<luma::MaskGeometry &>(mask)
                     : mask.components[static_cast<std::size_t>(index)].geometry;
}
const luma::MaskGeometry &EditorController::localGeometry(const luma::LocalMask &mask) const {
    const int index = localComponentIndex(mask);
    return index < 0 ? static_cast<const luma::MaskGeometry &>(mask)
                     : mask.components[static_cast<std::size_t>(index)].geometry;
}
QVariantList EditorController::localComponents() const {
    QVariantList result;
    if (localIndex() < 0)
        return result;
    const auto &mask = history_.current().localMasks[static_cast<std::size_t>(localIndex())];
    result.append(
        QVariantMap{{"id", QString()}, {"shape", localShapeKey(mask.shape)}, {"operation", -1}});
    for (const auto &component : mask.components)
        result.append(QVariantMap{{"id", QString::fromStdString(component.id)},
                                  {"shape", localShapeKey(component.geometry.shape)},
                                  {"operation", static_cast<int>(component.operation)}});
    return result;
}
namespace {
luma::MaskOperation operation(const QString &value) {
    if (value == "add")
        return luma::MaskOperation::add;
    if (value == "subtract")
        return luma::MaskOperation::subtract;
    if (value == "intersect")
        return luma::MaskOperation::intersect;
    throw std::runtime_error("Unsupported mask operation.");
}
} // namespace
QString EditorController::addLocalComponent(const QString &shape, const QString &op) {
    if (!editable() || original_ || localIndex() < 0)
        return {};
    try {
        endEdit();
        auto next = history_.current();
        luma::MaskComponent component;
        component.id = luma::newID();
        component.operation = operation(op);
        component.geometry = makeLocalGeometry(shape);
        next.localMasks[static_cast<std::size_t>(localIndex())].components.push_back(component);
        next.validate();
        localComponentSelected_ = QString::fromStdString(component.id);
        setLocalMode(true);
        error_.clear();
        applyAdvanced(std::move(next));
        return localComponentSelected_;
    } catch (const std::exception &failure) {
        advancedFailure(failure);
        return {};
    }
}
void EditorController::selectLocalComponent(const QString &id) {
    if (!editable() || localIndex() < 0)
        return;
    const auto &mask = history_.current().localMasks[static_cast<std::size_t>(localIndex())];
    if (!id.isEmpty() &&
        std::none_of(mask.components.begin(), mask.components.end(),
                     [&](const auto &component) { return component.id == id.toStdString(); }))
        return;
    endEdit();
    localComponentSelected_ = id;
    render();
    emit stateChanged();
}
void EditorController::removeLocalComponent() {
    if (!editable() || localIndex() < 0)
        return;
    auto next = history_.current();
    auto &mask = next.localMasks[static_cast<std::size_t>(localIndex())];
    const int index = localComponentIndex(mask);
    if (index < 0)
        return;
    endEdit();
    mask.components.erase(mask.components.begin() + index);
    localComponentSelected_.clear();
    applyAdvanced(std::move(next));
}
void EditorController::moveLocalComponent(int direction) {
    if (!editable() || localIndex() < 0 || (direction != -1 && direction != 1))
        return;
    auto next = history_.current();
    auto &mask = next.localMasks[static_cast<std::size_t>(localIndex())];
    const int index = localComponentIndex(mask), target = index + direction;
    if (index < 0 || target < 0 || static_cast<std::size_t>(target) >= mask.components.size())
        return;
    endEdit();
    std::swap(mask.components[static_cast<std::size_t>(index)],
              mask.components[static_cast<std::size_t>(target)]);
    applyAdvanced(std::move(next));
}
void EditorController::setLocalComponentEnabled(bool value) {
    if (!editable() || localIndex() < 0)
        return;
    auto next = history_.current();
    auto &mask = next.localMasks[static_cast<std::size_t>(localIndex())];
    const int index = localComponentIndex(mask);
    if (index < 0)
        return;
    endEdit();
    mask.components[static_cast<std::size_t>(index)].enabled = value;
    applyAdvanced(std::move(next));
}
void EditorController::setLocalComponentInverted(bool value) {
    if (!editable() || localIndex() < 0)
        return;
    auto next = history_.current();
    auto &mask = next.localMasks[static_cast<std::size_t>(localIndex())];
    const int index = localComponentIndex(mask);
    if (index < 0)
        return;
    endEdit();
    mask.components[static_cast<std::size_t>(index)].inverted = value;
    applyAdvanced(std::move(next));
}
void EditorController::setLocalComponentOperation(const QString &value) {
    if (!editable() || localIndex() < 0)
        return;
    try {
        auto next = history_.current();
        auto &mask = next.localMasks[static_cast<std::size_t>(localIndex())];
        const int index = localComponentIndex(mask);
        if (index < 0)
            return;
        const auto op = operation(value);
        endEdit();
        mask.components[static_cast<std::size_t>(index)].operation = op;
        applyAdvanced(std::move(next));
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
void EditorController::setLocalBrushSize(double value) {
    if (!std::isfinite(value) || value < .1 || value > 100)
        return;
    localBrushSize_ = value;
    emit stateChanged();
}
void EditorController::setLocalBrushOpacity(double value) {
    if (!std::isfinite(value) || value < 0 || value > 100)
        return;
    localBrushOpacity_ = value;
    emit stateChanged();
}
void EditorController::setLocalBrushErase(bool value) {
    localBrushErase_ = value;
    emit stateChanged();
}
void EditorController::clearLocalBrush() {
    if (!editable() || localIndex() < 0)
        return;
    auto next = history_.current();
    auto &geometry = localGeometry(next.localMasks[static_cast<std::size_t>(localIndex())]);
    if (geometry.shape != luma::MaskShape::brush || geometry.strokes.empty())
        return;
    endEdit();
    geometry.strokes.clear();
    applyAdvanced(std::move(next));
}
void EditorController::cancelLocalSample() {
    localSampleJob_.cancel();
    ++localSampleRevision_;
    localSampling_ = false;
}
