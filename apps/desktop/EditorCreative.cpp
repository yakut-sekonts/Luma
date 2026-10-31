#include "apps/desktop/EditorController.h"
#include "core/catalog/Identity.h"
#include "core/image/Bayer.h"
#include "core/lens/LensCorrection.h"
#include "core/render/AutoAdjust.h"
#include "core/render/CreativeColor.h"
#include "core/render/Enhance.h"
#include "core/render/RetouchPass.h"
#include <QPointer>
#include <algorithm>
#include <cmath>

QVariantList EditorController::creativeParameters() const {
    QVariantList rows;
    for (const auto &p : luma::creativeParameters)
        rows.append(QVariantMap{{"name", QString::fromUtf8(p.name.data(), p.name.size())},
                                {"label", QString::fromUtf8(p.label.data(), p.label.size())},
                                {"minimum", p.minimum},
                                {"maximum", p.maximum},
                                {"initial", p.initial},
                                {"group", static_cast<int>(p.group)}});
    return rows;
}
void EditorController::cancelCreative() {
    creativeJob_.cancel();
    ++creativeRevision_;
    const bool changed = creativeBusy_ || pointColorPicking_;
    creativeBusy_ = false;
    pointColorPicking_ = false;
    pointColorCanvasReady_ = false;
    if (changed)
        emit stateChanged();
}
void EditorController::setPointColorRange(bool value) {
    if (pointColorRange_ == value)
        return;
    pointColorRange_ = value;
    render();
    emit stateChanged();
}
void EditorController::setPointColorPicking(bool value) {
    if (!value) {
        cancelCreative();
        return;
    }
    if (!editable() || original_ || exporting_)
        return;
    finishHistoryTools();
    targetedTool_ = QStringLiteral("none");
    setOneToOne(false);
    pointColorPicking_ = true;
    pointColorCanvasReady_ = false;
    render(true);
    emit stateChanged();
}
void EditorController::autoAdjust() {
    if (!editable() || original_ || exporting_)
        return;
    finishHistoryTools();
    runCreative(std::nullopt);
}
void EditorController::applyEnhance() {
    if (!editable() || !session_ || original_ || exporting_ || creativeBusy_)
        return;
    if (history_.current().creative.quickLooks.size() >= 32) {
        error_ = QStringLiteral("A photo supports at most 32 Quick Look layers.");
        emit stateChanged();
        return;
    }
    finishHistoryTools();
    creativeBusy_ = true;
    error_.clear();
    const auto revision = ++creativeRevision_, edit = editRevision_, load = loadRevision_,
               history = history_.revision();
    const auto &session = session_.value();
    const auto image = session.preview;
    const auto metadata = session.asset.metadata;
    const auto calibration = session.calibration;
    const auto current = history_.current();
    const QPointer<EditorController> self(this);
    try {
        creativeJob_ = creativeJobs_.submit(
            luma::JobPriority::interactive, [self, image, metadata, calibration, current, revision,
                                             edit, load, history](const luma::JobContext &context) {
                std::optional<luma::EnhanceResult> result;
                QString error;
                try {
                    result = luma::DeterministicEnhance{}.estimate(
                        *image, current, metadata, calibration, context.stopToken());
                } catch (const std::exception &failure) {
                    error = QString::fromUtf8(failure.what());
                }
                if (!self || context.stopToken().isCancelled())
                    return;
                QMetaObject::invokeMethod(
                    self,
                    [self, result = std::move(result), error, revision, edit, load,
                     history]() mutable {
                        if (!self || self->creativeRevision_ != revision)
                            return;
                        self->creativeBusy_ = false;
                        if (!self->editable() || self->original_ || self->editRevision_ != edit ||
                            self->loadRevision_ != load || self->history_.revision() != history ||
                            self->history_.gestureActive()) {
                            self->cancelCreative();
                            emit self->stateChanged();
                            return;
                        }
                        if (result) {
                            auto next = self->history_.current();
                            if (next.creative.quickLooks.size() >= 32) {
                                self->error_ = QStringLiteral(
                                    "A photo supports at most 32 Quick Look layers.");
                            } else {
                                next.creative.quickLooks.push_back(
                                    {luma::newID(),
                                     "",
                                     100,
                                     true,
                                     {},
                                     "AI Enhance · CPU",
                                     std::move(result->deltas),
                                     std::move(result->provider),
                                     std::move(result->providerVersion),
                                     std::move(result->model),
                                     std::move(result->modelDigest),
                                     result->deterministicFallback});
                                next.validate();
                                self->endEdit();
                                self->history_.apply(next, "AI Enhance");
                                self->editsChanged();
                            }
                        } else {
                            self->error_ = error;
                        }
                        emit self->stateChanged();
                    },
                    Qt::QueuedConnection);
            });
    } catch (const std::exception &failure) {
        creativeBusy_ = false;
        error_ = QString::fromUtf8(failure.what());
    }
    emit stateChanged();
}
void EditorController::samplePointColor(double x, double y) {
    if (!pointColorCanvasReady() || !editable() || original_ || !std::isfinite(x) ||
        !std::isfinite(y) || x < 0 || y < 0 || x > 1 || y > 1)
        return;
    runCreative(std::array{x, y});
}
void EditorController::runCreative(std::optional<std::array<double, 2>> point) {
    if (!editable() || creativeBusy_)
        return;
    creativeBusy_ = true;
    error_.clear();
    const auto revision = ++creativeRevision_, edit = editRevision_, load = loadRevision_,
               history = history_.revision();
    const auto image = session_->preview;
    const auto metadata = session_->asset.metadata;
    const auto calibration = session_->calibration;
    const auto current = history_.current();
    const QPointer<EditorController> self(this);
    try {
        creativeJob_ = creativeJobs_.submit(
            luma::JobPriority::interactive,
            [self, image, metadata, calibration, current, point, revision, edit, load,
             history](const luma::JobContext &context) {
                std::optional<luma::EditDocument> result;
                QString error;
                try {
                    if (!point)
                        result = luma::HistogramAutoAdjust{}.estimate(
                            *image, current, metadata, calibration, context.stopToken());
                    else {
                        const luma::LensCorrectionPass optics(*image, current, metadata,
                                                              context.stopToken());
                        const luma::RetouchPass retouch(optics.image(), current.retouch,
                                                        context.stopToken());
                        const auto &input = retouch.image();
                        const luma::CropGeometry geometry(input.width, input.height,
                                                          metadata.orientation, current);
                        const int x = std::clamp(static_cast<int>((*point)[0] * geometry.width()),
                                                 0, geometry.width() - 1);
                        const int y = std::clamp(static_cast<int>((*point)[1] * geometry.height()),
                                                 0, geometry.height() - 1);
                        auto reference = current;
                        reference.setEnabled(luma::Correction::pointColor, false);
                        reference.setEnabled(luma::Correction::grading, false);
                        const luma::DevelopPipeline pipeline(reference, calibration);
                        const auto rgb = geometry.sample(x, y, [&](int sx, int sy) {
                            const auto i = (static_cast<std::size_t>(sy) * input.width + sx) * 3;
                            auto c =
                                pipeline.pixel({input.rgb[i], input.rgb[i + 1], input.rgb[i + 2]});
                            for (auto &v : c)
                                v *= input.coverage(sx, sy);
                            return c;
                        });
                        const auto alpha = geometry.sample(x, y, [&](int sx, int sy) {
                            const auto a = input.coverage(sx, sy);
                            return std::array{a, a, a};
                        })[0];
                        if (alpha <= 1e-8)
                            throw std::runtime_error("Cannot sample a transparent pixel.");
                        const auto color =
                            luma::toHSL({std::clamp(luma::encodeSRGB(rgb[0] / alpha), 0.0, 1.0),
                                         std::clamp(luma::encodeSRGB(rgb[1] / alpha), 0.0, 1.0),
                                         std::clamp(luma::encodeSRGB(rgb[2] / alpha), 0.0, 1.0)});
                        result = current;
                        result->setParameters(
                            {{"point.referenceHue", color.hue},
                             {"point.referenceSaturation", color.saturation * 100},
                             {"point.referenceLuminance", color.luminance * 100}});
                        result->setEnabled(luma::Correction::pointColor, true);
                    }
                } catch (const std::exception &failure) {
                    error = QString::fromUtf8(failure.what());
                }
                if (!self || context.stopToken().isCancelled())
                    return;
                QMetaObject::invokeMethod(
                    self,
                    [self, result, error, point, revision, edit, load, history] {
                        if (!self || self->creativeRevision_ != revision)
                            return;
                        self->creativeBusy_ = false;
                        if (!self->editable() || self->original_ || self->editRevision_ != edit ||
                            self->loadRevision_ != load || self->history_.revision() != history ||
                            self->history_.gestureActive()) {
                            self->cancelCreative();
                            emit self->stateChanged();
                            return;
                        }
                        if (result) {
                            self->endEdit();
                            if (*result != self->history_.current()) {
                                self->history_.apply(*result,
                                                     point ? "Point Color sample" : "Auto Adjust");
                                self->editsChanged();
                            }
                        } else
                            self->error_ = error;
                        emit self->stateChanged();
                    },
                    Qt::QueuedConnection);
            });
    } catch (const std::exception &failure) {
        creativeBusy_ = false;
        error_ = QString::fromUtf8(failure.what());
    }
    emit stateChanged();
}
