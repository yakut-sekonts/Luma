#include "apps/shared/MetadataService.h"
#include "apps/shared/PhotoDecoder.h"
#include "apps/shared/PhotoFormats.h"
#include "apps/shared/SessionService.h"
#include "core/catalog/Identity.h"
#include "core/export/LinearOutput.h"
#include "core/lens/LensCorrection.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/CFHandle.h"
#include "platform/macos/ExportColor.h"
#include "platform/macos/FileHasher.h"
#include "platform/macos/ImageExporter.h"
#include "platform/macos/RasterDecoder.h"
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>

namespace {
using luma::macos::CFHandle;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F work) {
    bool failed = false;
    try {
        work();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Invalid raster input was accepted.");
}
CFHandle<CFURLRef> url(const std::filesystem::path &path) {
    return CFHandle<CFURLRef>(CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8 *>(path.c_str()),
        static_cast<CFIndex>(path.native().size()), false));
}
void write(const std::filesystem::path &path, CGImageRef image, CFStringRef type,
           int orientation = 1, int frames = 1) {
    auto target = url(path);
    CFHandle writer(CGImageDestinationCreateWithURL(target.get(), type, frames, nullptr));
    require(bool(writer), "Native fixture encoder is unavailable.");
    NSDictionary *properties = @{
        (__bridge NSString *)kCGImagePropertyOrientation : @(orientation),
        (__bridge NSString *)kCGImageDestinationLossyCompressionQuality : @1,
        (__bridge NSString *)kCGImagePropertyTIFFDictionary : @{
            (__bridge NSString *)kCGImagePropertyTIFFMake : @"Luma",
            (__bridge NSString *)kCGImagePropertyTIFFModel : @"Raster Test"
        },
        (__bridge NSString *)kCGImagePropertyExifDictionary : @{
            (__bridge NSString *)kCGImagePropertyExifISOSpeedRatings : @[ @200 ],
            (__bridge NSString *)kCGImagePropertyExifFNumber : @4,
            (__bridge NSString *)kCGImagePropertyExifExposureTime : @0.01,
            (__bridge NSString *)kCGImagePropertyExifFocalLength : @35,
            (__bridge NSString *)kCGImagePropertyExifLensModel : @"Luma Test Lens",
            (__bridge NSString *)kCGImagePropertyExifDateTimeOriginal : @"2026:10:14 12:00:00",
            (__bridge NSString *)kCGImagePropertyExifOffsetTimeOriginal : @"+03:00"
        }
    };
    for (int i = 0; i < frames; ++i)
        CGImageDestinationAddImage(writer.get(), image, (__bridge CFDictionaryRef)properties);
    require(CGImageDestinationFinalize(writer.get()), "Native fixture encoding failed.");
}
void writeGainMap(const std::filesystem::path &path) {
    if (@available(macOS 15.0, *)) {
        constexpr int width = 16, height = 8;
        std::vector<float> pixels(static_cast<std::size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const float value = x < width / 2 ? .18F : 2.F;
                const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
                pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = value;
                pixels[offset + 3] = 1;
            }
        CFHandle color(CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB));
        CFHandle provider(CGDataProviderCreateWithData(nullptr, pixels.data(),
                                                       pixels.size() * sizeof(float), nullptr));
        CFHandle original(
            CGImageCreate(width, height, 32, 128, width * 4 * sizeof(float), color.get(),
                          static_cast<CGBitmapInfo>(kCGImageAlphaLast) | kCGBitmapFloatComponents |
                              kCGBitmapByteOrder32Little,
                          provider.get(), nullptr, false, kCGRenderingIntentRelativeColorimetric));
        CFHandle hdr(CGImageCreateCopyWithContentHeadroom(4, original.get()));
        auto target = url(path);
        CFHandle writer(
            CGImageDestinationCreateWithURL(target.get(), CFSTR("public.heic"), 1, nullptr));
        require(bool(hdr) && bool(writer), "Cannot prepare Adaptive HDR fixture.");
        NSDictionary *properties = @{
            (__bridge NSString *)kCGImageDestinationEncodeRequest :
                (__bridge NSString *)kCGImageDestinationEncodeToISOGainmap,
            (__bridge NSString *)kCGImageDestinationLossyCompressionQuality : @1
        };
        CGImageDestinationAddImage(writer.get(), hdr.get(), (__bridge CFDictionaryRef)properties);
        require(CGImageDestinationFinalize(writer.get()), "Adaptive HDR fixture encoding failed.");
        auto input = url(path);
        CFHandle source(input ? CGImageSourceCreateWithURL(input.get(), nullptr) : nullptr);
        CFHandle gain(source ? CGImageSourceCopyAuxiliaryDataInfoAtIndex(
                                   source.get(), 0, kCGImageAuxiliaryDataTypeISOGainMap)
                             : nullptr);
        require(bool(gain), "Adaptive HDR fixture has no ISO gain map.");
    }
}
std::array<float, 3> pixel(const luma::FloatImage &image, int x, int y) {
    const auto i = (static_cast<std::size_t>(y) * image.width + x) * 3;
    return {image.rgb[i], image.rgb[i + 1], image.rgb[i + 2]};
}
double linear(double value) {
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
}
std::vector<std::uint8_t> quadrants(int width = 64, int height = 48) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    const std::array<std::array<std::uint8_t, 4>, 4> colors{
        {{255, 0, 0, 255}, {0, 255, 0, 128}, {0, 0, 255, 0}, {128, 128, 128, 255}}};
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const auto i = (static_cast<std::size_t>(y) * width + x) * 4;
            const auto &c = colors[(y >= height / 2) * 2 + (x >= width / 2)];
            std::copy(c.begin(), c.end(), pixels.begin() + static_cast<std::ptrdiff_t>(i));
        }
    return pixels;
}
void fixtures(const std::filesystem::path &folder) {
    std::filesystem::create_directories(folder);
    auto pixels = quadrants();
    CFHandle color(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    CFHandle provider(CGDataProviderCreateWithData(nullptr, pixels.data(), pixels.size(), nullptr));
    CFHandle image(CGImageCreate(64, 48, 8, 32, 64 * 4, color.get(), kCGImageAlphaLast,
                                 provider.get(), nullptr, false,
                                 kCGRenderingIntentRelativeColorimetric));
    require(bool(image), "Cannot create RGBA fixture.");
    write(folder / "rgba.png", image.get(), CFSTR("public.png"));
    write(folder / "rgba.tif", image.get(), CFSTR("public.tiff"));
    for (int i = 1; i <= 8; ++i)
        write(folder / ("orientation-" + std::to_string(i) + ".tif"), image.get(),
              CFSTR("public.tiff"), i);
    write(folder / "multipage.tif", image.get(), CFSTR("public.tiff"), 1, 2);
    // Opaque JPEG/BMP avoid making alpha support a requirement of these formats.
    for (std::size_t i = 3; i < pixels.size(); i += 4)
        pixels[i] = 255;
    CFHandle opaque(CGImageCreate(64, 48, 8, 32, 64 * 4, color.get(), kCGImageAlphaLast,
                                  provider.get(), nullptr, false,
                                  kCGRenderingIntentRelativeColorimetric));
    write(folder / "opaque.jpg", opaque.get(), CFSTR("public.jpeg"));
    write(folder / "opaque.bmp", opaque.get(), CFSTR("com.microsoft.bmp"));
    const int width = 1024, height = 4;
    std::vector<std::uint16_t> ramp(static_cast<std::size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            for (std::size_t c = 0; c < 3; ++c)
                ramp[(static_cast<std::size_t>(y) * width + x) * 3 + c] =
                    static_cast<std::uint16_t>(20000 + x * 30);
    CFHandle rampProvider(
        CGDataProviderCreateWithData(nullptr, ramp.data(), ramp.size() * 2, nullptr));
    CFHandle rampImage(
        CGImageCreate(width, height, 16, 48, width * 6, color.get(),
                      static_cast<CGBitmapInfo>(kCGImageAlphaNone) | kCGBitmapByteOrder16Little,
                      rampProvider.get(), nullptr, false, kCGRenderingIntentRelativeColorimetric));
    write(folder / "ramp16.png", rampImage.get(), CFSTR("public.png"));
    write(folder / "ramp16.tif", rampImage.get(), CFSTR("public.tiff"));
    std::array<std::uint8_t, 4> gray{128, 128, 128, 255};
    CFHandle grayProvider(CGDataProviderCreateWithData(nullptr, gray.data(), gray.size(), nullptr));
    CFHandle one(CGImageCreate(1, 1, 8, 32, 4, color.get(), kCGImageAlphaLast, grayProvider.get(),
                               nullptr, false, kCGRenderingIntentRelativeColorimetric));
    write(folder / "one.png", one.get(), CFSTR("public.png"));
    std::array<std::uint8_t, 2> graySamples{0, 255};
    CFHandle graySpace(CGColorSpaceCreateWithName(kCGColorSpaceGenericGrayGamma2_2));
    CFHandle grayData(
        CGDataProviderCreateWithData(nullptr, graySamples.data(), graySamples.size(), nullptr));
    CFHandle grayImage(CGImageCreate(2, 1, 8, 8, 2, graySpace.get(), kCGImageAlphaNone,
                                     grayData.get(), nullptr, false,
                                     kCGRenderingIntentRelativeColorimetric));
    write(folder / "gray.png", grayImage.get(), CFSTR("public.png"));
    std::array<std::uint8_t, 8> cmykSamples{0, 0, 0, 0, 0, 0, 0, 255};
    CFHandle cmykSpace(CGColorSpaceCreateWithName(kCGColorSpaceGenericCMYK));
    CFHandle cmykData(
        CGDataProviderCreateWithData(nullptr, cmykSamples.data(), cmykSamples.size(), nullptr));
    CFHandle cmykImage(CGImageCreate(2, 1, 8, 32, 8, cmykSpace.get(), kCGImageAlphaNone,
                                     cmykData.get(), nullptr, false,
                                     kCGRenderingIntentRelativeColorimetric));
    write(folder / "cmyk.tif", cmykImage.get(), CFSTR("public.tiff"));
    writeGainMap(folder / "adaptive-hdr.heic");
    for (const auto &[name, space] : std::array{std::pair{"p3", kCGColorSpaceDisplayP3},
                                                std::pair{"adobe", kCGColorSpaceAdobeRGB1998},
                                                std::pair{"prophoto", kCGColorSpaceROMMRGB}}) {
        CFHandle profile(CGColorSpaceCreateWithName(space));
        CFHandle tagged(CGImageCreate(1, 1, 8, 32, 4, profile.get(), kCGImageAlphaLast,
                                      grayProvider.get(), nullptr, false,
                                      kCGRenderingIntentRelativeColorimetric));
        write(folder / (std::string(name) + ".png"), tagged.get(), CFSTR("public.png"));
    }
}
void nativeTests(const std::filesystem::path &folder, const std::filesystem::path &assets) {
    const luma::PhotoDecoder decoder;
    for (const auto &name : {"rgba.png", "rgba.tif", "opaque.jpg", "opaque.bmp", "multipage.tif"}) {
        const auto decoded = decoder.decode(folder / name);
        require(decoded.image.width == 64 && decoded.image.height == 48,
                "Raster dimensions changed.");
        const auto red = pixel(decoded.image, 4, 4);
        require(red[0] > .93 && red[1] < .03 && red[2] < .03,
                "Raster color/row orientation is incorrect.");
    }
    const auto decoded = decoder.decode(folder / "rgba.png");
    require(decoded.diagnostics.decoder == "ImageIO" &&
                !decoded.diagnostics.decoderVersion.empty() &&
                decoded.diagnostics.source == luma::DecodeSource::raster &&
                decoded.diagnostics.color == luma::DecodeColorPath::embeddedProfileToLinearSRGB &&
                decoded.diagnostics.highlights == luma::DecodeHighlightPath::sourceRange &&
                decoded.diagnostics.activeWidth == 64 && decoded.diagnostics.activeHeight == 48,
            "Raster decoder diagnostics are incomplete or incorrect.");
    require(decoded.metadata.camera == "Luma Raster Test" &&
                decoded.metadata.lens == "Luma Test Lens" && decoded.metadata.iso == 200 &&
                decoded.metadata.aperture == 4 && decoded.metadata.focalLength == 35 &&
                decoded.metadata.shutterSeconds == .01,
            "Raster photographic metadata was not read.");
    require(decoded.metadata.captureTimestamp == 1791968400,
            "EXIF capture offset was not applied.");
    require(!decoded.image.alpha.empty() &&
                std::abs(decoded.image.coverage(48, 8) - 128.F / 255) < .001 &&
                decoded.image.coverage(8, 36) == 0,
            "Source transparency was lost.");
    require(std::abs(pixel(decoded.image, 48, 36)[0] - linear(128. / 255)) < .002,
            "sRGB transfer function was not removed.");
    for (int i = 1; i <= 8; ++i) {
        const auto oriented =
            decoder.decode(folder / ("orientation-" + std::to_string(i) + ".tif"));
        const auto rendered = luma::renderLinearOutput(oriented, {});
        const bool swap = i >= 5;
        require(rendered.width == (swap ? 48 : 64) && rendered.height == (swap ? 64 : 48),
                "EXIF dimensions were rotated incorrectly.");
        // Expected original source corner for the displayed top-left pixel (EXIF standard).
        constexpr std::array corners{0, 0, 1, 3, 2, 0, 2, 3, 1};
        const int corner = corners[static_cast<std::size_t>(i)];
        const auto expected = pixel(decoded.image, corner % 2 ? 63 : 0, corner / 2 ? 47 : 0);
        const float alpha = decoded.image.coverage(corner % 2 ? 63 : 0, corner / 2 ? 47 : 0);
        require(std::abs(rendered.rgba[3] - alpha) < .002,
                "EXIF source alpha was oriented incorrectly.");
        if (alpha > 0)
            for (std::size_t c = 0; c < 3; ++c)
                require(std::abs(rendered.rgba[c] - expected[c]) < .005,
                        "EXIF color corner was rotated twice or mirrored.");
    }
    for (const auto &name : {"ramp16.png", "ramp16.tif"}) {
        const auto ramp = decoder.decode(folder / name);
        std::set<float> distinct;
        for (int x = 0; x < 1024; ++x)
            distinct.insert(pixel(ramp.image, x, 0)[0]);
        require(distinct.size() > 900, "16-bit input was quantized to 8 bits.");
    }
    const auto one = decoder.decode(folder / "one.png");
    require(luma::renderLinearOutput(one, {}).width == 1 &&
                luma::renderCPU(one.image, {}, 0).width == 1,
            "Single-pixel image failed.");
    for (const auto &name : {"p3", "adobe", "prophoto"}) {
        const auto tagged = decoder.decode(folder / (std::string(name) + ".png"));
        const double expected = std::string_view(name) == "adobe" ? std::pow(128. / 255, 2.19921875)
                                : std::string_view(name) == "prophoto" ? std::pow(128. / 255, 1.8)
                                                                       : linear(128. / 255);
        for (const auto value : tagged.image.rgb)
            require(std::abs(value - expected) < .003,
                    "Input ICC profile did not convert to linear sRGB.");
    }
    for (const auto &name : {"gray.png", "cmyk.tif"}) {
        const auto converted = decoder.decode(folder / name);
        require(converted.image.width == 2 && converted.image.alpha.empty(),
                "Grayscale/CMYK dimensions or opacity changed.");
        const bool gray = std::string_view(name) == "gray.png";
        const auto white = pixel(converted.image, gray ? 1 : 0, 0);
        const auto black = pixel(converted.image, gray ? 0 : 1, 0);
        for (std::size_t c = 0; c < 3; ++c)
            require(white[c] > .95 && black[c] < .05,
                    "Grayscale/CMYK native color conversion failed.");
    }
    const auto webp = decoder.decode(assets / "quadrants.webp");
    require(webp.image.width == 32 && webp.image.height == 24 && !webp.image.alpha.empty(),
            "WebP did not decode as a real RGBA image.");
    require(pixel(webp.image, 2, 2)[0] > .99 &&
                std::abs(webp.image.coverage(20, 2) - 128.F / 255) < .005 &&
                webp.image.coverage(2, 20) == 0,
            "WebP pixels or alpha changed.");
    for (const auto &name : {"opaque.heic", "opaque.avif"}) {
        if (!luma::PhotoDecoder::supportedExtension(name))
            continue;
        const auto native = decoder.decode(assets / name);
        require(native.image.width == 64 && native.image.height == 48 && native.image.alpha.empty(),
                "HEIC/AVIF did not decode the full primary image.");
        const auto red = pixel(native.image, 8, 8);
        const auto blue = pixel(native.image, 8, 40);
        require(red[0] > .9 && red[1] < .03 && blue[2] > .9 && blue[0] < .03,
                "HEIC/AVIF primary pixels changed.");
    }
    if (@available(macOS 15.0, *)) {
        const auto hdr = decoder.decode(folder / "adaptive-hdr.heic");
        const auto shadow = pixel(hdr.image, 2, 2);
        const auto highlight = pixel(hdr.image, 12, 2);
        require(shadow[0] > .12 && shadow[0] < .25 && highlight[0] > 1.25 && highlight[0] <= 4.1,
                "Adaptive HDR gain map did not decode to bounded linear headroom.");
        require(hdr.diagnostics.highlights == luma::DecodeHighlightPath::adaptiveHDR,
                "Adaptive HDR decoder diagnostics did not report expanded headroom.");
    }
    const auto gif = decoder.decode(assets / "two-frames.gif");
    require(pixel(gif.image, 2, 2)[0] > .99 && pixel(gif.image, 31, 23)[2] > .99,
            "GIF did not select the first frame.");
    luma::CancellationSource stop;
    stop.cancel();
    rejects([&] { decoder.decode(folder / "rgba.png", stop.token()); });
    luma::CancellationSource duringDecode;
    rejects([&] {
        decoder.decode(folder / "rgba.png", duringDecode.token(),
                       [&](double) { duringDecode.cancel(); });
    });
    require(duringDecode.isCancelled(), "Decode did not report cancellable progress.");
    const auto corrupt = folder / "corrupt.png";
    {
        std::ofstream file(corrupt);
        file << "not an image";
    }
    rejects([&] { decoder.inspect(corrupt); });
    std::vector<std::uint8_t> widePixels(20001 * 4, 255);
    CFHandle wideColor(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    CFHandle wideData(
        CGDataProviderCreateWithData(nullptr, widePixels.data(), widePixels.size(), nullptr));
    CFHandle wide(CGImageCreate(20001, 1, 8, 32, 20001 * 4, wideColor.get(), kCGImageAlphaLast,
                                wideData.get(), nullptr, false,
                                kCGRenderingIntentRelativeColorimetric));
    write(folder / "oversized.png", wide.get(), CFSTR("public.png"));
    rejects([&] { decoder.inspect(folder / "oversized.png"); });
    rejects([&] { decoder.decode(folder / "oversized.png"); });
    const auto truncated = folder / "truncated.png";
    std::filesystem::copy_file(folder / "rgba.png", truncated);
    std::filesystem::resize_file(truncated, 32);
    rejects([&] { decoder.decode(truncated); });
    require(luma::PhotoDecoder::supportedExtension("TEST.JPEG") &&
                luma::PhotoDecoder::supportedExtension("TEST.PNG") &&
                !luma::PhotoDecoder::supportedExtension("notes.txt"),
            "Image extension discovery changed.");
}
void coverageTests(const std::filesystem::path &folder) {
    luma::DecodedImage source{luma::FloatImage(64, 48), {}, {}, {}};
    source.metadata.width = 64;
    source.metadata.height = 48;
    source.image.alpha.resize(64 * 48);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x) {
            const auto i = static_cast<std::size_t>(y) * 64 + x;
            const bool visible = x >= 24;
            source.image.alpha[i] = visible ? (x == 24 ? .5F : 1.F) : 0.F;
            source.image.rgb[i * 3] = visible ? .3F : 99.F;
            source.image.rgb[i * 3 + 1] = visible ? .2F : 0.F;
            source.image.rgb[i * 3 + 2] = visible ? .1F : 99.F;
        }
    auto small = luma::standardPreview(source.image, 16);
    for (std::size_t i = 0; i < small.alpha.size(); ++i)
        if (small.alpha[i] > 0)
            require(std::abs(small.rgb[i * 3] - .3) < .00001,
                    "Preview introduced a transparent-edge halo.");
    luma::EditDocument edits;
    edits.sharpenAmount = 100;
    edits.sharpenRadius = 1.5;
    edits.noiseLuminance = 75;
    edits.noiseColor = 75;
    edits.transform.rotate = 14;
    edits.transform.constrainCrop = false;
    const auto surface = luma::renderLinearOutput(source, edits);
    int covered = 0;
    for (std::size_t i = 0; i < surface.rgba.size(); i += 4)
        if (surface.rgba[i + 3] > .01) {
            ++covered;
            for (std::size_t c = 0; c < 3; ++c)
                require(std::abs(surface.rgba[i + c] - (.3 - .1 * c)) < .003,
                        "Spatial processing or geometry leaked hidden RGB through alpha.");
        }
    require(covered > 100, "Coverage regression fixture is empty.");
    source.metadata.camera = "NIKON CORPORATION NIKON D7500";
    source.metadata.lens = "AF-S DX Nikkor 18-140mm f/3.5-5.6G ED VR";
    source.metadata.focalLength = 18;
    source.metadata.aperture = 5.6;
    luma::EditDocument optics;
    optics.setEnabled(luma::Correction::lens, true);
    optics.lens.vignetting = 0;
    const luma::LensCorrectionPass corrected(source.image, optics, source.metadata);
    require(!corrected.image().alpha.empty(), "Optics dropped image coverage.");
    for (std::size_t i = 0; i < corrected.image().alpha.size(); ++i)
        if (corrected.image().alpha[i] > .01)
            for (std::size_t c = 0; c < 3; ++c)
                require(std::abs(corrected.image().rgb[i * 3 + c] - (.3 - .1 * c)) < .003,
                        "Optics leaked hidden transparent RGB.");
    luma::ExportOptions options;
    for (auto format :
         {luma::ExportFormat::png, luma::ExportFormat::tiff8, luma::ExportFormat::tiff16,
          luma::ExportFormat::jpeg, luma::ExportFormat::dng}) {
        options.format = format;
        const auto path =
            folder / ("alpha-" + std::to_string(static_cast<int>(format)) + options.extension());
        luma::macos::writeImage(source, {}, path, options);
        const auto imported = luma::PhotoDecoder().decode(path);
        if (format == luma::ExportFormat::png || format == luma::ExportFormat::tiff8 ||
            format == luma::ExportFormat::tiff16) {
            require(!imported.image.alpha.empty() && imported.image.coverage(2, 2) == 0 &&
                        std::abs(imported.image.coverage(24, 2) - .5) < .005,
                    "PNG/TIFF export did not preserve source coverage.");
        } else
            require(imported.image.alpha.empty() && pixel(imported.image, 2, 2)[0] < .005,
                    "Opaque export did not flatten against black.");
    }
    auto bad = source.image;
    bad.alpha.pop_back();
    rejects([&] { luma::standardPreview(bad, 16); });
    bad = source.image;
    bad.alpha[0] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { luma::renderCPU(bad, {}, 0); });
}
void rawFormatTests(const std::filesystem::path &folder) {
    for (const auto extension : luma::supportedRawExtensions) {
        std::string upper(extension);
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char character) { return std::toupper(character); });
        const auto source = folder / ("camera" + upper);
        require(luma::PhotoDecoder::supportedExtension(source),
                "A registered RAW extension was not discovered.");
        require(luma::MetadataService::sidecarPath(source) == folder / "camera.xmp",
                "A registered RAW extension did not use a standard XMP sidecar.");
    }
    require(!luma::PhotoDecoder::supportedExtension("camera.x3f") &&
                !luma::PhotoDecoder::supportedExtension("camera.r3d"),
            "Unsupported layered or cinema RAW was advertised.");

    const auto collision = folder / "sidecar-collision";
    std::filesystem::create_directory(collision);
    std::ofstream(collision / "same.CR3").put('\0');
    std::ofstream(collision / "same.ARW").put('\0');
    rejects([&] { (void)luma::MetadataService::sidecarPath(collision / "same.CR3"); });
}
} // namespace
int main(int argc, char **argv) {
    @autoreleasepool {
        try {
            if (argc == 3 && std::string_view(argv[1]) == "--fixtures") {
                fixtures(argv[2]);
                std::cout << "Generated raster fixtures\n";
                return 0;
            }
            require(argc == 2, "Expected original raster asset directory.");
            struct Folder {
                std::filesystem::path path =
                    std::filesystem::temp_directory_path() / ("luma-raster-" + luma::newID());
                ~Folder() {
                    std::error_code error;
                    std::filesystem::remove_all(path, error);
                }
            } folder;
            fixtures(folder.path);
            nativeTests(folder.path, argv[1]);
            coverageTests(folder.path);
            rawFormatTests(folder.path);
            std::cout << "Raster decode: ICC, metadata, EXIF orientations, 16-bit, Adaptive HDR, "
                         "WebP/GIF, coverage/spatial/export, RAW registry/XMP and invalid inputs "
                         "passed.\n";
            return 0;
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
}
