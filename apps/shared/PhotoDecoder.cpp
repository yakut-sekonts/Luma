#include "apps/shared/PhotoDecoder.h"
#include "apps/shared/MetadataService.h"
#include "core/image/Bayer.h"
#include "core/raw/RawDecoder.h"
#include "platform/macos/RasterDecoder.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace luma {
namespace {
std::string extension(const std::filesystem::path &path) {
    auto value = path.extension().string();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return value;
}
bool rasterExtension(const std::string &extension) {
    static const std::set<std::string> values{".jpg",  ".jpeg", ".png", ".tif", ".tiff", ".heic",
                                              ".heif", ".webp", ".bmp", ".gif", ".avif"};
    return values.contains(extension);
}
bool raw(const std::filesystem::path &path, CancellationToken stop) {
    // Unknown/RAW extensions retain the previous LibRaw behavior. TIFF containers
    // can expose a preview as public.tiff; recognize their RAW header first.
    const auto value = extension(path);
    if (!rasterExtension(value))
        return true;
    if ((value == ".tif" || value == ".tiff") && RawDecoder().recognizes(path, stop))
        return true;
    return macos::RasterDecoder().cameraRaw(path);
}
} // namespace
bool PhotoDecoder::supportedExtension(const std::filesystem::path &path) {
    const auto value = extension(path);
    static const std::set<std::string> raws{".nef", ".dng", ".cr2", ".cr3",
                                            ".arw", ".rw2", ".orf", ".raf"};
    return raws.contains(value) || macos::RasterDecoder().available(value);
}
PhotoMetadata PhotoDecoder::inspect(const std::filesystem::path &path,
                                    CancellationToken stop) const {
    checkCancellation(stop);
    auto metadata = raw(path, stop) ? RawDecoder().inspect(path, stop)
                                    : macos::RasterDecoder().inspect(path, stop);
    MetadataService::readSourceMetadata(path, metadata, stop);
    return metadata;
}
DecodedImage PhotoDecoder::decode(const std::filesystem::path &path, CancellationToken stop,
                                  const std::function<void(double)> &progress) const {
    checkCancellation(stop);
    auto decoded = raw(path, stop) ? RawDecoder().decode(path, stop, progress)
                                   : macos::RasterDecoder().decode(path, stop, progress);
    MetadataService::readSourceMetadata(path, decoded.metadata, stop);
    return decoded;
}
} // namespace luma
