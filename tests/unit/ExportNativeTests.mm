#include "apps/shared/SessionService.h"
#include "core/catalog/Identity.h"
#include "core/export/DngWriter.h"
#include "platform/macos/CFHandle.h"
#include "platform/macos/ExportColor.h"
#include "platform/macos/ExportMetadata.h"
#include "platform/macos/FileHasher.h"
#include "platform/macos/ImageExporter.h"
#include "platform/macos/RasterDecoder.h"
#include "platform/macos/Watermark.h"
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace {
using luma::macos::CFHandle;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Invalid native export was accepted.");
}
CFHandle<CFURLRef> url(const std::filesystem::path &path) {
    return CFHandle<CFURLRef>(CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8 *>(path.c_str()),
        static_cast<CFIndex>(path.string().size()), false));
}
void sourceJPEG(const std::filesystem::path &path) {
    std::vector<std::uint8_t> pixels(128 * 96 * 4, 255);
    CFHandle color(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    CFHandle provider(CGDataProviderCreateWithData(nullptr, pixels.data(), pixels.size(), nullptr));
    CFHandle image(CGImageCreate(128, 96, 8, 32, 128 * 4, color.get(), kCGImageAlphaLast,
                                 provider.get(), nullptr, false,
                                 kCGRenderingIntentRelativeColorimetric));
    auto target = url(path);
    CFHandle writer(
        CGImageDestinationCreateWithURL(target.get(), CFSTR("public.jpeg"), 1, nullptr));
    require(image && writer, "Cannot create source metadata fixture.");
    NSDictionary *properties = @{
        (__bridge NSString *)kCGImagePropertyTIFFDictionary : @{
            (__bridge NSString *)kCGImagePropertyTIFFArtist : @"QA Artist",
            (__bridge NSString *)kCGImagePropertyTIFFCopyright : @"Luma QA"
        },
        (__bridge NSString *)kCGImagePropertyExifDictionary : @{
            (__bridge NSString *)kCGImagePropertyExifDateTimeOriginal : @"2026:10:14 10:20:30",
            (__bridge NSString *)kCGImagePropertyExifLensModel : @"QA Lens"
        },
        (__bridge NSString *)kCGImagePropertyGPSDictionary : @{
            (__bridge NSString *)kCGImagePropertyGPSLatitude : @48.5,
            (__bridge NSString *)kCGImagePropertyGPSLatitudeRef : @"N",
            (__bridge NSString *)kCGImagePropertyGPSLongitude : @2.3,
            (__bridge NSString *)kCGImagePropertyGPSLongitudeRef : @"E"
        },
        (__bridge NSString *)kCGImagePropertyIPTCDictionary : @{
            (__bridge NSString *)kCGImagePropertyIPTCCity : @"SecretCity",
            (__bridge NSString *)kCGImagePropertyIPTCProvinceState : @"SecretState",
            (__bridge NSString *)kCGImagePropertyIPTCCountryPrimaryLocationName : @"SecretCountry",
            (__bridge NSString *)kCGImagePropertyIPTCSubLocation : @"SecretStreet",
            (__bridge NSString *)kCGImagePropertyIPTCCopyrightNotice : @"Luma QA"
        }
    };
    CGImageDestinationAddImage(writer.get(), image.get(), (__bridge CFDictionaryRef)properties);
    require(CGImageDestinationFinalize(writer.get()), "Cannot write source metadata fixture.");
}
std::string xmp(CGImageMetadataRef metadata, luma::MetadataPolicy policy) {
    const auto bytes = luma::macos::metadataXMP(metadata, policy);
    return {bytes.begin(), bytes.end()};
}
bool tag(CGImageMetadataRef metadata, CFStringRef dictionary, CFStringRef property) {
    CFHandle result(CGImageMetadataCopyTagMatchingImageProperty(metadata, dictionary, property));
    return static_cast<bool>(result);
}
void metadataTests(const std::filesystem::path &source) {
    luma::ExportOptions options;
    options.format = luma::ExportFormat::png;
    options.metadata = luma::MetadataPolicy::all;
    auto all = luma::macos::exportMetadata(source, options, 64, 48);
    const auto allText = xmp(all.get(), options.metadata);
    require(allText.find("QA Artist") != std::string::npos &&
                allText.find("Luma QA") != std::string::npos &&
                allText.find("SecretCity") != std::string::npos,
            "All metadata lost attribution or available IPTC fields.");
    require(tag(all.get(), kCGImagePropertyGPSDictionary, kCGImagePropertyGPSLatitude),
            "All metadata lost GPS.");
    options.metadata = luma::MetadataPolicy::removeLocation;
    auto removed = luma::macos::exportMetadata(source, options, 64, 48);
    const auto removedText = xmp(removed.get(), options.metadata);
    require(!tag(removed.get(), kCGImagePropertyGPSDictionary, kCGImagePropertyGPSLatitude) &&
                removedText.find("SecretCity") == std::string::npos &&
                removedText.find("SecretCountry") == std::string::npos &&
                removedText.find("SecretState") == std::string::npos &&
                removedText.find("SecretStreet") == std::string::npos,
            "Location removal retained GPS or IPTC location.");
    require(removedText.find("QA Artist") != std::string::npos &&
                tag(removed.get(), kCGImagePropertyExifDictionary,
                    kCGImagePropertyExifDateTimeOriginal),
            "Location removal erased unrelated attribution/capture metadata.");
    options.metadata = luma::MetadataPolicy::copyrightOnly;
    auto copyright = luma::macos::exportMetadata(source, options, 64, 48);
    const auto copyrightText = xmp(copyright.get(), options.metadata);
    require(copyrightText.find("QA Artist") != std::string::npos &&
                copyrightText.find("Luma QA") != std::string::npos,
            "Copyright-only filtering erased creator/rights array values.");
    require(!tag(copyright.get(), kCGImagePropertyGPSDictionary, kCGImagePropertyGPSLatitude) &&
                !tag(copyright.get(), kCGImagePropertyExifDictionary,
                     kCGImagePropertyExifDateTimeOriginal) &&
                copyrightText.find("QA Lens") == std::string::npos &&
                copyrightText.find("SecretCity") == std::string::npos,
            "Copyright-only metadata retained unrelated fields.");
    options.metadata = luma::MetadataPolicy::none;
    auto none = luma::macos::exportMetadata(source, options, 64, 48);
    require(luma::macos::metadataXMP(none.get(), options.metadata).empty(),
            "Metadata None produced source XMP.");

    luma::MetadataOverlay overlay;
    overlay.includeText = true;
    overlay.values.title = "Private title";
    overlay.values.caption = "Private caption";
    overlay.values.creators = {"First creator", "Second creator"};
    overlay.values.copyright = "Retained rights";
    overlay.values.location = "Private location";
    overlay.values.keywords = {"Private keyword one", "Private keyword two"};
    options.metadata = luma::MetadataPolicy::copyrightOnly;
    auto filtered = luma::macos::exportMetadata(source, options, 64, 48, overlay);
    const auto filteredText = xmp(filtered.get(), options.metadata);
    require(filteredText.find("First creator") != std::string::npos &&
                filteredText.find("Second creator") != std::string::npos &&
                filteredText.find("Retained rights") != std::string::npos &&
                filteredText.find("Private") == std::string::npos &&
                filteredText.find("Secret") == std::string::npos,
            "Copyright-only export did not filter nested catalog metadata safely.");
}
std::vector<std::uint8_t> rgba(const std::filesystem::path &path, int width, int height) {
    auto input = url(path);
    CFHandle source(CGImageSourceCreateWithURL(input.get(), nullptr));
    CFHandle image(source ? CGImageSourceCreateImageAtIndex(source.get(), 0, nullptr) : nullptr);
    require(image && CGImageGetWidth(image.get()) == static_cast<std::size_t>(width) &&
                CGImageGetHeight(image.get()) == static_cast<std::size_t>(height),
            "Encoded output has incorrect geometry or cannot be read.");
    std::vector<std::uint8_t> result(static_cast<std::size_t>(width) * height * 4);
    CFHandle color(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    CFHandle context(CGBitmapContextCreate(result.data(), width, height, 8,
                                           static_cast<std::size_t>(width) * 4, color.get(),
                                           kCGImageAlphaPremultipliedLast));
    require(static_cast<bool>(context), "Cannot allocate independent encoded-image reader.");
    CGContextDrawImage(context.get(), CGRectMake(0, 0, width, height), image.get());
    return result;
}
void outputTests(const std::filesystem::path &folder, const std::filesystem::path &source) {
    luma::DecodedRaw decoded{luma::FloatImage(128, 96), {}, {}, {}};
    decoded.metadata.width = 128;
    decoded.metadata.height = 96;
    for (int y = 0; y < 96; ++y)
        for (int x = 0; x < 128; ++x) {
            const auto i = (static_cast<std::size_t>(y) * 128 + x) * 3;
            decoded.image.rgb[i] = .1F + .6F * x / 127;
            decoded.image.rgb[i + 1] = .1F + .6F * y / 95;
            decoded.image.rgb[i + 2] = .2F;
        }
    int index = 0;
    for (auto format :
         {luma::ExportFormat::jpeg, luma::ExportFormat::png, luma::ExportFormat::tiff8,
          luma::ExportFormat::tiff16, luma::ExportFormat::dng})
        for (auto space : {luma::ExportColorSpace::srgb, luma::ExportColorSpace::displayP3,
                           luma::ExportColorSpace::adobeRGB, luma::ExportColorSpace::proPhoto}) {
            luma::ExportOptions options;
            options.format = format;
            options.colorSpace = space;
            options.resize = luma::ResizeMode::width;
            options.resizeValue = 64;
            const auto path = folder / ("encoded-" + std::to_string(index++) + options.extension());
            luma::macos::writeImage(decoded, {}, path, options, {}, {}, source);
            require(std::filesystem::file_size(path) > 128, "Encoded export is empty.");
            if (format == luma::ExportFormat::dng) {
                const auto imported = luma::RawDecoder().inspect(path);
                require(imported.width == 64 && imported.height == 48,
                        "Advanced DNG output did not reimport at resized dimensions.");
                continue;
            }
            const auto pixels = rgba(path, 64, 48);
            double error = 0;
            for (int y = 0; y < 48; ++y)
                for (int x = 0; x < 64; ++x) {
                    const auto i = (static_cast<std::size_t>(y) * 64 + x) * 4;
                    const std::array linear{.1 + .6 * (2 * x + .5) / 127,
                                            .1 + .6 * (2 * y + .5) / 95, .2};
                    for (std::size_t c = 0; c < 3; ++c) {
                        const double encoded = linear[c] <= .0031308
                                                   ? 12.92 * linear[c]
                                                   : 1.055 * std::pow(linear[c], 1 / 2.4) - .055;
                        error += std::abs(pixels[i + c] - encoded * 255);
                    }
                }
            require(error / (64 * 48 * 3) < (format == luma::ExportFormat::jpeg ? 4 : 1.5),
                    "Output profile/encoding changed independent linear RGB reference colors.");
        }
    for (auto format : {luma::ExportFormat::tiff8, luma::ExportFormat::tiff16})
        for (auto compression : {luma::TiffCompression::none, luma::TiffCompression::lzw,
                                 luma::TiffCompression::zip}) {
            luma::ExportOptions options;
            options.format = format;
            options.compression = compression;
            options.resize = luma::ResizeMode::width;
            options.resizeValue = 64;
            options.metadata = luma::MetadataPolicy::all;
            luma::macos::writeImage(decoded, {},
                                    folder / ("compression-" + std::to_string(index++) + ".tif"),
                                    options, {}, {}, source);
        }
    for (auto policy : {luma::MetadataPolicy::all, luma::MetadataPolicy::removeLocation,
                        luma::MetadataPolicy::copyrightOnly, luma::MetadataPolicy::none}) {
        luma::ExportOptions options;
        options.format = luma::ExportFormat::png;
        options.metadata = policy;
        options.resize = luma::ResizeMode::width;
        options.resizeValue = 64;
        const auto path = folder / ("metadata-" + std::to_string(index++) + ".png");
        luma::macos::writeImage(decoded, {}, path, options, {}, {}, source);
        auto input = url(path);
        CFHandle image(CGImageSourceCreateWithURL(input.get(), nullptr));
        CFHandle metadata(CGImageSourceCopyMetadataAtIndex(image.get(), 0, nullptr));
        const bool gps = metadata && tag(metadata.get(), kCGImagePropertyGPSDictionary,
                                         kCGImagePropertyGPSLatitude);
        require(gps == (policy == luma::MetadataPolicy::all),
                "Encoded output did not honor metadata location policy.");
    }
    const auto destination = folder / "cancelled.png";
    {
        std::ofstream sentinel(destination);
        sentinel << "existing output";
    }
    const auto before = luma::macos::sha256File(destination);
    luma::CancellationSource stop;
    luma::ExportOptions options;
    options.format = luma::ExportFormat::png;
    options.colorSpace = luma::ExportColorSpace::displayP3;
    options.overwrite = true;
    rejects([&] {
        luma::macos::writeImage(decoded, {}, destination, options, stop.token(), [&](double value) {
            if (value > .5)
                stop.cancel();
        });
    });
    require(luma::macos::sha256File(destination) == before,
            "Cancelled export changed an existing destination.");
    for (const auto &entry : std::filesystem::directory_iterator(folder))
        require(!entry.path().filename().string().starts_with(".luma-export-"),
                "Cancelled export leaked a temporary file.");
}
void adaptiveHDRTests(const std::filesystem::path &folder,
                      const std::filesystem::path &metadataSource) {
    if (@available(macOS 15.0, *)) {
        constexpr int width = 64, height = 32;
        luma::DecodedRaw decoded{luma::FloatImage(width, height), {}, {}, {}};
        decoded.metadata.width = width;
        decoded.metadata.height = height;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const float value = x < width / 2 ? .18F : 2.F;
                const auto offset = (static_cast<std::size_t>(y) * width + x) * 3;
                decoded.image.rgb[offset] = decoded.image.rgb[offset + 1] =
                    decoded.image.rgb[offset + 2] = value;
            }
        luma::ExportOptions options;
        options.format = luma::ExportFormat::heicHDR;
        options.quality = 100;
        options.metadata = luma::MetadataPolicy::all;
        const auto rendered = luma::renderLinearOutput(decoded, {});
        const auto expectedShadow =
            rendered.rgba[(static_cast<std::size_t>(height / 2) * width + width / 4) * 4];
        const auto expectedHighlight =
            rendered.rgba[(static_cast<std::size_t>(height / 2) * width + width * 3 / 4) * 4];
        require(expectedHighlight > 1, "Adaptive HDR fixture has no rendered HDR highlight.");
        const auto path = folder / "adaptive-hdr.heic";
        luma::macos::writeImage(decoded, {}, path, options, {}, {}, metadataSource);
        auto inputURL = url(path);
        CFHandle source(CGImageSourceCreateWithURL(inputURL.get(), nullptr));
        CFHandle gainMap(source ? CGImageSourceCopyAuxiliaryDataInfoAtIndex(
                                      source.get(), 0, kCGImageAuxiliaryDataTypeISOGainMap)
                                : nullptr);
        CFHandle base(source ? CGImageSourceCreateImageAtIndex(source.get(), 0, nullptr) : nullptr);
        require(source && CGImageSourceGetStatus(source.get()) == kCGImageStatusComplete &&
                    gainMap && base,
                "Adaptive HDR export lacks a readable SDR base or ISO gain map.");
        const auto imported = luma::macos::RasterDecoder().decode(path);
        const auto sample = [&](int x) {
            return imported.image.rgb[(static_cast<std::size_t>(height / 2) * width + x) * 3];
        };
        const auto shadow = sample(width / 4), highlight = sample(width * 3 / 4);
        if (std::abs(shadow - expectedShadow) >= .04F || highlight <= 1.1F ||
            highlight <= shadow * 5 || highlight > expectedHighlight + .1F)
            throw std::runtime_error(
                "Adaptive HDR round trip lost HDR headroom or SDR shadow accuracy: rendered " +
                std::to_string(expectedShadow) + "/" + std::to_string(expectedHighlight) +
                ", decoded " + std::to_string(shadow) + "/" + std::to_string(highlight));
    }
}
void watermarkTests(const std::filesystem::path &folder) {
    luma::LinearOutput original(256, 192);
    for (std::size_t i = 3; i < original.rgba.size(); i += 4)
        original.rgba[i] = 1;
    luma::WatermarkOptions options;
    options.kind = luma::WatermarkKind::text;
    options.text = "Luma · Тест";
    options.opacity = 100;
    options.size = 25;
    for (int anchor = 0; anchor < 9; ++anchor) {
        options.anchor = anchor;
        auto image = original;
        luma::macos::applyWatermark(image, options);
        int minX = 256, minY = 192, maxX = -1, maxY = -1;
        for (int y = 0; y < 192; ++y)
            for (int x = 0; x < 256; ++x)
                if (image.rgba[(static_cast<std::size_t>(y) * 256 + x) * 4] > .01) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
        require(maxX >= minX && maxY >= minY && minX >= 0 && minY >= 0 && maxX < 256 && maxY < 192,
                "Text watermark is empty or escaped its canvas.");
        const int centerX = (minX + maxX) / 2, centerY = (minY + maxY) / 2;
        require((anchor % 3 == 0   ? centerX < 80
                 : anchor % 3 == 1 ? centerX > 90 && centerX < 166
                                   : centerX > 176) &&
                    (anchor / 3 == 0   ? centerY < 55
                     : anchor / 3 == 1 ? centerY > 65 && centerY < 127
                                       : centerY > 137),
                "Watermark anchor did not position text correctly.");
    }
    const auto path = folder / "watermark.png";
    const std::array<std::uint8_t, 4> red{255, 0, 0, 128};
    CFHandle color(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    CFHandle provider(CGDataProviderCreateWithData(nullptr, red.data(), red.size(), nullptr));
    CFHandle image(CGImageCreate(1, 1, 8, 32, 4, color.get(), kCGImageAlphaLast, provider.get(),
                                 nullptr, false, kCGRenderingIntentRelativeColorimetric));
    auto target = url(path);
    CFHandle writer(CGImageDestinationCreateWithURL(target.get(), CFSTR("public.png"), 1, nullptr));
    CGImageDestinationAddImage(writer.get(), image.get(), nullptr);
    require(CGImageDestinationFinalize(writer.get()), "Cannot write watermark fixture.");
    options.kind = luma::WatermarkKind::image;
    options.image = path;
    options.opacity = 50;
    options.anchor = 8;
    auto output = original;
    luma::macos::applyWatermark(output, options);
    const auto pixel = (std::size_t{160} * 256 + 220) * 4;
    require(std::abs(output.rgba[pixel] - .25) < .004 && std::abs(output.rgba[pixel + 1]) < .002 &&
                std::abs(output.rgba[pixel + 2]) < .002 && output.rgba[pixel + 3] == 1,
            "Image watermark alpha/opacity/color compositing is wrong.");
    // An asymmetric image detects vertical mirroring, independently of text positioning.
    const auto twoColorPath = folder / "two-color.png";
    std::array<std::uint8_t, 4 * 8 * 8> twoColor{};
    for (std::size_t y = 0; y < 8; ++y)
        for (std::size_t x = 0; x < 8; ++x) {
            const auto i = (y * 8 + x) * 4;
            twoColor[i + (y < 4 ? 0 : 2)] = 255;
            twoColor[i + 3] = 255;
        }
    CFHandle twoProvider(
        CGDataProviderCreateWithData(nullptr, twoColor.data(), twoColor.size(), nullptr));
    CFHandle twoImage(CGImageCreate(8, 8, 8, 32, 32, color.get(), kCGImageAlphaLast,
                                    twoProvider.get(), nullptr, false,
                                    kCGRenderingIntentRelativeColorimetric));
    auto twoURL = url(twoColorPath);
    CFHandle twoWriter(
        CGImageDestinationCreateWithURL(twoURL.get(), CFSTR("public.png"), 1, nullptr));
    CGImageDestinationAddImage(twoWriter.get(), twoImage.get(), nullptr);
    require(CGImageDestinationFinalize(twoWriter.get()), "Cannot write orientation watermark.");
    options.image = twoColorPath;
    options.opacity = 100;
    options.anchor = 0;
    options.margin = 0;
    options.size = 25;
    auto orientation = original;
    luma::macos::applyWatermark(orientation, options);
    const auto topPixel = (std::size_t{8} * 256 + 8) * 4,
               bottomPixel = (std::size_t{54} * 256 + 8) * 4;
    require(orientation.rgba[topPixel] > .95 && orientation.rgba[topPixel + 2] < .01 &&
                orientation.rgba[bottomPixel + 2] > .95 && orientation.rgba[bottomPixel] < .01,
            "Image watermark was vertically mirrored.");
    options.opacity = 0;
    auto disabled = original;
    luma::macos::applyWatermark(disabled, options);
    require(disabled.rgba == original.rgba, "Zero-opacity watermark changed pixels.");
}
void originalProtection(const std::filesystem::path &folder) {
    luma::LinearOutput source(32, 24);
    for (std::size_t i = 0; i < source.rgba.size(); i += 4) {
        source.rgba[i] = .1F;
        source.rgba[i + 1] = .2F;
        source.rgba[i + 2] = .3F;
        source.rgba[i + 3] = 1;
    }
    const luma::macos::ExportColor color(luma::ExportColorSpace::srgb);
    const auto first = folder / "first.dng", second = folder / "second.dng";
    luma::writeLinearDNG(
        source, color.colorants(),
        [&](auto input, auto output) { color.convert(input, output, true); }, first, "Luma QA");
    std::filesystem::copy_file(first, second);
    const auto checksum = luma::macos::sha256File(first);
    const auto metadata = luma::RawDecoder().inspect(first);
    const auto catalog = folder / "protect.sqlite";
    luma::Catalog db(catalog);
    const auto asset = db.addAndSelect(first, checksum, metadata);
    db.addAndSelect(second, checksum, metadata);
    luma::ExportOptions options;
    options.format = luma::ExportFormat::dng;
    options.overwrite = true;
    rejects([&] { luma::SessionService(catalog).exportPhoto(asset, {}, second, options); });
    const auto alias = folder / "original-alias.dng";
    std::filesystem::create_hard_link(second, alias);
    rejects([&] { luma::SessionService(catalog).exportPhoto(asset, {}, alias, options); });
    const auto symbolic = folder / "original-symbolic.dng";
    std::filesystem::create_symlink(second, symbolic);
    rejects([&] { luma::SessionService(catalog).exportPhoto(asset, {}, symbolic, options); });
    const auto late = folder / "late-original.dng";
    std::filesystem::copy_file(first, late);
    bool registered = false;
    rejects([&] {
        luma::SessionService(catalog).exportPhoto(
            asset, {}, late, options, {}, [&](double progress) {
                if (progress > .5 && !registered) {
                    registered = true;
                    luma::Catalog(catalog).addAndSelect(late, checksum, metadata);
                }
            });
    });
    require(registered && luma::macos::sha256File(late) == checksum,
            "Atomic export install replaced an original registered during rendering.");
    require(luma::macos::sha256File(first) == checksum &&
                luma::macos::sha256File(second) == checksum,
            "Export replaced an unselected catalog original.");
}
} // namespace
void exportNativeTests() {
    @autoreleasepool {
        struct Folder {
            std::filesystem::path path =
                std::filesystem::temp_directory_path() / ("luma-native-export-" + luma::newID());
            Folder() {
                require(std::filesystem::create_directory(path),
                        "Cannot create native export directory.");
            }
            ~Folder() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } folder;
        const auto source = folder.path / "source.jpg";
        sourceJPEG(source);
        const auto hash = luma::macos::sha256File(source);
        metadataTests(source);
        watermarkTests(folder.path);
        outputTests(folder.path, source);
        adaptiveHDRTests(folder.path, source);
        originalProtection(folder.path);
        require(luma::macos::sha256File(source) == hash,
                "Native export altered its metadata source.");
    }
}
