#include "platform/macos/ImageAlignment.h"
#include "core/image/Bayer.h"
#include "platform/macos/CFHandle.h"
#import <Vision/Vision.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace luma::macos {
namespace {
CFHandle<CGImageRef> image(ai::RgbaImage input) {
    input.validate();
    if (input.width > 1024 || input.height > 1024)
        throw std::runtime_error("Alignment reference exceeds its budget.");
    CFHandle color(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    CFHandle data(
        CGDataProviderCreateWithData(nullptr, input.pixels.data(), input.pixels.size(), nullptr));
    CFHandle result(
        CGImageCreate(input.width, input.height, 8, 32, input.width * 4, color.get(),
                      static_cast<CGBitmapInfo>(kCGImageAlphaLast) | kCGBitmapByteOrder32Big,
                      data.get(), nullptr, false, kCGRenderingIntentDefault));
    if (!result)
        throw std::runtime_error("Cannot prepare alignment image.");
    return result;
}
// Registration can return a finite but incorrect homography for a translated
// panorama. Compare image content before accepting it. ZNCC tolerates exposure
// changes; a bounded translation search also supplies a large-motion candidate
// when Vision's local homography optimizer stays near its initial position.
double correlation(ai::RgbaImage reference, ai::RgbaImage floating, const Homography &placement) {
    const auto inverse = placement.inverse();
    auto luminance = [](ai::RgbaImage input, double u, double v) {
        const double x = std::clamp(u * input.width - .5, 0.0, input.width - 1.0);
        const double y = std::clamp(v * input.height - .5, 0.0, input.height - 1.0);
        const int ix = static_cast<int>(x), iy = static_cast<int>(y);
        double result = 0;
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const auto offset =
                    (static_cast<std::size_t>(std::min(iy + dy, input.height - 1)) * input.width +
                     std::min(ix + dx, input.width - 1)) *
                    4;
                const double weight = (dx ? x - ix : 1 - x + ix) * (dy ? y - iy : 1 - y + iy);
                result +=
                    weight * (.2126 * input.pixels[offset] + .7152 * input.pixels[offset + 1] +
                              .0722 * input.pixels[offset + 2]);
            }
        return result;
    };
    double sumA = 0, sumB = 0, squareA = 0, squareB = 0, product = 0;
    int count = 0;
    constexpr int grid = 24;
    for (int y = 0; y < grid; ++y)
        for (int x = 0; x < grid; ++x) {
            const double u = (x + .5) / grid, v = (y + .5) / grid;
            const auto mapped = inverse.point(u, v);
            if (mapped[0] <= 0 || mapped[0] >= 1 || mapped[1] <= 0 || mapped[1] >= 1)
                continue;
            const double a = luminance(reference, u, v),
                         b = luminance(floating, mapped[0], mapped[1]);
            sumA += a;
            sumB += b;
            squareA += a * a;
            squareB += b * b;
            product += a * b;
            ++count;
        }
    if (count < grid * grid / 4)
        return -1;
    const double varianceA = squareA - sumA * sumA / count,
                 varianceB = squareB - sumB * sumB / count;
    if (varianceA < count || varianceB < count)
        return -1; // Less than one RGB8 step of variation cannot establish alignment.
    return (product - sumA * sumB / count) / std::sqrt(varianceA * varianceB);
}
Homography verifiedAlignment(ai::RgbaImage reference, ai::RgbaImage floating,
                             const Homography &native, CancellationToken stop) {
    Homography best = native;
    double bestScore = correlation(reference, floating, native);
    Homography translation;
    double translationScore = -std::numeric_limits<double>::infinity();
    auto consider = [&](double x, double y) {
        if (std::abs(x) > .75 || std::abs(y) > .75)
            return;
        const Homography candidate{{1, 0, x, 0, 1, y, 0, 0, 1}};
        const double score = correlation(reference, floating, candidate);
        if (score > translationScore) {
            translation = candidate;
            translationScore = score;
        }
    };
    for (int y = -12; y <= 12; ++y) {
        checkCancellation(stop);
        for (int x = -12; x <= 12; ++x)
            consider(x / 16.0, y / 16.0);
    }
    // Refine the strongest coarse translation to subpixel resolution, retaining
    // the candidate only when measured correspondence improves.
    for (double step = 1.0 / 64; step * std::max(reference.width, reference.height) >= .25;
         step /= 4) {
        checkCancellation(stop);
        const double centerX = translation.values[2], centerY = translation.values[5];
        for (int y = -3; y <= 3; ++y)
            for (int x = -3; x <= 3; ++x)
                consider(centerX + step * x, centerY + step * y);
    }
    if (translationScore > bestScore) {
        best = translation;
        bestScore = translationScore;
    }
    if (!std::isfinite(bestScore) || bestScore < .85)
        throw std::runtime_error(
            "Could not align these frames. Use images with sufficient overlap.");
    return best;
}
} // namespace
Homography alignImages(ai::RgbaImage reference, ai::RgbaImage floating, CancellationToken stop,
                       AlignmentMode mode) {
    checkCancellation(stop);
    if (reference.width != floating.width || reference.height != floating.height)
        throw std::runtime_error("Alignment requires matching upright image dimensions.");
    @autoreleasepool {
        const auto target = image(reference), source = image(floating);
        auto *request =
            [[VNHomographicImageRegistrationRequest alloc] initWithTargetedCGImage:source.get()
                                                                           options:@{}];
        request.revision = VNHomographicImageRegistrationRequestRevision1;
        auto *handler = [[VNImageRequestHandler alloc] initWithCGImage:target.get() options:@{}];
        NSError *error = nil;
        if (![handler performRequests:@[ request ] error:&error] || request.results.count != 1)
            throw std::runtime_error(
                "Could not align these frames. Use images with sufficient overlap.");
        checkCancellation(stop);
        const auto matrix = request.results.firstObject.warpTransform;
        Homography native;
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column)
                native.values[row * 3 + column] = matrix.columns[column][row];
        // Vision/Core Image pixel coordinates are bottom-left. Convert to the
        // normalized top-left canvas used by the CPU merge code.
        const double w = reference.width, h = reference.height;
        const Homography fromNormalized{{w, 0, 0, 0, -h, h, 0, 0, 1}};
        const Homography toNormalized{{1 / w, 0, 0, 0, -1 / h, 1, 0, 0, 1}};
        const auto result = toNormalized * native * fromNormalized;
        const auto center = result.point(.5, .5);
        if (center[0] < -1 || center[0] > 2 || center[1] < -1 || center[1] > 2)
            throw std::runtime_error("Image alignment lies outside the supported overlap.");
        // Refuse a horizon/pole crossing inside the image.
        const auto origin = result.homogeneous(0, 0);
        for (const auto &p : {std::array{0., 1.}, std::array{1., 0.}, std::array{1., 1.}})
            if (origin[2] * result.homogeneous(p[0], p[1])[2] <= 0)
                throw std::runtime_error("Unstable image alignment.");
        return mode == AlignmentMode::panorama
                   ? verifiedAlignment(reference, floating, result, stop)
                   : result;
    }
}
} // namespace luma::macos
