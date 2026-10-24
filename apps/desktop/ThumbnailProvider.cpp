#include "apps/desktop/ThumbnailProvider.h"
#if defined(LUMA_UX_THUMBNAIL_TRACE)
#include "tests/ux/ThumbnailCpuRenderProbe.h"
#include "tests/ux/ThumbnailResponseTrace.h"
#endif
#include "apps/shared/PhotoDecoder.h"
#include "apps/shared/PresetStore.h"
#include "core/catalog/Catalog.h"
#include "core/image/Bayer.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/FileHasher.h"
#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QImageReader>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QSaveFile>
#include <algorithm>
#include <mutex>
#include <vector>

struct ThumbnailState {
    std::mutex mutex, cacheMutex;
    std::vector<luma::CancellationSource> active;
    bool shuttingDown = false;
    bool cacheBudgetChecked = false; // Protected by cacheMutex, including the first cache hit.
};
namespace {
void trimCache(const std::filesystem::path &cache) {
    std::vector<std::filesystem::directory_entry> entries;
    std::uintmax_t size = 0;
    for (const auto &entry : std::filesystem::directory_iterator(cache)) {
        if (entry.is_regular_file() && entry.path().extension() == ".jpg") {
            entries.push_back(entry);
            size += entry.file_size();
        }
    }
    constexpr std::uintmax_t budget = 512ULL * 1024 * 1024;
    if (size <= budget)
        return;
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return a.last_write_time() < b.last_write_time();
    });
    for (const auto &entry : entries) {
        if (size <= budget)
            break;
        size -= entry.file_size();
        std::filesystem::remove(entry.path());
    }
}
QImage thumbnail(const std::filesystem::path &catalog, const QString &id,
                 const std::shared_ptr<ThumbnailState> &state, const luma::CancellationToken &stop
#if defined(LUMA_UX_THUMBNAIL_TRACE)
                 ,
                 const ux::thumbnail_trace::Token &trace
#endif
) {
    luma::checkCancellation(stop);
    luma::Catalog connection(catalog);
    const auto snapshot = connection.assetSnapshot(id.toStdString());
    if (!snapshot)
        throw std::runtime_error("Photo is not in the catalog.");
#if defined(LUMA_UX_THUMBNAIL_TRACE)
    trace.note(ux::thumbnail_trace::Kind::SnapshotRead, snapshot->revision);
#endif
    const auto &asset = snapshot->asset;
    const auto encoded =
        luma::encodePreset({"Thumbnail", {}, asset.edits, luma::allSettingsGroups});
    const auto identity =
        QByteArray::fromStdString(asset.checksum) + "|" + QByteArray::number(snapshot->revision) +
        "|" + encoded + "|local-v2|" +
        QByteArray::fromStdString(luma::encodeLocalMasks(asset.edits.localMasks)) + "|retouch-v1|" +
        QByteArray::fromStdString(luma::encodeRetouch(asset.edits.retouch));
    const auto fingerprint = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
    const auto directory =
        catalog.parent_path() / (catalog.filename().string() + ".luma-cache") / "thumbnails";
    const auto cached = directory / (fingerprint.toStdString() + "-edited-v4.jpg");
    {
        std::lock_guard lock(state->cacheMutex);
        QImageReader reader(QString::fromStdString(cached.string()), "JPEG");
        const auto dimensions = reader.size();
        const QImage image =
            dimensions.isValid() && dimensions.width() <= 384 && dimensions.height() <= 384
                ? reader.read()
                : QImage();
        if (!image.isNull()) {
            std::error_code error;
            std::filesystem::last_write_time(cached, std::filesystem::file_time_type::clock::now(),
                                             error);
            luma::checkCancellation(stop);
            // Recover a persisted oversized cache even when this session only
            // reads existing thumbnails. Later hits need no directory scan;
            // every new cache publication continues to enforce the same cap.
            if (!state->cacheBudgetChecked) {
                trimCache(directory);
                state->cacheBudgetChecked = true;
            }
            return image;
        }
    }
    if (luma::macos::sha256File(asset.path, stop) != asset.checksum)
        throw std::runtime_error("Original changed; thumbnail not regenerated.");
    auto decoded = luma::PhotoDecoder().decode(asset.path, stop);
    auto small = luma::standardPreview(decoded.image, 384, stop);
    const double scale = std::min(static_cast<double>(small.width) / decoded.image.width,
                                  static_cast<double>(small.height) / decoded.image.height);
#if defined(LUMA_UX_THUMBNAIL_TRACE)
    auto pixels = ux::thumbnail_trace::renderThumbnailWithCpuProbe(
        trace, snapshot->revision, small, asset.edits, asset.metadata.orientation, {}, stop,
        decoded.calibration, luma::DetailDiagnostic::none, scale, asset.metadata);
#else
    auto pixels =
        luma::renderCPU(small, asset.edits, asset.metadata.orientation, {}, stop,
                        decoded.calibration, luma::DetailDiagnostic::none, scale, asset.metadata);
#endif
    if (!decoded.image.alpha.empty()) {
        for (std::size_t i = 0; i < pixels.rgba.size(); i += 4) {
            for (std::size_t c = 0; c < 3; ++c)
                pixels.rgba[i + c] = static_cast<std::uint8_t>(
                    (static_cast<unsigned>(pixels.rgba[i + c]) * pixels.rgba[i + 3] + 127) / 255);
            pixels.rgba[i + 3] = 255;
        }
    }
    QImage image = QImage(pixels.rgba.data(), pixels.width, pixels.height,
                          static_cast<qsizetype>(pixels.width) * 4, QImage::Format_RGBA8888)
                       .copy();
    image.setColorSpace(QColorSpace::SRgb);
    if (luma::macos::sha256File(asset.path, stop) != asset.checksum)
        throw std::runtime_error("Original changed while generating its thumbnail.");
    luma::checkCancellation(stop);
    QByteArray jpeg;
    QBuffer encodedImage(&jpeg);
    if (!encodedImage.open(QIODevice::WriteOnly) || !image.save(&encodedImage, "JPEG", 85))
        throw std::runtime_error("Cannot encode thumbnail cache.");
    QImage compressed = QImage::fromData(jpeg, "JPEG");
    if (compressed.isNull())
        throw std::runtime_error("Cannot verify encoded thumbnail cache.");
    luma::checkCancellation(stop);
    {
        std::lock_guard lock(state->cacheMutex);
        std::filesystem::create_directories(directory);
        QSaveFile file(QString::fromStdString(cached.string()));
        if (!file.open(QIODevice::WriteOnly) || file.write(jpeg) != jpeg.size() || !file.commit())
            throw std::runtime_error("Cannot write thumbnail cache.");
        trimCache(directory);
        state->cacheBudgetChecked = true;
    }
    return compressed;
}
class ThumbnailResponse final : public QQuickImageResponse, public QRunnable {
  public:
    ThumbnailResponse(std::filesystem::path catalog, QString id, QSize size,
                      std::shared_ptr<ThumbnailState> state)
        : catalog_(std::move(catalog)), id_(std::move(id)), size_(size), state_(std::move(state)) {
        setAutoDelete(false);
        std::lock_guard lock(state_->mutex);
        allowed_ = !state_->shuttingDown && state_->active.size() < 96;
        if (allowed_)
            state_->active.push_back(cancellation_);
    }
#if defined(LUMA_UX_THUMBNAIL_TRACE)
    void setTrace(ux::thumbnail_trace::Token trace) noexcept {
        trace_ = std::move(trace);
    }
#endif
    QQuickTextureFactory *textureFactory() const override {
#if defined(LUMA_UX_THUMBNAIL_TRACE)
        trace_.note(ux::thumbnail_trace::Kind::TextureFactoryCalled);
#endif
        return QQuickTextureFactory::textureFactoryForImage(image_);
    }
    QString errorString() const override {
        return error_;
    }
    void cancel() override {
        cancellation_.cancel();
#if defined(LUMA_UX_THUMBNAIL_TRACE)
        trace_.note(ux::thumbnail_trace::Kind::Cancelled);
#endif
    }
    void run() override {
        try {
            if (!allowed_)
                throw std::runtime_error("Thumbnail queue busy.");
#if defined(LUMA_UX_THUMBNAIL_TRACE)
            image_ = thumbnail(catalog_, id_, state_, cancellation_.token(), trace_);
#else
            image_ = thumbnail(catalog_, id_, state_, cancellation_.token());
#endif
            if (size_.isValid())
                image_ = image_.scaled(size_.boundedTo(QSize(384, 384)).boundedTo(image_.size()),
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation);
        } catch (const std::exception &error) {
            error_ = QString::fromUtf8(error.what());
        }
        if (allowed_) {
            std::lock_guard lock(state_->mutex);
            // Sources are shared atomic tokens; remove this exact token via its assigned ID.
            auto &active = state_->active;
            const auto iterator =
                std::find_if(active.begin(), active.end(), [this](const auto &source) {
                    return source.sameState(cancellation_);
                });
            if (iterator != active.end())
                active.erase(iterator);
        }
#if defined(LUMA_UX_THUMBNAIL_TRACE)
        // Pool worker only; both state/cache mutex scopes ended before this point.
        trace_.beforeFinishedQueue(error_.isEmpty() && !image_.isNull());
#endif
        QMetaObject::invokeMethod(this, [this] { emit finished(); }, Qt::QueuedConnection);
    }

