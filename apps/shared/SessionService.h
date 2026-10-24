#pragma once
#include "core/cache/ByteLruCache.h"
#include "core/catalog/Catalog.h"
#include "core/color/RawColorCalibration.h"
#include "core/export/ExportOptions.h"
#include "core/image/DecodedImage.h"
#include "core/image/FloatImage.h"
#include "core/jobs/Cancellation.h"
#include <functional>
#include <memory>

namespace luma {
struct PhotoSession {
    AssetRecord asset;
    std::shared_ptr<const FloatImage> preview;
    RawColorCalibration calibration;
    double previewScale = 1;
    std::shared_ptr<const FloatImage> interactivePreview;
    double interactiveScale = 1;
    std::shared_ptr<const FloatImage> quickPreview;
    double quickScale = 1;
};
class SessionService {
  public:
    explicit SessionService(std::filesystem::path catalog) : catalogPath_(std::move(catalog)) {}
    PhotoSession open(const std::filesystem::path &source, luma::CancellationToken stop = {},
                      const std::function<void(double)> &progress = {}) const;
    std::optional<PhotoSession> restore(luma::CancellationToken stop = {},
                                        const std::function<void(double)> &progress = {}) const;
    void save(const std::string &assetID, double exposure) const;
    void save(const std::string &assetID, EditDocument edits) const;
    void exportPhoto(const AssetRecord &asset, EditDocument edits,
                     const std::filesystem::path &destination, ExportOptions options,
                     CancellationToken stop = {},
                     const std::function<void(double)> &progress = {}) const;
    void verify() const;
    std::shared_ptr<const DecodedImage> fullResolution(const AssetRecord &asset,
                                                       CancellationToken stop = {}) const;
    void setCacheBudget(std::size_t bytes);
    void clearCache();
    CacheStatistics previewCacheStatistics() const {
        return previews_->statistics();
    }
    CacheStatistics sourceCacheStatistics() const {
        return originals_->statistics();
    }

  private:
    struct PreviewSource {
        std::shared_ptr<const FloatImage> standard, interactive;
        PhotoMetadata metadata;
        RawColorCalibration calibration;
        double standardScale, interactiveScale;
        std::shared_ptr<const FloatImage> quick;
        double quickScale;
    };
    std::filesystem::path catalogPath_;
    std::shared_ptr<ByteLruCache<std::string, PreviewSource>> previews_ =
        std::make_shared<ByteLruCache<std::string, PreviewSource>>(64ULL * 1024 * 1024);
    std::shared_ptr<ByteLruCache<std::string, DecodedImage>> originals_ =
        std::make_shared<ByteLruCache<std::string, DecodedImage>>(128ULL * 1024 * 1024);
};
} // namespace luma
