#include "apps/shared/MergeService.h"
#include "apps/shared/DerivedImage.h"
#include "apps/shared/PhotoDecoder.h"
#include "core/catalog/Catalog.h"
#include "core/image/Bayer.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/FileHasher.h"
#include "platform/macos/ImageAlignment.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <tuple>

namespace luma {
namespace {
FloatImage loadUpright(const AssetRecord &asset, CancellationToken stop, int limit = 0) {
    if (macos::sha256File(asset.path, stop) != asset.checksum)
        throw std::runtime_error("Original changed; merge refused.");
    auto decoded = PhotoDecoder().decode(asset.path, stop);
    if (limit)
        decoded.image = standardPreview(decoded.image, limit, stop);
    EditDocument neutral;
    neutral.enabledCorrections = 0;
    auto rendered = renderLinearOutput(decoded, neutral, stop);
    FloatImage result(rendered.width, rendered.height);
    for (std::size_t i = 0; i < result.rgb.size() / 3; ++i) {
        if ((i & 65535) == 0)
            checkCancellation(stop);
        if (rendered.rgba[i * 4 + 3] != 1)
            throw std::runtime_error("Merge requires opaque images.");
        for (std::size_t c = 0; c < 3; ++c)
            result.rgb[i * 3 + c] = rendered.rgba[i * 4 + c];
    }
    if (macos::sha256File(asset.path, stop) != asset.checksum)
        throw std::runtime_error("Original changed during merge decoding.");
    return result;
}
double exposure(const PhotoMetadata &m) {
    if (!m.iso || !m.shutterSeconds || !m.aperture || *m.iso <= 0 || *m.shutterSeconds <= 0 ||
        *m.aperture <= 0)
        throw std::runtime_error("HDR needs shutter speed, ISO and aperture for every frame.");
    const double result = std::log2(*m.iso * *m.shutterSeconds / (*m.aperture * *m.aperture));
    if (!std::isfinite(result))
        throw std::runtime_error("Invalid HDR exposure metadata.");
    return result;
}
std::string xmlText(std::string_view value) {
    std::string out;
    for (char c : value) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '"')
            out += "&quot;";
        else
            out += c;
    }
    return out;
}
DisplayImage registrationPreview(const FloatImage &image, CancellationToken stop) {
    EditDocument neutral;
    neutral.enabledCorrections = 0;
    return renderCPU(image, neutral, 0, {}, stop);
}
// Robust relative exposure from overlap in linear RGB. Ignore clipped/dark
// samples; a lack of evidence leaves the exposure unchanged.
double overlapExposure(const FloatImage &reference, const FloatImage &floating,
                       const Homography &alignment) {
    const auto inverse = alignment.inverse();
    std::vector<double> ratios;
    for (int y = 1; y < 32; ++y) {
        for (int x = 1; x < 32; ++x) {
            const double u = x / 32.0, v = y / 32.0;
            const auto mapped = inverse.point(u, v);
            if (mapped[0] <= 0 || mapped[0] >= 1 || mapped[1] <= 0 || mapped[1] >= 1)
                continue;
            auto luminance = [](const FloatImage &image, double px, double py) {
                const auto offset =
                    (static_cast<std::size_t>(py * (image.height - 1)) * image.width +
                     static_cast<std::size_t>(px * (image.width - 1))) *
                    3;
                const double r = image.rgb[offset], g = image.rgb[offset + 1],
                             b = image.rgb[offset + 2];
                if (std::min({r, g, b}) < .005 || std::max({r, g, b}) > .9)
                    return 0.0;
                return .2126 * r + .7152 * g + .0722 * b;
            };
            const double a = luminance(reference, u, v),
                         b = luminance(floating, mapped[0], mapped[1]);
            if (a > .015 && b > .015 && std::isfinite(a) && std::isfinite(b))
                ratios.push_back(std::log2(a / b));
        }
    }
    if (ratios.size() < 32)
        return 0;
    const auto middle = ratios.begin() + static_cast<std::ptrdiff_t>(ratios.size() / 2);
    std::nth_element(ratios.begin(), middle, ratios.end());
    return std::clamp(*middle, -2.0, 2.0);
}
std::filesystem::path savePanorama(const std::filesystem::path &catalog, FloatImage image,
                                   const std::vector<AssetRecord> &sources,
                                   const PanoramaOptions &options, CancellationToken stop,
                                   const std::function<void(double)> &progress) {
    LinearOutput output(image.width, image.height);
    for (std::size_t i = 0; i < image.rgb.size() / 3; ++i) {
        if ((i & 65535) == 0)
            checkCancellation(stop);
        for (std::size_t c = 0; c < 3; ++c)
            output.rgba[i * 4 + c] = image.rgb[i * 3 + c];
        output.rgba[i * 4 + 3] = 1;
    }
    std::vector<float>().swap(image.rgb);
    std::ostringstream metadata;
    metadata.imbue(std::locale::classic());
    metadata << "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
                "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                "<rdf:Description rdf:about=\"\" xmlns:luma=\"https://luma.photo/ns/1.0/\" "
                "luma:Operation=\"Panorama\" luma:Projection=\""
             << static_cast<int>(options.projection) << "\" luma:FieldOfView=\""
             << options.fieldOfView << "\" luma:BoundaryWarp=\"" << options.boundaryWarp
             << "\">"
                "<luma:Sources><rdf:Seq>";
    for (const auto &source : sources)
        metadata << "<rdf:li>" << xmlText(source.id + ":" + source.checksum) << "</rdf:li>";
    metadata << "</rdf:Seq></luma:Sources></rdf:Description></rdf:RDF></x:xmpmeta>";
    return createDerivedImage(catalog, output, metadata.str(), stop, progress);
}
} // namespace
std::filesystem::path MergeService::hdr(const std::vector<std::string> &ids, HdrOptions options,
                                        CancellationToken stop,
                                        const std::function<void(double)> &progress) const {
    if (ids.size() < 2 || ids.size() > 9 ||
        std::set<std::string>(ids.begin(), ids.end()).size() != ids.size())
        throw std::runtime_error("Select 2–9 different bracketed photos for HDR.");
    Catalog catalog(catalog_);
    struct Source {
        AssetRecord asset;
        double ev;
        Homography alignment;
    };
    std::vector<Source> sources;
    for (const auto &id : ids) {
        checkCancellation(stop);
        const auto asset = catalog.assetByID(id);
        if (!asset)
            throw std::runtime_error("Merge source is missing from the catalog.");
        sources.push_back({*asset, exposure(asset->metadata), {}});
    }
    std::stable_sort(sources.begin(), sources.end(), [](const auto &a, const auto &b) {
        return a.ev != b.ev ? a.ev < b.ev : a.asset.id < b.asset.id;
    });
    if (sources.back().ev - sources.front().ev < .25)
        throw std::runtime_error("HDR needs frames with different exposures.");
    const auto middle = sources.size() / 2;
    std::swap(sources.front(), sources[middle]);
    std::vector<DisplayImage> previews;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        auto image = loadUpright(sources[i].asset, stop, 768);
        // Approximate common exposure only for registration, without changing
        // the float inputs used by the radiometric merge.
        const double gain = std::exp2(std::clamp(sources.front().ev - sources[i].ev, -16.0, 16.0));
        for (auto &v : image.rgb)
            v = static_cast<float>(v * gain);
        EditDocument neutral;
        neutral.enabledCorrections = 0;
        previews.push_back(renderCPU(image, neutral, 0, {}, stop));
        if (i && options.alignFrames) {
            const auto &reference = previews.front(), &floating = previews.back();
            sources[i].alignment =
                macos::alignImages({reference.width, reference.height, reference.rgba},
                                   {floating.width, floating.height, floating.rgba}, stop);
        }
        if (progress)
            progress(.25 * (i + 1.0) / sources.size());
    }
    auto reference = std::make_shared<FloatImage>(loadUpright(sources.front().asset, stop));
    auto merged = mergeHDR(
        {reference, sources.front().ev, {}}, sources.size() - 1,
        [&](std::size_t index) {
            const auto &source = sources[index + 1];
            return MergeFrame{std::make_shared<FloatImage>(loadUpright(source.asset, stop)),
                              source.ev, source.alignment};
        },
        options, stop,
        [&](double value) {
            if (progress)
                progress(.25 + .6 * value);
        });
    reference.reset();
    LinearOutput output(merged.width, merged.height);
    for (std::size_t i = 0; i < merged.rgb.size() / 3; ++i) {
        if ((i & 65535) == 0)
            checkCancellation(stop);
        for (std::size_t c = 0; c < 3; ++c)
            output.rgba[i * 4 + c] = merged.rgb[i * 3 + c];
        output.rgba[i * 4 + 3] = 1;
    }
    std::vector<float>().swap(merged.rgb);
    std::ostringstream metadata;
    metadata.imbue(std::locale::classic());
    metadata << "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
                "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                "<rdf:Description rdf:about=\"\" xmlns:luma=\"https://luma.photo/ns/1.0/\" "
                "luma:Operation=\"HDR\" luma:AlignFrames=\""
             << options.alignFrames << "\" luma:Deghost=\"" << options.deghost
             << "\">"
                "<luma:Sources><rdf:Seq>";
    for (const auto &source : sources)
        metadata << "<rdf:li>" << xmlText(source.asset.id + ":" + source.asset.checksum)
                 << "</rdf:li>";
    metadata << "</rdf:Seq></luma:Sources></rdf:Description></rdf:RDF></x:xmpmeta>";
    return createDerivedImage(catalog_, output, metadata.str(), stop, [&](double value) {
        if (progress)
            progress(.85 + .15 * value);
    });
}
std::filesystem::path MergeService::panorama(const std::vector<std::string> &ids,
                                             PanoramaOptions options, CancellationToken stop,
                                             const std::function<void(double)> &progress) const {
    if (ids.size() < 2 || ids.size() > 24 ||
        std::set<std::string>(ids.begin(), ids.end()).size() != ids.size())
        throw std::runtime_error("Select 2–24 different overlapping photos for a panorama.");
    Catalog catalog(catalog_);
    std::vector<AssetRecord> sources;
    for (const auto &id : ids) {
        checkCancellation(stop);
        auto asset = catalog.assetByID(id);
        if (!asset)
            throw std::runtime_error("Merge source is missing from the catalog.");
        sources.push_back(std::move(*asset));
    }
    // Preserve the shooting sequence, independent of selection/hash-set order.
    std::sort(sources.begin(), sources.end(), [](const auto &a, const auto &b) {
        return std::tuple(a.metadata.captureTimestamp.value_or(0), a.path.filename(), a.id) <
               std::tuple(b.metadata.captureTimestamp.value_or(0), b.path.filename(), b.id);
    });
    std::vector<Homography> placements(sources.size());
    std::vector<double> gains(sources.size());
    auto previous = loadUpright(sources.front(), stop, 768);
    auto previousDisplay = registrationPreview(previous, stop);
    for (std::size_t i = 1; i < sources.size(); ++i) {
        auto current = loadUpright(sources[i], stop, 768);
        auto currentDisplay = registrationPreview(current, stop);
        const auto pair = macos::alignImages(
            {previousDisplay.width, previousDisplay.height, previousDisplay.rgba},
            {currentDisplay.width, currentDisplay.height, currentDisplay.rgba}, stop,
            macos::AlignmentMode::panorama);
        placements[i] = placements[i - 1] * pair;
        gains[i] = std::clamp(gains[i - 1] + overlapExposure(previous, current, pair), -4.0, 4.0);
        previous = std::move(current);
        previousDisplay = std::move(currentDisplay);
        if (progress)
            progress(.25 * i / (sources.size() - 1.0));
    }
    auto first = std::make_shared<FloatImage>(loadUpright(sources.front(), stop));
    const int width = first->width, height = first->height;
    auto result = mergePanorama(
        width, height, placements,
        [&](std::size_t index) {
            auto image = index == 0
                             ? std::move(first)
                             : std::make_shared<FloatImage>(loadUpright(sources[index], stop));
            if (image->width != width || image->height != height)
                throw std::runtime_error("Panorama frames must have the same upright dimensions.");
            return MergeFrame{std::move(image), gains[index], placements[index]};
        },
        options, stop,
        [&](double value) {
            if (progress)
                progress(.25 + .6 * value);
        });
    return savePanorama(catalog_, std::move(result), sources, options, stop, [&](double value) {
        if (progress)
            progress(.85 + .15 * value);
    });
}
} // namespace luma
