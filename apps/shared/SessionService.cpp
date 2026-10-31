#include "apps/shared/SessionService.h"
#include "apps/shared/MetadataService.h"
#include "apps/shared/PhotoDecoder.h"
#include "core/image/Bayer.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/FileHasher.h"
#include "platform/macos/ImageExporter.h"
#include <algorithm>
#include <cctype>

namespace luma {
PhotoSession SessionService::open(const std::filesystem::path &source, luma::CancellationToken stop,
                                  const std::function<void(double)> &progress) const {
    const auto path = std::filesystem::canonical(source);
    const auto originalSize = std::filesystem::file_size(path);
    const auto originalTime = std::filesystem::last_write_time(path);
    if (originalSize > 1'300'000'000) {
        throw std::runtime_error("Image exceeds the input budget.");
    }
    const auto checksum = macos::sha256File(path, stop);
    // Only decoded pixels are cached. Selection and saved edits always come
    // from the current catalog transaction, including on a cache hit.
    const auto key = checksum + "|decode-v2|preview-2560-1024-512";
    const auto generation = previews_->generation();
    auto cached = previews_->get(key);
    if (!cached) {
        auto decoded = PhotoDecoder().decode(path, stop, progress);
        auto preview = std::make_shared<FloatImage>(standardPreview(decoded.image, 2560, stop));
        auto interactive = std::make_shared<FloatImage>(standardPreview(decoded.image, 1024, stop));
        auto quick = std::make_shared<FloatImage>(standardPreview(decoded.image, 512, stop));
        const auto scale = [&](const FloatImage &image) {
            return std::min(static_cast<double>(image.width) / decoded.image.width,
                            static_cast<double>(image.height) / decoded.image.height);
        };
        cached = std::make_shared<PreviewSource>(PreviewSource{
            preview, interactive, decoded.metadata, decoded.calibration, decoded.diagnostics,
            scale(*preview), scale(*interactive), quick, scale(*quick)});
    }
    checkCancellation(stop);
    if (originalSize != std::filesystem::file_size(path) ||
        originalTime != std::filesystem::last_write_time(path)) {
        throw std::runtime_error(
            "Original changed while it was being decoded. Import was not committed.");
    }
    Catalog catalog(catalogPath_);
    auto metadata = cached->metadata;
    MetadataService::readSourceMetadata(path, metadata, stop);
    auto asset = catalog.addAndSelect(path, checksum, metadata);
    const auto bytes =
        (cached->standard->rgb.capacity() + cached->standard->alpha.capacity() +
         cached->interactive->rgb.capacity() + cached->interactive->alpha.capacity() +
         cached->quick->rgb.capacity() + cached->quick->alpha.capacity()) *
        sizeof(float);
    previews_->put(key, cached, bytes, generation);
    if (progress)
        progress(1);
    return {std::move(asset),         cached->standard,      cached->calibration,
            cached->diagnostics,      cached->standardScale, cached->interactive,
            cached->interactiveScale, cached->quick,         cached->quickScale};
}
std::shared_ptr<const DecodedImage> SessionService::fullResolution(const AssetRecord &asset,
                                                                   CancellationToken stop) const {
    const auto path = std::filesystem::canonical(asset.path);
    const auto size = std::filesystem::file_size(path);
    const auto time = std::filesystem::last_write_time(path);
    if (size > 1'300'000'000)
        throw std::runtime_error("Image exceeds the input budget.");
    if (macos::sha256File(path, stop) != asset.checksum)
        throw std::runtime_error("Original changed; full-resolution preview refused.");
    const auto key = asset.checksum + "|decode-v2|full";
    const auto generation = originals_->generation();
    auto decoded = originals_->get(key);
    if (!decoded)
        decoded = std::make_shared<DecodedImage>(PhotoDecoder().decode(path, stop));
    checkCancellation(stop);
    if (size != std::filesystem::file_size(path) || time != std::filesystem::last_write_time(path))
        throw std::runtime_error("Original changed during full-resolution decoding.");
    const auto bytes =
        (decoded->image.rgb.capacity() + decoded->image.alpha.capacity()) * sizeof(float);
    originals_->put(key, decoded, bytes, generation);
    return decoded;
}
void SessionService::setCacheBudget(std::size_t bytes) {
    previews_->setBudget(bytes / 3);
    originals_->setBudget(bytes - bytes / 3);
}
void SessionService::clearCache() {
    previews_->clear();
    originals_->clear();
}
std::optional<PhotoSession>
SessionService::restore(luma::CancellationToken stop,
                        const std::function<void(double)> &progress) const {
    Catalog catalog(catalogPath_);
    auto active = catalog.activeAsset();
    if (!active) {
        return std::nullopt;
    }
    return open(active->path, stop, progress);
}
void SessionService::save(const std::string &assetID, double exposure) const {
    Catalog catalog(catalogPath_);
    catalog.saveExposure(assetID, exposure);
}
void SessionService::save(const std::string &assetID, EditDocument edits) const {
    Catalog(catalogPath_).saveEdits(assetID, edits);
}
void SessionService::exportPhoto(const AssetRecord &asset, EditDocument edits,
                                 const std::filesystem::path &destination, ExportOptions options,
                                 CancellationToken stop,
                                 const std::function<void(double)> &progress) const {
    edits.validate();
    options.validate();
    const auto source = std::filesystem::canonical(asset.path);
    const auto target = std::filesystem::weakly_canonical(destination);
    const auto catalog = std::filesystem::weakly_canonical(catalogPath_);
    if (target == catalog || (std::filesystem::exists(target) && std::filesystem::exists(catalog) &&
                              std::filesystem::equivalent(target, catalog)))
        throw std::runtime_error("Export cannot replace the catalog.");
    if (source == target ||
        (std::filesystem::exists(target) && std::filesystem::equivalent(source, target)))
        throw std::runtime_error("Export cannot replace the original image.");
    if (Catalog(catalogPath_).originalAtPath(target, stop))
        throw std::runtime_error("Export cannot replace a catalog original.");
    for (const auto &name : {catalog.filename().string() + "-wal",
                             catalog.filename().string() + "-shm", std::string("presets.json"),
                             std::string("export-settings.json"), std::string("preview.ini")}) {
        const auto state = catalog.parent_path() / name;
        if (target == state || (std::filesystem::exists(target) && std::filesystem::exists(state) &&
                                std::filesystem::equivalent(target, state)))
            throw std::runtime_error("Export cannot replace application state.");
    }
    auto extension = destination.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    const bool validExtension =
        options.format == ExportFormat::jpeg      ? (extension == ".jpg" || extension == ".jpeg")
        : options.format == ExportFormat::png     ? extension == ".png"
        : options.format == ExportFormat::dng     ? extension == ".dng"
        : options.format == ExportFormat::heicHDR ? extension == ".heic"
                                                  : (extension == ".tif" || extension == ".tiff");
    if (!validExtension)
        throw std::runtime_error("Destination extension does not match export format.");
    if (!options.overwrite && std::filesystem::exists(target))
        throw std::runtime_error("Destination already exists.");
    const auto initialTime = std::filesystem::last_write_time(source);
    const auto initialSize = std::filesystem::file_size(source);
    if (macos::sha256File(source, stop) != asset.checksum)
        throw std::runtime_error("Original changed; export refused.");
    auto decoded = PhotoDecoder().decode(source, stop, [&](double value) {
        if (progress)
            progress(value * 0.5);
    });
    if (initialTime != std::filesystem::last_write_time(source) ||
        initialSize != std::filesystem::file_size(source))
        throw std::runtime_error("Original changed during export decoding.");
    const auto guard = [&](const std::function<void()> &install) {
        Catalog(catalogPath_).installExport(target, stop, install);
    };
    macos::writeImage(decoded, edits, destination, options, stop, progress, source, guard,
                      asset.descriptive);
}
void SessionService::verify() const {
    Catalog(catalogPath_).verify();
}
} // namespace luma