  private:
    std::filesystem::path catalog_;
    QString id_, error_;
    QSize size_;
    std::shared_ptr<ThumbnailState> state_;
    luma::CancellationSource cancellation_;
    QImage image_;
    bool allowed_ = false;
#if defined(LUMA_UX_THUMBNAIL_TRACE)
    ux::thumbnail_trace::Token trace_;
#endif
};
} // namespace
ThumbnailProvider::ThumbnailProvider(std::filesystem::path catalog)
    : catalog_(std::move(catalog)), state_(std::make_shared<ThumbnailState>()) {
#if defined(LUMA_UX_THUMBNAIL_TRACE)
    uxTraceSlot_ = ux::thumbnail_trace::makeSlot();
#endif
    pool_.setMaxThreadCount(2);
}
ThumbnailProvider::~ThumbnailProvider() {
    {
        std::lock_guard lock(state_->mutex);
        state_->shuttingDown = true;
        for (const auto &source : state_->active)
            source.cancel();
    }
    pool_.waitForDone();
}
QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id,
                                                             const QSize &requestedSize) {
    auto *response = new ThumbnailResponse(catalog_, id.section('?', 0, 0), requestedSize, state_);
#if defined(LUMA_UX_THUMBNAIL_TRACE)
    response->setTrace(
        ux::thumbnail_trace::observeActualResponse(uxTraceSlot_, response, id, requestedSize));
#endif
    pool_.start(response);
    return response;
}
