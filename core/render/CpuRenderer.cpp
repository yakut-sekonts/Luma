#include "core/render/CpuRenderer.h"
#if defined(LUMA_UX_CPU_RENDER_ROWS)
#include "tests/ux/CpuRenderRowProbe.h"
#endif
#include "core/color/DisplayTransform.h"
#include "core/image/Bayer.h"
#include "core/lens/LensCorrection.h"
#include "core/quick/QuickLooks.h"
#include "core/render/CreativeColor.h"
#include "core/render/CropGeometry.h"
#include "core/render/DevelopPipeline.h"
#include "core/render/LocalBlur.h"
#include "core/render/NoiseReduction.h"
#include "core/render/Presence.h"
#include "core/render/RetouchPass.h"
#include <algorithm>
#include <cmath>
#include <future>
#include <thread>

namespace luma {
#if !defined(LUMA_UX_CPU_RENDER_ROWS)
float applyExposure(float linearValue, double ev) {
    EditDocument{ev}.validate();
    return linearValue * static_cast<float>(std::exp2(ev));
}

FloatImage standardPreview(const FloatImage &input, int maximumDimension,
                           luma::CancellationToken stop) {
    if (maximumDimension < 1 || input.rgb.size() != pixelCount(input.width, input.height) * 3) {
        throw std::runtime_error("Invalid preview dimensions or input buffer.");
    }
    input.validateCoverage();
    const double scale =
        std::min(1.0, static_cast<double>(maximumDimension) / std::max(input.width, input.height));
    const int width = std::max(1, static_cast<int>(std::round(input.width * scale)));
    const int height = std::max(1, static_cast<int>(std::round(input.height * scale)));
    FloatImage result(width, height);
    if (!input.alpha.empty())
        result.alpha.resize(pixelCount(width, height));
    // Area averaging prevents the aliasing produced by subsampling a Bayer-derived image.
    for (int y = 0; y < height; ++y) {
        checkCancellation(stop);
        const int firstY = y * input.height / height;
        const int lastY = std::max(firstY + 1, (y + 1) * input.height / height);
        for (int x = 0; x < width; ++x) {
            const int firstX = x * input.width / width;
            const int lastX = std::max(firstX + 1, (x + 1) * input.width / width);
            const float count = static_cast<float>((lastX - firstX) * (lastY - firstY));
            const auto output = (static_cast<std::size_t>(y) * width + x) * 3;
            if (!input.alpha.empty()) {
                double coverage = 0;
                std::array<double, 3> sum{};
                for (int sy = firstY; sy < lastY; ++sy)
                    for (int sx = firstX; sx < lastX; ++sx) {
                        const auto i = static_cast<std::size_t>(sy) * input.width + sx;
                        const auto alpha = input.alpha[i];
                        coverage += alpha;
                        for (std::size_t c = 0; c < 3; ++c)
                            sum[c] += input.rgb[i * 3 + c] * alpha;
                    }
                result.alpha[output / 3] = static_cast<float>(coverage / count);
                for (std::size_t c = 0; c < 3; ++c)
                    result.rgb[output + c] =
                        coverage > 1e-8 ? static_cast<float>(sum[c] / coverage) : 0;
                continue;
            }
            for (int sy = firstY; sy < lastY; ++sy) {
                for (int sx = firstX; sx < lastX; ++sx) {
                    const auto offset = (static_cast<std::size_t>(sy) * input.width + sx) * 3;
                    for (int c = 0; c < 3; ++c) {
                        result.rgb[output + c] += input.rgb[offset + c] / count;
                    }
                }
            }
        }
    }
    return result;
}

#endif

#if defined(LUMA_UX_CPU_RENDER_ROWS)
DisplayImage renderCPUWithUxRows(ux_test::CpuRowObserver &observer, const FloatImage &source,
                                 EditDocument edits, int orientation,
#else
DisplayImage renderCPU(const FloatImage &source, EditDocument edits, int orientation,
#endif
                                 std::span<const std::uint8_t> displayProfile,
                                 luma::CancellationToken stop, RawColorCalibration calibration,
                                 DetailDiagnostic diagnostic, double detailScale,
                                 PhotoMetadata metadata, MaskOverlay overlay
#if !defined(LUMA_UX_CPU_RENDER_ROWS)
                                 ,
                                 DevelopAccelerator *accelerator, PreviewAnalysisOptions analysis,
                                 unsigned maximumWorkers, DisplayMapping displayMapping
#endif
) {
#if defined(LUMA_UX_CPU_RENDER_ROWS)
    const PreviewAnalysisOptions analysis{};
    constexpr unsigned maximumWorkers = 1;
    DisplayMapping displayMapping = DisplayMapping::clip;
#endif
    if (maximumWorkers < 1 || maximumWorkers > 4)
        throw std::runtime_error("Render worker budget must be between one and four.");
    if (static_cast<unsigned>(analysis.clipping) > 3)
        throw std::runtime_error("Invalid clipping preview mode.");
    source.validateCoverage();
    edits = resolveQuickLooks(edits);
    const LensCorrectionPass optics(source, edits, metadata, stop);
    const RetouchPass retouch(optics.image(), edits.retouch, stop);
    const auto &input = retouch.image();
    if (displayMapping == DisplayMapping::adaptiveHDR &&
        std::none_of(input.rgb.begin(), input.rgb.end(), [](float value) { return value > 1; }))
        displayMapping = DisplayMapping::clip;
    edits.validate();
    if (orientation < 0 || orientation > 7 ||
        input.rgb.size() != pixelCount(input.width, input.height) * 3) {
        throw std::runtime_error("Invalid render input or orientation.");
    }
    const CropGeometry geometry(input.width, input.height, orientation, edits);
    const int width = geometry.width(), height = geometry.height();
    DisplayImage result{width, height, std::vector<std::uint8_t>(pixelCount(width, height) * 4)};
    std::optional<HistogramSamplingGrid> histogramGrid;
    if (analysis.histogram && diagnostic == DetailDiagnostic::none) {
        result.histogram.emplace();
        histogramGrid.emplace(width, height);
    }
    const auto clipping =
        diagnostic == DetailDiagnostic::none ? analysis.clipping : ClippingPreview::none;
    const double sourceAspect = metadata.width > 0 && metadata.height > 0
                                    ? static_cast<double>(metadata.width) / metadata.height
                                    : static_cast<double>(input.width) / input.height;
    const LocalAdjustmentPass local(edits.localMasks, sourceAspect);
    const auto selected = std::find_if(edits.localMasks.begin(), edits.localMasks.end(),
                                       [&](const auto &mask) { return mask.id == overlay.id; });
    if (!std::isfinite(overlay.opacity) || overlay.opacity < 0 || overlay.opacity > 1 ||
        std::any_of(overlay.color.begin(), overlay.color.end(),
                    [](double v) { return !std::isfinite(v) || v < 0 || v > 1; }))
        throw std::runtime_error("Invalid mask overlay settings.");
    const DevelopPipeline pipeline(edits, calibration);
    std::vector<float> developedPixels;
#if !defined(LUMA_UX_CPU_RENDER_ROWS)
    if (accelerator && pipeline.gpuCompatible())
        developedPixels = accelerator->develop(input, pipeline, stop);
#endif
    if (!developedPixels.empty() && developedPixels.size() != input.rgb.size())
        throw std::runtime_error("Invalid accelerated Develop buffer.");
    const NoiseReductionPass noise(input, pipeline, edits, stop, detailScale, diagnostic, {},
                                   developedPixels);
    const SharpeningPass sharpening(input, pipeline, edits, stop, detailScale, diagnostic, {},
                                    &noise, developedPixels);
    const auto detailPixel = [&](int sx, int sy) {
        const auto offset = (static_cast<std::size_t>(sy) * input.width + sx) * 3;
        const auto developed =
            noise.active() ? noise.pixel({}, sx, sy)
            : !developedPixels.empty()
                ? std::array{developedPixels[offset], developedPixels[offset + 1],
                             developedPixels[offset + 2]}
                : pipeline.pixel({input.rgb[offset], input.rgb[offset + 1], input.rgb[offset + 2]});
        return sharpening.pixel(developed, sx, sy);
    };
    auto presenceEdits = edits;
    if (diagnostic != DetailDiagnostic::none)
        presenceEdits.setEnabled(Correction::presence, false);
    const PresencePass presence(input, presenceEdits, detailPixel, stop, detailScale);
    const auto referencePixel = [&](int x, int y) {
        return presence.pixel(detailPixel(x, y), x, y);
    };
    const bool hasLocalBlur =
        diagnostic == DetailDiagnostic::none &&
        std::any_of(edits.localMasks.begin(), edits.localMasks.end(), [](const auto &mask) {
            return mask.enabled && mask.opacity > 0 && mask.blur > 0;
        });
    const LocalBlurPass blur(input.width, input.height, hasLocalBlur, referencePixel, stop);
    auto referenceEdits = edits;
    referenceEdits.setEnabled(Correction::pointColor, false);
    referenceEdits.setEnabled(Correction::grading, false);
    const DevelopPipeline pointReference(referenceEdits, calibration);
    // Each stripe owns its ICC transform and scratch row. Only disjoint output
    // bytes are shared; the completed result is returned after every worker joins.
    // Preserve the exact serial histogram accumulation order, including tiny alpha.
    using HistogramSample = ImageHistogram::PreparedSample;
    const auto renderRows = [&](int firstRow, int lastRow) {
        std::vector<HistogramSample> samples;
        DisplayTransform transform(displayProfile);
        std::vector<float> row(static_cast<std::size_t>(width) * 3);
        std::vector<std::uint8_t> rowClipping(
            clipping == ClippingPreview::none ? 0 : static_cast<std::size_t>(width));
        for (int y = firstRow; y < lastRow; ++y) {
            checkCancellation(stop);
            for (int x = 0; x < width; ++x) {
                auto rgb = geometry.sample(x, y, [&](int sx, int sy) {
                    auto color = referencePixel(sx, sy);
                    if (diagnostic == DetailDiagnostic::none) {
                        if (blur.active()) {
                            const double amount = local.blur((sx + 0.5) / input.width,
                                                             (sy + 0.5) / input.height, color);
                            const auto softened = blur.pixel(sx, sy);
                            for (std::size_t c = 0; c < 3; ++c)
                                color[c] = static_cast<float>(color[c] +
                                                              (softened[c] - color[c]) * amount);
                        }
                        color =
                            local.pixel(color, (sx + 0.5) / input.width, (sy + 0.5) / input.height);
                    }
                    if (!input.alpha.empty())
                        for (auto &value : color)
                            value *= input.coverage(sx, sy);
                    return color;
                });
                const bool histogramSample = histogramGrid && histogramGrid->contains(x, y);
                float coverage = 1.F;
                if ((histogramSample || !rowClipping.empty()) && !geometry.opaque() &&
                    !geometry.covered(x, y))
                    coverage = 0.F;
                if (!input.alpha.empty()) {
                    const auto alpha = geometry.sample(x, y, [&](int sx, int sy) {
                        const auto value = input.coverage(sx, sy);
                        return std::array{value, value, value};
                    })[0];
                    for (auto &value : rgb)
                        value = alpha > 1e-8F ? value / alpha : 0;
                    coverage = std::clamp(alpha, 0.F, 1.F);
                }
                if (diagnostic == DetailDiagnostic::none)
                    rgb = applyOutputEffects(rgb, edits, x, y, width, height, detailScale);
                if (histogramSample)
                    samples.push_back(ImageHistogram::prepare(rgb, coverage));
                if (!rowClipping.empty())
                    rowClipping[static_cast<std::size_t>(x)] =
                        coverage > 0 ? clippingFlags(rgb) : 0;
                if (analysis.pointColorRange && diagnostic == DetailDiagnostic::none) {
                    const auto range = geometry.sample(x, y, [&](int sx, int sy) {
                        const auto i = (static_cast<std::size_t>(sy) * input.width + sx) * 3;
                        const auto color = pointReference.pixel(
                            {input.rgb[i], input.rgb[i + 1], input.rgb[i + 2]});
                        const auto a = input.coverage(sx, sy);
                        return std::array{
                            static_cast<float>(pointColorWeight(color, edits.creative)) * a, a,
                            0.F};
                    });
                    const float weight = range[1] > 1e-8F ? range[0] / range[1] : 0;
                    rgb = {weight, weight, weight};
                }
                for (std::size_t c = 0; c < 3; ++c)
                    row[static_cast<std::size_t>(x) * 3 + c] = rgb[c];
            }
            auto destination = std::span(result.rgba)
                                   .subspan(static_cast<std::size_t>(y) * width * 4,
                                            static_cast<std::size_t>(width) * 4);
            transform.convert(row, destination, displayMapping);
            if (!input.alpha.empty()) {
                for (int x = 0; x < width; ++x) {
                    const auto alpha = geometry.sample(x, y, [&](int sx, int sy) {
                        const auto value = input.coverage(sx, sy);
                        return std::array{value, value, value};
                    })[0];
                    destination[static_cast<std::size_t>(x) * 4 + 3] =
                        static_cast<std::uint8_t>(std::lround(std::clamp(alpha, 0.F, 1.F) * 255));
                }
            } else if (!geometry.opaque())
                for (int x = 0; x < width; ++x)
                    if (!geometry.covered(x, y))
                        destination[static_cast<std::size_t>(x) * 4 + 3] = 0;
            if (selected != edits.localMasks.end() && diagnostic == DetailDiagnostic::none)
                for (int x = 0; x < width; ++x) {
                    if (destination[static_cast<std::size_t>(x) * 4 + 3] == 0)
                        continue;
                    const auto maskSample = geometry.sample(x, y, [&](int sx, int sy) {
                        const auto a = static_cast<float>(local.coverage(
                            static_cast<std::size_t>(selected - edits.localMasks.begin()),
                            (sx + 0.5) / input.width, (sy + 0.5) / input.height,
                            referencePixel(sx, sy)));
                        const auto alpha = input.coverage(sx, sy);
                        return std::array{a * alpha, alpha, 0.F};
                    });
                    const double coverage =
                        maskSample[1] > 1e-8F
                            ? std::clamp(maskSample[0] / maskSample[1], 0.F, 1.F) * overlay.opacity
                            : 0;
                    for (std::size_t c = 0; c < 3; ++c) {
                        auto &value = destination[static_cast<std::size_t>(x) * 4 + c];
                        value = static_cast<std::uint8_t>(std::lround(
                            value * (1 - coverage) + 255 * overlay.color[c] * coverage));
                    }
                }
            if (!rowClipping.empty())
                for (int x = 0; x < width; ++x) {
                    const auto flags = rowClipping[static_cast<std::size_t>(x)] &
                                       static_cast<std::uint8_t>(clipping);
                    const auto offset = static_cast<std::size_t>(x) * 4;
                    if (!flags || destination[offset + 3] == 0)
                        continue;
                    // UI overlay colors, applied after ICC and mask overlays. Alpha
                    // and the clean histogram are preserved; exports opt out.
                    destination[offset] = flags & 2 ? 255 : 32;
                    destination[offset + 1] = flags & 2 ? 32 : 96;
                    destination[offset + 2] = flags & 2 ? 32 : 255;
                }
#if defined(LUMA_UX_CPU_RENDER_ROWS)
            // This is real converted output, after alpha/mask composition. The next
            // iteration retains the original cancellation check. Shipping has no hook.
            if (y == 0)
                observer.firstRowCompleted(result);
#endif
        }
        return samples;
    };
    const auto workers =
        pixelCount(width, height) < 65536
            ? 1U
            : std::min({maximumWorkers, std::max(1U, std::thread::hardware_concurrency()),
                        static_cast<unsigned>(height)});
    std::vector<std::future<std::vector<HistogramSample>>> pending;
    for (unsigned worker = 1; worker < workers; ++worker)
        pending.push_back(std::async(std::launch::async, renderRows,
                                     static_cast<int>(height * worker / workers),
                                     static_cast<int>(height * (worker + 1) / workers)));
    const auto addHistogram = [&](const std::vector<HistogramSample> &samples) {
        for (const auto &sample : samples) {
            checkCancellation(stop);
            result.histogram->add(sample);
        }
    };
    addHistogram(renderRows(0, height / static_cast<int>(workers)));
    for (auto &worker : pending)
        addHistogram(worker.get());
    return result;
}
} // namespace luma
