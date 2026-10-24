#include "apps/desktop/EditorController.h"
#include "core/lens/LensCorrection.h"
#include "core/render/AdvancedColor.h"
#include "core/render/CropGeometry.h"
#include "core/render/DevelopPipeline.h"
#include <algorithm>
#include <cmath>

QVariantMap EditorController::curvePoints() const {
    QVariantMap result;
    for (std::size_t c = 0; c < luma::curveChannels.size(); ++c) {
        QVariantList points;
        for (const auto &point : history_.current().curves[c].points)
            points.append(
                QVariantMap{{QStringLiteral("x"), point.x}, {QStringLiteral("y"), point.y}});
        result.insert(QString::fromUtf8(luma::curveChannels[c].data(),
                                        static_cast<qsizetype>(luma::curveChannels[c].size())),
                      points);
    }
    return result;
}
void EditorController::advancedFailure(const std::exception &failure) {
    error_ = QString::fromUtf8(failure.what());
    emit stateChanged();
}
void EditorController::applyAdvanced(luma::EditDocument next) {
    clearPreviewSettings();
    next.validate();
    validateGeometry(next);
    if (next != history_.current()) {
        history_.apply(std::move(next));
        editsChanged();
    }
}
int EditorController::addCurvePoint(const QString &channel, double x, double y) {
    if (!editable())
        return -1;
    try {
        auto next = history_.current();
        auto &points = next.curves[luma::EditDocument::curveChannel(channel.toStdString())].points;
        if (!std::isfinite(x) || !std::isfinite(y) || x <= 0 || x >= 1 || y < 0 || y > 1 ||
            points.size() >= 32)
            throw std::runtime_error(
                "Curve point is out of range or the 32-point limit was reached.");
        const auto location =
            std::lower_bound(points.begin(), points.end(), x,
                             [](const luma::CurvePoint &p, double value) { return p.x < value; });
        const auto index = static_cast<int>(location - points.begin());
        points.insert(location, {x, y});
        applyAdvanced(std::move(next));
        return index;
    } catch (const std::exception &failure) {
        advancedFailure(failure);
        return -1;
    }
}
void EditorController::moveCurvePoint(const QString &channel, int index, double x, double y) {
    if (!editable())
        return;
    try {
        auto next = history_.current();
        auto &points = next.curves[luma::EditDocument::curveChannel(channel.toStdString())].points;
        if (index < 0 || static_cast<std::size_t>(index) >= points.size() || !std::isfinite(x) ||
            !std::isfinite(y))
            throw std::runtime_error("Invalid curve point index or coordinates.");
        const auto i = static_cast<std::size_t>(index);
        const double lower = i > 0 ? points[i - 1].x + 0.001 : 0;
        const double upper = i + 1 < points.size() ? points[i + 1].x - 0.001 : 1;
        points[i] = {i == 0                   ? 0
                     : i + 1 == points.size() ? 1
                     : lower <= upper         ? std::clamp(x, lower, upper)
                                              : points[i].x,
                     std::clamp(y, 0.0, 1.0)};
        applyAdvanced(std::move(next));
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
void EditorController::removeCurvePoint(const QString &channel, int index) {
    if (!editable())
        return;
    try {
        auto next = history_.current();
        auto &points = next.curves[luma::EditDocument::curveChannel(channel.toStdString())].points;
        if (index <= 0 || static_cast<std::size_t>(index) + 1 >= points.size())
            throw std::runtime_error("Only interior curve points can be deleted.");
        points.erase(points.begin() + index);
        history_.end();
        applyAdvanced(std::move(next));
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
void EditorController::resetCurve(const QString &channel) {
    if (!editable())
        return;
    try {
        auto next = history_.current();
        next.curves[luma::EditDocument::curveChannel(channel.toStdString())] = {};
        history_.end();
        applyAdvanced(std::move(next));
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
QVariantList EditorController::curveSamples(const QString &channel, bool parametric) const {
    QVariantList values;
    const auto &curve =
        history_.current().curves[luma::EditDocument::curveChannel(channel.toStdString())];
    const luma::ToneCurve compiled(curve);
    for (int i = 0; i <= 128; ++i) {
        const double x = static_cast<double>(i) / 128;
        values.append(parametric ? luma::parametricCurve(x, history_.current())
                                 : compiled.value(x));
    }
    return values;
}
void EditorController::setTargetedTool(const QString &tool) {
    if (tool != QStringLiteral("none")) {
        setRetouchMode(QStringLiteral("off"));
        cancelAI();
    }
    if (tool != QStringLiteral("none"))
        setLocalMode(false);
    closeTransformTool();
    setCropMode(false);
    if (tool == targetedTool_)
        return;
    try {
        if (tool.startsWith(QStringLiteral("curve.")))
            luma::EditDocument::curveChannel(tool.mid(6).toStdString());
        else if (tool != QStringLiteral("none") && tool != QStringLiteral("hsl.hue") &&
                 tool != QStringLiteral("hsl.saturation") &&
                 tool != QStringLiteral("hsl.luminance"))
            throw std::runtime_error("Unknown targeted adjustment tool.");
        endEdit();
        targetedTool_ = tool;
        emit stateChanged();
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
bool EditorController::beginTargeted(double x, double y) {
    if (!editable() || original_ || cropMode_ || guidedMode_ ||
        targetedTool_ == QStringLiteral("none"))
        return false;
    try {
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
            throw std::runtime_error("Target must be inside the displayed image.");
        const bool curve = targetedTool_.startsWith(QStringLiteral("curve."));
        if (!history_.current().enabled(curve ? luma::Correction::curves : luma::Correction::hsl))
            return false;
        const luma::LensCorrectionPass optics(*session_->preview, history_.current(),
                                              session_->asset.metadata);
        const auto &image = optics.image();
        const luma::CropGeometry geometry(image.width, image.height,
                                          session_->asset.metadata.orientation, history_.current());
        const int px = std::min(static_cast<int>(x * geometry.width()), geometry.width() - 1);
        const int py = std::min(static_cast<int>(y * geometry.height()), geometry.height() - 1);
        if (!geometry.covered(px, py))
            return false;
        const auto mapped = geometry.source(px, py);
        const int sx = std::clamp(static_cast<int>(std::lround(mapped[0])), 0, image.width - 1);
        const int sy = std::clamp(static_cast<int>(std::lround(mapped[1])), 0, image.height - 1);
        const auto offset = (static_cast<std::size_t>(sy) * image.width + sx) * 3;
        auto sampling = history_.current();
        sampling.setEnabled(luma::Correction::hsl, false);
        if (curve)
            sampling.setEnabled(luma::Correction::curves, false);
        const auto pixel =
            luma::DevelopPipeline(sampling, session_->calibration)
                .pixel({image.rgb[offset], image.rgb[offset + 1], image.rgb[offset + 2]});
        std::array<double, 3> encoded{luma::encodeSRGB(pixel[0]), luma::encodeSRGB(pixel[1]),
                                      luma::encodeSRGB(pixel[2])};
        TargetGesture target;
        target.base = history_.current();
        if (curve) {
            target.channel = luma::EditDocument::curveChannel(targetedTool_.mid(6).toStdString());
            double value = target.channel == 0
                               ? 0.2126 * encoded[0] + 0.7152 * encoded[1] + 0.0722 * encoded[2]
                               : encoded[target.channel - 1];
            value = luma::parametricCurve(value, target.base);
            if (target.channel != 0)
                value = luma::ToneCurve(target.base.curves[0]).value(value);
            target.sample = std::clamp(value, 0.001, 0.999);
        } else {
            const double minimum = std::min(0.0, *std::min_element(encoded.begin(), encoded.end()));
            const double scale =
                std::max(1.0, *std::max_element(encoded.begin(), encoded.end()) - minimum);
            for (auto &value : encoded)
                value = (value - minimum) / scale;
            const auto color = luma::toHSL(encoded);
            if (color.saturation < 0.01)
                return false;
            target.weights = luma::hueWeights(color.hue);
            target.parameter = targetedTool_.mid(4);
        }
        endEdit();
        history_.begin();
        targeted_ = std::move(target);
        return true;
    } catch (const std::exception &failure) {
        advancedFailure(failure);
        return false;
    }
}
void EditorController::adjustTargeted(double delta) {
    if (!editable() || original_ || !targeted_)
        return;
    try {
        if (!std::isfinite(delta))
            throw std::runtime_error("Invalid targeted adjustment.");
        auto next = targeted_->base;
        if (delta != 0) {
            if (targeted_->parameter.isEmpty()) {
                auto &points = next.curves[targeted_->channel].points;
                auto nearest = std::min_element(points.begin(), points.end(),
                                                [this](const auto &a, const auto &b) {
                                                    return std::abs(a.x - targeted_->sample) <
                                                           std::abs(b.x - targeted_->sample);
                                                });
                if (std::abs(nearest->x - targeted_->sample) < 0.02)
                    nearest->y = std::clamp(nearest->y + delta, 0.0, 1.0);
                else {
                    const auto y = std::clamp(
                        luma::ToneCurve(next.curves[targeted_->channel]).value(targeted_->sample) +
                            delta,
                        0.0, 1.0);
                    const auto position =
                        std::lower_bound(points.begin(), points.end(), targeted_->sample,
                                         [](const auto &p, double value) { return p.x < value; });
                    points.insert(position, {targeted_->sample, y});
                }
            } else {
                for (std::size_t band = 0; band < luma::hslBands.size(); ++band) {
                    const auto parameter = "hsl." + std::string(luma::hslBands[band]) + "." +
                                           targeted_->parameter.toStdString();
                    next.setParameter(parameter,
                                      std::clamp(targeted_->base.parameter(parameter) +
                                                     delta * 100 * targeted_->weights[band],
                                                 -100.0, 100.0));
                }
            }
        }
        applyAdvanced(std::move(next));
    } catch (const std::exception &failure) {
        advancedFailure(failure);
    }
}
void EditorController::endTargeted() {
    // Loading another photo already ends the gesture before hiding the preview.
    // Its MouseArea cancellation must not re-enter stateChanged (or end a later edit).
    if (targeted_)
        endEdit();
}
