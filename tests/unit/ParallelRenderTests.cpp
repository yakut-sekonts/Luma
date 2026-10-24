#include "core/color/RGBProfiles.h"
#include "core/render/CpuRenderer.h"
#include <algorithm>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void equal(const luma::DisplayImage &left, const luma::DisplayImage &right) {
    require(left.width == right.width && left.height == right.height && left.rgba == right.rgba,
            "Parallel render changed exact display pixels, geometry or alpha");
    require(left.histogram.has_value() == right.histogram.has_value(),
            "Histogram presence changed");
    if (left.histogram) {
        const auto &a = *left.histogram, &b = *right.histogram;
        require(a.bins == b.bins && a.weight == b.weight && a.samples == b.samples &&
                    a.shadows == b.shadows && a.highlights == b.highlights,
                "Parallel render changed exact serial histogram accumulation");
    }
}
void exercise(bool alpha, bool transformed, bool monitorProfile) {
    luma::FloatImage input(769, 513); // Non-divisible stripes, above the parallel threshold.
    for (std::size_t i = 0; i < input.rgb.size(); ++i)
        input.rgb[i] = static_cast<float>((i * 37) % 2049) / 1024 - .125F;
    if (alpha) {
        input.alpha.resize(luma::pixelCount(input.width, input.height));
        const std::array coverage{0.F, 1.F, .375F, 1e-35F, 1e-8F, .99999994F};
        for (std::size_t i = 0; i < input.alpha.size(); ++i)
            input.alpha[i] = coverage[i % coverage.size()];
    }
    const auto originalRGB = input.rgb, originalAlpha = input.alpha;
    luma::EditDocument edits;
    edits.exposure = .7;
    edits.highlights = -25;
    edits.creative.setParameter("presence.clarity", 37);
    edits.creative.setParameter("presence.texture", -23);
    edits.creative.setParameter("grain.amount", 15);
    luma::MaskOverlay overlay;
    if (transformed) {
        edits.crop.angle = 5;
        edits.transform.scale = 80;
        edits.transform.constrainCrop = false;
        luma::LocalMask mask;
        mask.id = "00000000-0000-4000-8000-000000000001";
        mask.name = "Parallel equivalence";
        mask.adjustments[0] = .4;
        edits.localMasks.push_back(mask);
        overlay = {mask.id, {.1, .7, .2}, .35};
    }
    const auto profile = monitorProfile ? luma::linearSRGBProfile() : std::vector<std::uint8_t>{};
    const auto render = [&](unsigned workers) {
        return luma::renderCPU(input, edits, transformed ? 5 : 0, profile, {}, {},
                               luma::DetailDiagnostic::none, .5, {}, overlay, nullptr,
                               {true, luma::ClippingPreview::both}, workers);
    };
    const auto serial = render(1);
    for (unsigned workers : {2U, 3U, 4U})
        equal(serial, render(workers));
    auto concurrent = std::async(std::launch::async, [&] { return render(4); });
    equal(serial, render(4));
    equal(serial, concurrent.get());
    require(input.rgb == originalRGB && input.alpha == originalAlpha,
            "Parallel render mutated immutable source pixels");
    luma::CancellationSource cancellation;
    cancellation.cancel();
    bool cancelled = false;
    try {
        (void)luma::renderCPU(input, edits, 0, profile, cancellation.token());
    } catch (const std::runtime_error &error) {
        cancelled = std::string(error.what()) == "Operation cancelled.";
    }
    require(cancelled, "Parallel render ignored cancellation");
}
void failedWorkerAndOptionalAnalysis() {
    luma::FloatImage input(513, 259);
    std::fill(input.rgb.begin(), input.rgb.end(), .3F);
    const auto render = [&](unsigned workers, bool histogram) {
        return luma::renderCPU(input, {}, 0, {}, {}, {}, luma::DetailDiagnostic::none, 1, {}, {},
                               nullptr, {histogram}, workers);
    };
    const auto original = render(1, false);
    equal(original, render(4, false));
    require(!original.histogram, "Disabled histogram was unexpectedly allocated");
    for (unsigned workers : {0U, 5U}) {
        bool rejected = false;
        try {
            (void)render(workers, false);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected, "Invalid parallel worker budget was accepted");
    }
    // Put a non-finite value in the last stripe, away from the caller's rows.
    // Its exception must escape only after all workers have joined; a later
    // render using the same source must remain valid.
    input.rgb.back() = std::numeric_limits<float>::quiet_NaN();
    bool failed = false;
    try {
        (void)render(4, false);
    } catch (const std::runtime_error &error) {
        failed = std::string(error.what()) == "Non-finite input pixel.";
    }
    require(failed, "Background renderer failure was swallowed");
    input.rgb.back() = .3F;
    equal(original, render(4, false));
}
} // namespace
int main() {
    try {
        failedWorkerAndOptionalAnalysis();
        exercise(false, false, false);
        exercise(true, false, true);
        exercise(true, true, false);
        std::cout << "Parallel/serial exact pixels, histogram, alpha, profiles, concurrent renders "
                     "and cancellation PASS\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
