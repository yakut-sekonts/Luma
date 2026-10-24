#include "platform/macos/AdvancedImageExporter.h"
#include "core/export/DngWriter.h"
#include "core/export/JpegICC.h"
#include "core/image/Bayer.h"
#include "platform/macos/CFHandle.h"
#include "platform/macos/ExportColor.h"
#include "platform/macos/ExportMetadata.h"
#include "platform/macos/Watermark.h"
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <unistd.h>

namespace luma::macos {
namespace {
class AtomicOutput {
  public:
    explicit AtomicOutput(const std::filesystem::path &destination) {
        auto pattern = destination.parent_path().string() + "/.luma-export-XXXXXX";
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back(0);
        const int descriptor = mkstemp(name.data());
        if (descriptor < 0)
            throw std::runtime_error("Cannot create export temporary file: " +
                                     std::string(std::strerror(errno)));
        close(descriptor);
        path = name.data();
    }
    ~AtomicOutput() {
        if (!path.empty()) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }
    void install(const std::filesystem::path &target, bool overwrite, CancellationToken stop) {
        checkCancellation(stop);
        const int descriptor = open(path.c_str(), O_RDONLY);
        if (descriptor < 0)
            throw std::runtime_error("Cannot reopen export temporary output.");
        const int result = fsync(descriptor);
        close(descriptor);
        if (result != 0)
            throw std::runtime_error("Cannot sync exported data.");
        checkCancellation(stop);
        const int installed =
            overwrite ? rename(path.c_str(), target.c_str()) : link(path.c_str(), target.c_str());
        if (installed != 0)
            throw std::runtime_error("Cannot install exported file: " +
                                     std::string(std::strerror(errno)));
        if (overwrite)
            path.clear();
    }
    std::filesystem::path path;
};
void jpegICC(const std::filesystem::path &path, std::span<const std::uint8_t> profile) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<std::uint8_t> original((std::istreambuf_iterator<char>(file)), {});
    if (!file.eof() && file.fail())
        throw std::runtime_error("Cannot read JPEG for ICC embedding.");
    file.close();
    const auto tagged = embedJpegICC(original, profile);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(tagged.data()),
                 static_cast<std::streamsize>(tagged.size()));
    output.close();
    if (!output)
        throw std::runtime_error("Cannot embed JPEG ICC profile.");
}
void verifyTIFF(const std::filesystem::path &path, const ExportOptions &options) {
    CFHandle url(CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8 *>(path.c_str()),
        static_cast<CFIndex>(path.string().size()), false));
    CFHandle source(url ? CGImageSourceCreateWithURL(url.get(), nullptr) : nullptr);
    CFHandle properties(source ? CGImageSourceCopyPropertiesAtIndex(source.get(), 0, nullptr)
                               : nullptr);
    if (!properties)
        throw std::runtime_error("Cannot verify encoded TIFF.");
    auto *dictionary = (__bridge NSDictionary *)properties.get();
    const int bits = [dictionary[(__bridge NSString *)kCGImagePropertyDepth] intValue];
    const int compression =
        [dictionary[(__bridge NSString *)kCGImagePropertyTIFFDictionary]
                   [(__bridge NSString *)kCGImagePropertyTIFFCompression] intValue];
    const bool matched = options.compression == TiffCompression::none ? compression == 1
                         : options.compression == TiffCompression::lzw
                             ? compression == 5
                             : compression == 8 || compression == 32946;
    if (bits != (options.format == ExportFormat::tiff16 ? 16 : 8) || !matched)
        throw std::runtime_error(
            "ImageIO did not honor TIFF depth/compression: bits=" + std::to_string(bits) +
            ", compression=" + std::to_string(compression));
}
} // namespace
void writeAdvancedImage(const DecodedRaw &decoded, const EditDocument &edits,
                        const std::filesystem::path &destination, const ExportOptions &options,
                        CancellationToken stop, const std::function<void(double)> &progress,
                        const std::filesystem::path &metadataSource,
                        const ExportInstallGuard &installGuard,
                        const std::optional<MetadataOverlay> &metadataOverlay) {
    @autoreleasepool {
        options.validate();
        edits.validate();
        checkCancellation(stop);
        const auto parent = std::filesystem::canonical(
            destination.parent_path().empty() ? "." : destination.parent_path());
        const auto output = parent / destination.filename();
        if (output.filename().empty())
            throw std::runtime_error("Export destination has no filename.");
        if (std::filesystem::exists(output) && !options.overwrite)
            throw std::runtime_error("Destination already exists.");
        if (options.watermark.kind == WatermarkKind::image && std::filesystem::exists(output) &&
            std::filesystem::exists(options.watermark.image) &&
            std::filesystem::equivalent(output, options.watermark.image))
            throw std::runtime_error("Export cannot replace the watermark input.");
        AtomicOutput temporary(output);
        auto surface = renderLinearOutput(decoded, edits, stop, [&](double value) {
            if (progress)
                progress(.5 + .25 * value);
        });
        surface = resizeOutput(std::move(surface), options, stop);
        if (progress)
            progress(.79);
        sharpenOutput(surface, options, stop);
        if (progress)
            progress(.83);
        applyWatermark(surface, options.watermark, stop);
        if (progress)
            progress(.86);
        const ExportColor color(options.colorSpace);
        auto metadata =
            exportMetadata(metadataSource, options, surface.width, surface.height, metadataOverlay);
        if (options.format == ExportFormat::dng) {
            const auto xmp = metadataXMP(metadata.get(), options.metadata);
            const std::array names{"Luma rendered sRGB", "Luma rendered Display P3",
                                   "Luma rendered Adobe RGB", "Luma rendered ProPhoto RGB"};
            writeLinearDNG(
                surface, color.colorants(),
                [&](auto input, auto result) { color.convert(input, result, true); },
                temporary.path, names[static_cast<std::size_t>(options.colorSpace)], xmp, stop,
                [&](double value) {
                    if (progress)
                        progress(.86 + .13 * value);
                });
        } else {
            const bool tiff =
                options.format == ExportFormat::tiff8 || options.format == ExportFormat::tiff16;
            bool opaque = options.format == ExportFormat::jpeg;
            if (tiff) {
                opaque = true;
                for (std::size_t i = 3; i < surface.rgba.size(); i += 4)
                    if (surface.rgba[i] != 1) {
                        opaque = false;
                        break;
                    }
            }
            const int channels = opaque ? 3 : 4,
                      bits = options.format == ExportFormat::tiff16 ? 16 : 8;
            const auto count = pixelCount(surface.width, surface.height);
            std::vector<std::uint8_t> pixels8;
            std::vector<std::uint16_t> pixels16;
            if (bits == 16)
                pixels16.resize(count * channels);
            else
                pixels8.resize(count * channels);
            std::vector<float> input(static_cast<std::size_t>(surface.width) * 3),
                encoded(input.size());
            for (int y = 0; y < surface.height; ++y) {
                checkCancellation(stop);
                for (int x = 0; x < surface.width; ++x) {
                    const auto source = (static_cast<std::size_t>(y) * surface.width + x) * 4;
                    for (std::size_t c = 0; c < 3; ++c)
                        input[static_cast<std::size_t>(x) * 3 + c] =
                            surface.rgba[source + c] * (opaque ? surface.rgba[source + 3] : 1);
                }
                color.convert(input, encoded, false);
                for (int x = 0; x < surface.width; ++x) {
                    const auto target =
                        (static_cast<std::size_t>(y) * surface.width + x) * channels;
                    for (int c = 0; c < channels; ++c) {
                        const double value =
                            c == 3
                                ? surface
                                      .rgba[(static_cast<std::size_t>(y) * surface.width + x) * 4 +
                                            3]
                                : encoded[static_cast<std::size_t>(x) * 3 + c];
                        const double unit = std::clamp(value, 0.0, 1.0);
                        if (bits == 16)
                            pixels16[target + c] =
                                static_cast<std::uint16_t>(std::lround(unit * 65535));
                        else
                            pixels8[target + c] =
                                static_cast<std::uint8_t>(std::lround(unit * 255));
                    }
                }
                if (progress)
                    progress(.86 + .1 * (y + 1.0) / surface.height);
            }
            CFHandle profile(CFDataCreate(nullptr, color.profile().data(),
                                          static_cast<CFIndex>(color.profile().size())));
            CFHandle space(profile ? CGColorSpaceCreateWithICCData(profile.get()) : nullptr);
            const void *bytes = bits == 16 ? static_cast<const void *>(pixels16.data())
                                           : static_cast<const void *>(pixels8.data());
            const auto length = bits == 16 ? pixels16.size() * 2 : pixels8.size();
            CFHandle provider(CGDataProviderCreateWithData(nullptr, bytes, length, nullptr));
            const CGBitmapInfo bitmap =
                static_cast<CGBitmapInfo>(opaque ? kCGImageAlphaNone : kCGImageAlphaLast) |
                (bits == 16 ? kCGBitmapByteOrder16Little : kCGBitmapByteOrderDefault);
            CFHandle image(
                space && provider
                    ? CGImageCreate(surface.width, surface.height, bits, bits * channels,
                                    static_cast<std::size_t>(surface.width) * channels * (bits / 8),
                                    space.get(), bitmap, provider.get(), nullptr, false,
                                    kCGRenderingIntentRelativeColorimetric)
                    : nullptr);
            CFHandle url(CFURLCreateFromFileSystemRepresentation(
                nullptr, reinterpret_cast<const UInt8 *>(temporary.path.c_str()),
                static_cast<CFIndex>(temporary.path.string().size()), false));
            CFStringRef type = options.format == ExportFormat::jpeg  ? CFSTR("public.jpeg")
                               : options.format == ExportFormat::png ? CFSTR("public.png")
                                                                     : CFSTR("public.tiff");
            CFHandle writer(url ? CGImageDestinationCreateWithURL(url.get(), type, 1, nullptr)
                                : nullptr);
            if (!image || !writer)
                throw std::runtime_error("Cannot create encoded export image.");
            const int compression = options.compression == TiffCompression::none  ? 1
                                    : options.compression == TiffCompression::lzw ? 5
                                                                                  : 8;
            NSMutableDictionary *properties = [NSMutableDictionary dictionaryWithDictionary:@{
                (__bridge NSString *)
                kCGImageDestinationLossyCompressionQuality : @(options.quality / 100.0),
                (__bridge NSString *)kCGImagePropertyOrientation : @1
            }];
            if (tiff)
                properties[(__bridge NSString *)kCGImagePropertyTIFFDictionary] =
                    @{(__bridge NSString *)kCGImagePropertyTIFFCompression : @(compression)};
            CGImageDestinationAddImageAndMetadata(writer.get(), image.get(), metadata.get(),
                                                  (__bridge CFDictionaryRef)properties);
            checkCancellation(stop);
            if (!CGImageDestinationFinalize(writer.get()))
                throw std::runtime_error("ImageIO could not finish advanced export.");
            checkCancellation(stop);
            if (options.format == ExportFormat::jpeg)
                jpegICC(temporary.path, color.profile());
            if (tiff)
                verifyTIFF(temporary.path, options);
        }
        const auto install = [&] { temporary.install(output, options.overwrite, stop); };
        if (installGuard)
            installGuard(install);
        else
            install();
        if (progress)
            progress(1);
    }
}
} // namespace luma::macos
