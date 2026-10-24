#include "apps/desktop/EditorController.h"
#include <algorithm>
#include <cmath>

void EditorController::publishHistogram(const std::optional<luma::ImageHistogram> &histogram) {
    histogram_.clear();
    histogramPending_ = false;
    if (histogram) {
        constexpr std::array<const char *, 4> names{"red", "green", "blue", "luminance"};
        for (std::size_t channel = 0; channel < names.size(); ++channel) {
            QVariantList values;
            values.reserve(256);
            for (const auto value : histogram->bins[channel])
                values.append(value);
            histogram_.insert(QString::fromLatin1(names[channel]), values);
        }
        histogram_.insert(QStringLiteral("weight"), histogram->weight);
        histogram_.insert(QStringLiteral("samples"), histogram->samples);
        histogram_.insert(QStringLiteral("shadowsPercent"),
                          histogram->weight > 0 ? 100 * histogram->shadows / histogram->weight : 0);
        histogram_.insert(QStringLiteral("highlightsPercent"),
                          histogram->weight > 0 ? 100 * histogram->highlights / histogram->weight
                                                : 0);
    }
    emit histogramChanged();
}
void EditorController::setShadowClipping(bool value) {
    if (shadowClipping_ == value)
        return;
    shadowClipping_ = value;
    refreshDetailDiagnostic();
    emit stateChanged();
}
void EditorController::setHighlightClipping(bool value) {
    if (highlightClipping_ == value)
        return;
    highlightClipping_ = value;
    refreshDetailDiagnostic();
    emit stateChanged();
}
bool EditorController::beginHistogramEdit(const QString &parameter) {
    if (!editable() || original_ || exporting_ || !deferredOpen_.isEmpty())
        return false;
    if (parameter != QStringLiteral("exposure") && parameter != QStringLiteral("highlights") &&
        parameter != QStringLiteral("shadows") && parameter != QStringLiteral("whites") &&
        parameter != QStringLiteral("blacks"))
        return false;
    const auto key = parameter.toStdString();
    if (!history_.current().enabled(luma::EditDocument::correction(key)))
        return false;
    finishHistoryTools();
    beginEdit();
    histogramGesture_ =
        HistogramGesture{parameter, history_.current().parameter(key), history_.revision()};
    if (parameter == QStringLiteral("whites") || parameter == QStringLiteral("blacks"))
        setDetailControl(parameter);
    return true;
}
void EditorController::updateHistogramEdit(double delta) {
    if (!histogramGesture_ || !editable() || original_ || !std::isfinite(delta))
        return;
    if (!history_.gestureActive() || history_.revision() != histogramGesture_->revision) {
        histogramGesture_.reset();
        return;
    }
    const auto parameter = histogramGesture_->parameter;
    if (!history_.current().enabled(luma::EditDocument::correction(parameter.toStdString())))
        return;
    const bool exposure = parameter == QStringLiteral("exposure");
    const double limit = exposure ? 5 : 100;
    const double scale = exposure ? 100 : 1;
    const double value = std::clamp(histogramGesture_->initial + delta, -limit, limit);
    setParameter(parameter, std::round(value * scale) / scale);
}
void EditorController::endHistogramEdit() {
    if (!histogramGesture_)
        return;
    if (history_.gestureActive() && history_.revision() == histogramGesture_->revision)
        endEdit();
    else
        histogramGesture_.reset();
}
