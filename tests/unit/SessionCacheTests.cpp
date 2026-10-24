#include "apps/shared/SessionService.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/FileHasher.h"
#include <QCoreApplication>
#include <QImage>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void source(const std::filesystem::path &path, int variant) {
    QImage image(1200, 800, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height(); ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            row[x * 4] = (x + variant * 23) % 256;
            row[x * 4 + 1] = (y + variant * 37) % 256;
            row[x * 4 + 2] = (x + y + variant * 51) % 256;
            row[x * 4 + 3] = x % 5 ? 255 : 128;
        }
    }
    require(image.save(QString::fromStdString(path.string())), "Cannot create own PNG fixture.");
}
bool samePixels(const luma::FloatImage &a, const luma::FloatImage &b) {
    return a.width == b.width && a.height == b.height && a.rgb == b.rgb && a.alpha == b.alpha;
}
template <typename F> void refuses(F action, const char *message) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, message);
}
void run(const std::filesystem::path &root) {
    const auto aPath = root / "first.png", bPath = root / "second.png";
    source(aPath, 0);
    source(bPath, 1);
    const auto aHash = luma::macos::sha256File(aPath), bHash = luma::macos::sha256File(bPath);
    const auto catalogPath = root / "catalog.sqlite";
    luma::SessionService service(catalogPath);
    const auto first = service.open(aPath);
    require(first.preview->width == 1200 && first.preview->height == 800 &&
                first.interactivePreview->width == 1024 && first.interactivePreview->height < 800 &&
                first.previewScale == 1 && first.interactiveScale < 1 && first.quickPreview &&
                first.quickPreview->width == 512 && first.quickScale < first.interactiveScale,
            "Session did not produce distinct standard and interaction resolutions.");
    const auto cost = service.previewCacheStatistics().usedBytes;
    require(cost > 0 && service.previewCacheStatistics().entries == 1,
            "Actual decoded preview was not retained.");
    const auto hit = service.open(aPath);
    require(hit.preview == first.preview && hit.interactivePreview == first.interactivePreview &&
                hit.quickPreview == first.quickPreview,
            "Same checksum did not reuse decoded preview storage.");
    auto edits = first.asset.edits;
    edits.exposure = 1.25;
    edits.tint = 25;
    edits.customWhiteBalance = true;
    service.save(first.asset.id, edits);
    const auto restored = service.restore();
    require(restored && restored->asset.id == first.asset.id && restored->asset.edits == edits &&
                restored->preview == first.preview,
            "A cache hit restored stale edits instead of the current catalog document.");
    const auto full = service.fullResolution(restored->asset);
    require(full->image.width == 1200 && full->image.height == 800 &&
                service.fullResolution(restored->asset) == full,
            "Full-resolution source cache failed its dimensions/identity contract.");

    require(samePixels(*first.quickPreview, luma::standardPreview(full->image, 512)),
            "Quick preview must be generated directly from the decoded source, including alpha.");
    const auto bytes = [](const auto &image) {
        return (image->rgb.capacity() + image->alpha.capacity()) * sizeof(float);
    };
    require(cost ==
                bytes(first.preview) + bytes(first.interactivePreview) + bytes(first.quickPreview),
            "Decoded cache budget must include all three preview allocations.");

    service.setCacheBudget(cost * 3); // Public split gives previews exactly one entry's cost.
    service.open(bPath);
    require(service.previewCacheStatistics().entries == 1 &&
                service.previewCacheStatistics().usedBytes <= cost,
            "Actual preview cache retained both sources beyond its one-entry byte budget.");
    const auto regenerated = service.open(aPath);
    require(regenerated.preview != first.preview &&
                regenerated.interactivePreview != first.interactivePreview &&
                regenerated.quickPreview != first.quickPreview &&
                samePixels(*regenerated.quickPreview, *first.quickPreview) &&
                samePixels(*regenerated.preview, *first.preview) &&
                samePixels(*regenerated.interactivePreview, *first.interactivePreview) &&
                regenerated.asset.id == first.asset.id && regenerated.asset.edits == edits,
            "Eviction did not transparently regenerate identical pixels and current edits.");

    service.setCacheBudget(0);
    const auto uncached = service.open(aPath);
    const auto fullUncached = service.fullResolution(uncached.asset);
    require(service.previewCacheStatistics().usedBytes == 0 &&
                service.sourceCacheStatistics().usedBytes == 0 &&
                service.previewCacheStatistics().entries == 0 &&
                service.sourceCacheStatistics().entries == 0 &&
                uncached.preview != regenerated.preview && fullUncached != full &&
                uncached.quickPreview != regenerated.quickPreview &&
                samePixels(*uncached.quickPreview, *first.quickPreview) &&
                samePixels(*uncached.preview, *first.preview) &&
                samePixels(fullUncached->image, full->image) && uncached.asset.edits == edits,
            "Zero cache budget lost active data or retained optional decoded storage.");
    service.setCacheBudget(cost * 3);
    const auto resumed = service.open(aPath);
    require(service.open(aPath).preview == resumed.preview,
            "Decoded cache retention did not resume after the budget was restored.");
    const auto fullResumed = service.fullResolution(resumed.asset);
    require(service.fullResolution(resumed.asset) == fullResumed,
            "Full-resolution cache retention did not resume.");
    service.clearCache();
    require(service.previewCacheStatistics().usedBytes == 0 &&
                service.sourceCacheStatistics().usedBytes == 0 &&
                samePixels(*resumed.preview, *first.preview) &&
                samePixels(fullResumed->image, full->image),
            "Explicit cache clear changed pixels still retained by active clients.");
    bool purgedDuringDecode = false;
    const auto late = service.open(aPath, {}, [&](double progress) {
        if (!purgedDuringDecode && progress > 0 && progress < 1) {
            service.clearCache();
            purgedDuringDecode = true;
        }
    });
    require(purgedDuringDecode && service.previewCacheStatistics().usedBytes == 0 &&
                service.previewCacheStatistics().entries == 0 &&
                samePixels(*late.preview, *first.preview) && late.asset.edits == edits,
            "A real decode completed after purge and repopulated a stale cache generation.");
    // Populate again before testing changed-source rejection.
    service.fullResolution(resumed.asset);
    luma::CancellationSource cancelled;
    cancelled.cancel();
    refuses([&] { service.open(bPath, cancelled.token()); }, "Cancelled open was accepted.");
    refuses([&] { service.fullResolution(resumed.asset, cancelled.token()); },
            "A full-resolution cache hit bypassed cancellation.");
    const auto active = luma::Catalog(catalogPath).activeAsset();
    require(active && active->id == first.asset.id && active->edits == edits &&
                luma::Catalog(catalogPath).libraryCount() == 2,
            "Cancellation changed catalog selection, membership or saved edits.");
    require(luma::macos::sha256File(aPath) == aHash && luma::macos::sha256File(bPath) == bHash,
            "Cache/open/restore/cancellation modified an original fixture.");

    // Deliberate mutation of this test's own generated source, never user data.
    source(aPath, 2);
    require(luma::macos::sha256File(aPath) != aHash, "Changed-source negative control is invalid.");
    refuses([&] { service.fullResolution(resumed.asset); },
            "A warm full-resolution cache hid a changed original checksum.");
    const auto durable = luma::Catalog(catalogPath).activeAsset();
    require(durable && durable->checksum == aHash && durable->edits == edits,
            "Refused changed source silently replaced catalog identity or edits.");
    service.verify();
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    try {
        require(temporary.isValid(), "Cannot create an isolated cache fixture directory.");
        run(temporary.path().toStdString());
        std::cout
            << "Session caches: real PNG decode/hits, fresh saved edits, eviction/regeneration, "
               "zero-budget operation/resume, active ownership, cancellation and changed-source "
               "rejection passed. No OS pressure or performance claim.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
