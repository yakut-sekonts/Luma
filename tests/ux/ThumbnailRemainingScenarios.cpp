#include "apps/shared/PhotoDecoder.h"
#include "apps/shared/PresetStore.h"
#include "core/catalog/Catalog.h"
#include "core/render/CpuRenderer.h"
#include "tests/ux/ThumbnailResponseTrace.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QBuffer>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QQuickItemGrabResult>
#include <QSaveFile>
#include <QSet>
#include <QSignalSpy>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <poll.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace ux {
namespace {
QImage samples(QImage image) {
    image = image.convertToFormat(QImage::Format_RGBA8888);
    image.setDevicePixelRatio(1);
    image.setColorSpace({});
    return image;
}
QByteArray bytes(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read owned thumbnail evidence: " + path);
    return file.readAll();
}
QByteArray hash(const QString &path) {
    return QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256);
}
QString cachePath(Context &c, const luma::AssetSnapshot &snapshot) {
    // Storage-key reproduction is used only to place a private input fixture.
    // A successful real reader connection proves the provider actually uses it.
    const auto &asset = snapshot.asset;
    const auto encoded =
        luma::encodePreset({"Thumbnail", {}, asset.edits, luma::allSettingsGroups});
    const auto identity =
        QByteArray::fromStdString(asset.checksum) + "|" + QByteArray::number(snapshot.revision) +
        "|" + encoded + "|local-v2|" +
        QByteArray::fromStdString(luma::encodeLocalMasks(asset.edits.localMasks)) + "|retouch-v1|" +
        QByteArray::fromStdString(luma::encodeRetouch(asset.edits.retouch));
    const auto fingerprint = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
    return c.catalogFile + ".luma-cache/thumbnails/" + QString::fromLatin1(fingerprint) +
           "-edited-v4.jpg";
}
QByteArray thumbnailJPEG(const luma::AssetSnapshot &snapshot) {
    const auto &asset = snapshot.asset;
    const auto decoded = luma::PhotoDecoder().decode(asset.path);
    require(decoded.image.width <= 512 && decoded.image.height <= 512 &&
                decoded.image.alpha.empty(),
            "Thumbnail oracle requires small opaque public-derived originals");
    const auto small = luma::standardPreview(decoded.image, 384);
    const double scale = std::min(static_cast<double>(small.width) / decoded.image.width,
                                  static_cast<double>(small.height) / decoded.image.height);
    const auto pixels =
        luma::renderCPU(small, asset.edits, asset.metadata.orientation, {}, {}, decoded.calibration,
                        luma::DetailDiagnostic::none, scale, asset.metadata);
    auto image = QImage(pixels.rgba.data(), pixels.width, pixels.height,
                        static_cast<qsizetype>(pixels.width) * 4, QImage::Format_RGBA8888)
                     .copy();
    image.setColorSpace(QColorSpace::SRgb);
    QByteArray encoded;
    QBuffer buffer(&encoded);
    require(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "JPEG", 85) &&
                !QImage::fromData(encoded, "JPEG").isNull(),
            "Cannot construct independent valid JPEG thumbnail oracle");
    return encoded;
}
luma::AssetSnapshot snapshot(Context &c, const QString &asset) {
    const auto saved =
        luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(asset.toStdString());
    if (!saved)
        throw std::runtime_error("Expected thumbnail asset is absent from the owned catalog");
    return *saved;
}
QQuickItem *imageFor(QQuickItem *card) {
    auto *image = card->findChild<QQuickItem *>("photoThumbnail");
    require(image, "Actual PhotoCard thumbnail Image is missing");
    return image;
}
bool ready(QQuickItem *card) {
    if (!card)
        return false;
    auto *image = imageFor(card);
    const auto base = card->property("thumbnailURL").toString();
    const int retry = card->property("retryCount").toInt();
    const auto source =
        base +
        (retry ? (base.contains('?') ? "&retry=" : "?retry=") + QString::number(retry) : QString{});
    return retry >= 0 && retry <= 2 && image->property("status").toInt() == 1 &&
           image->property("source").toUrl().toString() == source;
}
double meanRGB(const QImage &first, const QImage &second, int border = 0) {
    require(first.size() == second.size() && first.width() > 2 * border &&
                first.height() > 2 * border,
            "Pixel comparison dimensions are not comparable");
    double error = 0;
    std::uint64_t count = 0;
    for (int y = border; y < first.height() - border; ++y)
        for (int x = border; x < first.width() - border; ++x) {
            const auto a = first.pixelColor(x, y);
            const auto b = second.pixelColor(x, y);
            error += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
                     std::abs(a.blue() - b.blue());
            count += 3;
        }
    return error / static_cast<double>(count);
}
void cardPixels(Context &c, QQuickItem *card, const luma::AssetSnapshot &saved) {
    auto &d = *c.driver;
    const QPointer<QQuickItem> cardGuard(card);
    auto *image = imageFor(card);
    const QPointer<QQuickItem> imageGuard(image);
    const auto asset = QString::fromStdString(saved.asset.id);
    const auto source = image->property("source").toUrl().toString();
    const auto baseURL = card->property("thumbnailURL").toString();
    const auto geometry = image->size();
    const int row = card->property("index").toInt();
    require(ready(card) && image->isVisible() && card->isVisible() &&
                card->property("assetID").toString() == asset &&
                baseURL.startsWith("image://thumbnail/" + asset + "?generation=") && row >= 0 &&
                row < c.library.count() &&
                c.library.data(c.library.index(row), LibraryModel::AssetID).toString() == asset &&
                c.library.data(c.library.index(row), LibraryModel::Path).toUrl().toLocalFile() ==
                    QString::fromStdString(saved.asset.path.string()) &&
                hash(QString::fromStdString(saved.asset.path.string())).toHex().toStdString() ==
                    saved.asset.checksum,
            "Card pixels lack exact current model/source/private-original identity");
    const auto expected = samples(QImage::fromData(thumbnailJPEG(saved), "JPEG"));
    const auto cached = cachePath(c, saved);
    require(QFileInfo(cached).isFile() && !QFileInfo(cached).isSymLink() &&
                samples(QImage(cached)) == expected,
            "Actual provider cache differs from the independent saved-document JPEG pixels");
    const auto capture = image->grabToImage();
    require(!capture.isNull(), "Cannot start thumbnail Image scene readback");
    d.wait([&] { return !cardGuard || !imageGuard || !capture->image().isNull(); },
           "actual thumbnail Image scene readback ready");
    require(cardGuard && imageGuard && ready(cardGuard) &&
                imageFor(cardGuard.data()) == imageGuard.data() &&
                cardGuard->property("assetID").toString() == asset &&
                cardGuard->property("index").toInt() == row &&
                cardGuard->property("thumbnailURL").toString() == baseURL &&
                imageGuard->property("source").toUrl().toString() == source &&
                imageGuard->size() == geometry,
            "Actual card was destroyed/rebound while its pixel readback was pending");
    const auto actual = samples(capture->image());
    const auto scaled =
        expected.scaled(actual.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const auto rectangle =
        QRect((actual.width() - scaled.width()) / 2, (actual.height() - scaled.height()) / 2,
              scaled.width(), scaled.height());
    require(rectangle.width() > 12 && rectangle.height() > 12,
            "Thumbnail painted region is too small for its asset pixel oracle");
    const double error = meanRGB(actual.copy(rectangle), samples(scaled), 4);
    d.log({{"action", "thumbnail_card_pixel_oracle"},
           {"asset", QString::fromStdString(saved.asset.id)},
           {"revision", static_cast<qint64>(saved.revision)},
           {"source", image->property("source").toUrl().toString()},
           {"scene_mean_rgb_error", error},
           {"scene_mean_rgb_limit", 4.0},
           {"exact_cache_pixels", true}});
    // The cache oracle above is bit-exact. Scene readback permits only this
    // declared numerical resampling tolerance, independently of the source URL.
    require(error <= 4.0, "Visible card pixels belong to another asset or saved edit");
}

// Block the genuine production QImageReader on its own cache input. No event
// payloads, provider replacement, fake response, shipping hook or public write.
class CacheReadGate {
  public:
    CacheReadGate(Context &c, QString path, QByteArray payload)
        : c_(c), path_(std::move(path)), backup_(path_ + ".gate-regular"),
          payload_(std::move(payload)) {}
    CacheReadGate(const CacheReadGate &) = delete;
    CacheReadGate &operator=(const CacheReadGate &) = delete;
    ~CacheReadGate() {
        if (armed_)
            finish();
    }
    void arm() {
        require(path_.startsWith(c_.catalogFile + ".luma-cache/thumbnails/") &&
                    !QImage::fromData(payload_, "JPEG").isNull() && !QFileInfo::exists(path_) &&
                    !QFileInfo(path_).isSymLink() && !QFileInfo::exists(backup_) &&
                    !QFileInfo(backup_).isSymLink(),
                "Unsafe or pre-existing owned thumbnail FIFO fixture");
        require(QDir().mkpath(QFileInfo(path_).absolutePath()),
                "Cannot prepare owned thumbnail cache");
        require(!QFileInfo(c_.catalogFile).isSymLink() &&
                    !QFileInfo(c_.catalogFile + ".luma-cache").isSymLink() &&
                    !QFileInfo(QFileInfo(path_).absolutePath()).isSymLink(),
                "Private thumbnail gate must not follow catalog/cache symlinks");
        const auto root = std::filesystem::canonical(c_.root.toStdString());
        const auto directory =
            std::filesystem::canonical(QFileInfo(path_).absolutePath().toStdString());
        require(QString::fromStdString(directory.string())
                    .startsWith(QString::fromStdString(root.string()) + '/'),
                "Cache FIFO directory resolves outside this case's private root");
        QSaveFile regular(backup_);
        require(regular.open(QIODevice::WriteOnly) && regular.write(payload_) == payload_.size() &&
                    regular.commit(),
                "Cannot prepare the valid regular JPEG before arming cache FIFO");
        require(::mkfifo(path_.toUtf8().constData(), 0600) == 0,
                "Cannot create private cache FIFO");
        armed_ = true;
        struct stat value{};
        require(::lstat(path_.toUtf8().constData(), &value) == 0 && S_ISFIFO(value.st_mode),
                "Owned cache gate is not a FIFO");
    }
    bool connectToRealReader() {
        require(armed_ && written_ == 0, "Cache gate is not an unopened input boundary");
        if (descriptor_ >= 0)
            return true;
        // O_WRONLY|O_NONBLOCK succeeds only if an actual reader has already
        // opened/waits to open this FIFO. The fixture never opens a reader here.
        descriptor_ = ::open(path_.toUtf8().constData(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (descriptor_ < 0) {
            require(errno == ENXIO || errno == EINTR, "Unexpected owned FIFO open failure");
            return false;
        }
#ifdef F_SETNOSIGPIPE
        require(::fcntl(descriptor_, F_SETNOSIGPIPE, 1) == 0,
                "Cannot suppress SIGPIPE on owned gate FD");
#else
        require(false, "This native cache-gate draft requires macOS F_SETNOSIGPIPE");
#endif
        connected_ = true;
        return true;
    }
    bool holdingRealReader() const {
        if (!armed_ || !connected_ || descriptor_ < 0 || written_ != 0)
            return false;
        pollfd descriptor{descriptor_, POLLOUT, 0};
        return ::poll(&descriptor, 1, 0) >= 0 &&
               (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0;
    }
    void release() {
        require(holdingRealReader(), "No genuine provider reader was held before release");
        const bool clean = finish();
        require(
            clean && QFileInfo(path_).isFile() && !QFileInfo(path_).isSymLink() &&
                bytes(path_) == payload_,
            "Cache read gate failed to restore regular valid bytes and release its real reader");
    }

  private:
    bool finish() noexcept {
        // Also handles an exception before connectToRealReader: O_RDWR pairs
        // any already-blocked open, then pathname replacement makes all future
        // opens regular-file reads. It never leaves a FIFO for Context teardown.
        if (descriptor_ < 0)
            descriptor_ = ::open(path_.toUtf8().constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
#ifdef F_SETNOSIGPIPE
        if (descriptor_ >= 0)
            ::fcntl(descriptor_, F_SETNOSIGPIPE, 1);
#endif
        bool regular = ::rename(backup_.toUtf8().constData(), path_.toUtf8().constData()) == 0;
        if (!regular) {
            // A failed fixture must still unblock teardown, even if a parent
            // directory or pathname unexpectedly changed. Never follow links.
            ::unlink(path_.toUtf8().constData());
            regular = ::rename(backup_.toUtf8().constData(), path_.toUtf8().constData()) == 0;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (descriptor_ >= 0 && written_ < payload_.size() &&
               std::chrono::steady_clock::now() < deadline) {
            const auto result = ::write(descriptor_, payload_.constData() + written_,
                                        static_cast<std::size_t>(payload_.size() - written_));
            if (result > 0) {
                written_ += result;
                continue;
            }
            if (result < 0 && errno == EINTR)
                continue;
            if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                pollfd descriptor{descriptor_, POLLOUT, 0};
                ::poll(&descriptor, 1, 5);
                continue;
            }
            break;
        }
        if (descriptor_ >= 0)
            ::close(descriptor_);
        descriptor_ = -1;
        armed_ = false;
        return regular && written_ == payload_.size();
    }
    Context &c_;
    QString path_, backup_;
    QByteArray payload_;
    int descriptor_ = -1;
    qsizetype written_ = 0;
    bool armed_ = false, connected_ = false;
};

struct MissingOriginal {
    QString path, backup;
    bool moved = false;
    explicit MissingOriginal(QString value)
        : path(std::move(value)), backup(path + ".owned-backup") {
        require(!QFileInfo::exists(backup) && !QFileInfo(backup).isSymLink() &&
                    QFileInfo(path).isFile() && !QFileInfo(path).isSymLink() &&
                    QFile::rename(path, backup),
                "Cannot hide this case's private original");
        moved = true;
    }
    ~MissingOriginal() {
        if (moved)
            QFile::rename(backup, path);
    }
    void restore() {
        require(moved && !QFileInfo::exists(path) && QFile::rename(backup, path),
                "Cannot restore this case's private original");
        moved = false;
    }
};
using Rows = std::vector<luma::LibraryItem>;
Rows importHidden(Context &c, int count) {
    auto &d = *c.driver;
    require(c.library.count() == 0 && !c.editor.hasPhoto(),
            "Thumbnail fixture needs a new isolated catalog");
    d.type("library.search", "no-such-owned-thumbnail-fixture-73e83d");
    d.wait([&] { return !c.library.loading() && c.library.count() == 0; },
           "empty thumbnail fixture filter");
    const auto directory = c.root + "/owned-thumbnail-originals";
    require(QDir().mkpath(directory), "Cannot create private thumbnail originals directory");
    const auto names = QDir(c.fixture).entryList({"Photo-*.png"}, QDir::Files, QDir::Name);
    require(names.size() >= count, "Not enough read-only public thumbnail fixtures");
    QList<QUrl> sources;
    for (int row = 0; row < count; ++row) {
        const auto destination = directory + '/' + names[row];
        require(QFile::copy(c.fixture + '/' + names[row], destination),
                "Cannot copy private thumbnail original");
        sources.push_back(QUrl::fromLocalFile(destination));
    }
    require(d.object("library.importDialog")->setProperty("sources", QVariant::fromValue(sources)),
            "Cannot supply the file-selection result to the actual Import dialog");
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "owned import dialog opened");
    d.choose("library.importMode", 0);
    if (d.object("library.importDuplicates")->property("checked").toBool())
        d.click("library.importDuplicates");
    d.click("library.importStart");
    d.wait([&] { return !c.library.working() && !c.library.loading() && c.library.count() == 0; },
           "owned originals imported behind the real empty filter", 60000);
    require(c.library.error().isEmpty(), c.library.error());
    d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
           "owned Import dialog exit completed before thumbnail inputs");
    luma::Catalog catalog(c.catalogFile.toStdString());
    require(catalog.libraryCount() == count,
            "Filtered fixture import did not durably commit its whole item set");
    const auto rows = catalog.libraryPage({}, 0, count);
    require(static_cast<int>(rows.size()) == count,
            "Cannot enumerate independent fixture identities");
    for (const auto &row : rows)
        require(QString::fromStdString(row.path.string()).startsWith(directory + '/'),
                "A negative thumbnail fixture points outside its private originals");
    return rows;
}
using Baseline = std::vector<luma::AssetSnapshot>;
Baseline freeze(Context &c, const Rows &rows) {
    Baseline baseline;
    for (const auto &row : rows) {
        auto saved = snapshot(c, QString::fromStdString(row.id));
        require(saved.asset.edits == luma::EditDocument{} &&
                    QString::fromStdString(saved.asset.path.string())
                        .startsWith(c.root + "/owned-thumbnail-originals/") &&
                    hash(QString::fromStdString(saved.asset.path.string())).toHex().toStdString() ==
                        saved.asset.checksum,
                "Private thumbnail baseline is edited, outside the owned root or damaged");
        baseline.push_back(std::move(saved));
    }
    return baseline;
}
void invariant(Context &c, const Baseline &baseline, const luma::AssetSnapshot &changed) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    require(catalog.libraryCount() == static_cast<std::int64_t>(baseline.size()),
            "Thumbnail inputs changed durable catalog membership");
    for (const auto &before : baseline) {
        const auto after = snapshot(c, QString::fromStdString(before.asset.id));
        const bool edited = before.asset.id == changed.asset.id;
        require(after.asset.path == before.asset.path &&
                    after.asset.checksum == before.asset.checksum &&
                    after.asset.edits == (edited ? changed.asset.edits : before.asset.edits) &&
                    after.revision == (edited ? changed.revision : before.revision) &&
                    hash(QString::fromStdString(after.asset.path.string())).toHex().toStdString() ==
                        before.asset.checksum,
                "Thumbnail workload changed an unrelated complete document, revision or original");
    }
}
void activatedRowReady(Context &c, const QString &asset, const QString &path, int count) {
    c.driver->wait(
        [&] {
            if (c.library.loading() || c.library.count() != count)
                return false;
            const auto row = c.library.index(0);
            const auto rowAsset = c.library.data(row, LibraryModel::AssetID).toString();
            const auto rowURL = c.library.data(row, LibraryModel::Thumbnail).toString();
            const auto rowPath = c.library.data(row, LibraryModel::Path).toUrl().toLocalFile();
            return rowAsset == asset && !rowURL.isEmpty() &&
                   rowURL.startsWith("image://thumbnail/" + asset + "?generation=") &&
                   rowPath == path;
        },
        "Activated photo's reloaded row metadata/thumbnail identity is ready");
    c.driver->frame();
}
QQuickItem *showInitialGrid(Context &c, const Rows &rows, CacheReadGate *gate = nullptr) {
    auto &d = *c.driver;
    d.type("library.search", "");
    d.wait(
        [&] { return !c.library.loading() && c.library.count() == static_cast<int>(rows.size()); },
        "owned thumbnail grid filter cleared");
    if (gate)
        d.wait([&] { return gate->connectToRealReader(); },
               "production provider actually opened the private FIFO reader");
    auto *card = d.item(c.card(0));
    require(card->property("assetID").toString() == QString::fromStdString(rows.front().id),
            "Initial actual card does not own the independently enumerated first asset");
    if (gate)
        require(gate->holdingRealReader() && imageFor(card)->property("status").toInt() == 2 &&
                    imageFor(card)->property("source").toUrl().toString() ==
                        card->property("thumbnailURL").toString(),
                "The real card request is not genuinely in flight before the controlled input");
    return card;
}
void moveGrid(Context &c, double fraction, int steps = 2) {
    auto &d = *c.driver;
    auto *bar = d.object("library.scrollBar");
    const double size = bar->property("size").toDouble();
    const double position = bar->property("position").toDouble();
    require(size > 0 && size < .5, "Thumbnail scroll fixture has no substantial scroll range");
    d.drag("library.scrollBar", {.5, position + size / 2}, {.5, fraction * (1 - size) + size / 2},
           steps);
    d.wait([&] { return !d.item("library.grid")->property("moving").toBool(); },
           "real thumbnail grid scroll settled");
    d.frame();
}
bool offscreen(Context &c, const QPointer<QQuickItem> &card) {
    auto *grid = c.driver->item("library.grid");
    return !card || !card->isVisible() ||
           !card->mapRectToItem(grid, card->boundingRect()).intersects(grid->boundingRect());
}
std::vector<QPointer<QQuickItem>> visibleCards(Context &c) {
    auto *grid = c.driver->item("library.grid");
    std::vector<QPointer<QQuickItem>> cards;
    std::function<void(QQuickItem *)> visit = [&](QQuickItem *parent) {
        for (auto *item : parent->childItems()) {
            if (item->objectName().startsWith("library.card.")) {
                if (item->isVisible() && item->mapRectToItem(grid, item->boundingRect())
                                             .intersects(grid->boundingRect()))
                    cards.push_back(item);
            } else
                visit(item);
        }
    };
    visit(grid);
    return cards;
}
void visibleOracles(Context &c, const Rows &rows) {
    auto &d = *c.driver;
    d.wait(
        [&] {
            const auto cards = visibleCards(c);
            return !cards.empty() && std::all_of(cards.begin(), cards.end(), [](const auto &card) {
                return card && ready(card.data());
            });
        },
        "all current visible card requests completed after the held reader released", 60000);
    QSet<QString> identities;
    int readbacks = 0;
    const auto currentCards = visibleCards(c);
    for (const auto &card : currentCards) {
        require(card, "A visible card was destroyed during an earlier asynchronous readback");
        const int row = card->property("index").toInt();
        const auto asset = card->property("assetID").toString();
        require(
            row >= 0 && row < static_cast<int>(rows.size()) &&
                asset == QString::fromStdString(rows[static_cast<std::size_t>(row)].id) &&
                c.library.data(c.library.index(row), LibraryModel::AssetID).toString() == asset &&
                card->property("thumbnailURL")
                    .toString()
                    .startsWith("image://thumbnail/" + asset + "?generation=") &&
                !identities.contains(asset),
            "Late thumbnail completion crossed actual delegate/model/independent asset identities");
        identities.insert(asset);
        cardPixels(c, card.data(), snapshot(c, asset));
        ++readbacks;
    }
    require(readbacks > 0 && readbacks == static_cast<int>(currentCards.size()) &&
                !c.library.loading() && c.library.error().isEmpty(),
            "Visible thumbnail fixture lacks any complete asset pixel readback");
}
void editExposure(Context &c, const luma::AssetSnapshot &old, CacheReadGate &gate) {
    auto &d = *c.driver;
    require(gate.holdingRealReader() && c.editor.photoID() == QString::fromStdString(old.asset.id),
            "Live edit does not overlap the held asset's actual thumbnail request");
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
    const auto edited = c.editor.currentSettings();
    auto expected = old.asset.edits;
    expected.exposure = edited.exposure;
    require(edited != old.asset.edits && edited == expected,
            "Real Develop gesture did not isolate Exposure");
    c.saved();
    const auto saved = snapshot(c, QString::fromStdString(old.asset.id));
    require(gate.holdingRealReader() && saved.asset.edits == edited &&
                saved.revision > old.revision &&
                meanRGB(samples(QImage::fromData(thumbnailJPEG(old), "JPEG")),
                        samples(QImage::fromData(thumbnailJPEG(saved), "JPEG"))) > 8,
            "Saved new thumbnail document is absent, not distinct, or missed the real in-flight "
            "request");
}
void invalidation(Context &c) {
    auto &d = *c.driver;
    const auto rows = importHidden(c, 200);
    const auto baseline = freeze(c, rows);
    const auto asset = QString::fromStdString(rows.front().id);
    const auto old = snapshot(c, asset);
    CacheReadGate gate(c, cachePath(c, old), thumbnailJPEG(old));
    gate.arm();
    const QPointer<QQuickItem> first(showInitialGrid(c, rows, &gate));
    const auto oldURL = first->property("thumbnailURL").toString();
    d.doubleClick(first->objectName());
    c.loaded();
    c.saved();
    require(c.editor.photoID() == asset && gate.holdingRealReader(),
            "Opening Develop lost the held thumbnail boundary");
    // photoLoaded -> LibraryModel::photoOpened -> reload(false) resets the model.
    // Count readiness is not page/role readiness: wait for the exact reloaded row
    // BEFORE observing invalidation, so a legitimate activation reset is not part
    // of the edit-under-test and empty asynchronous metadata cannot be its oracle.
    activatedRowReady(c, asset, QString::fromStdString(old.asset.path.string()),
                      static_cast<int>(rows.size()));
    const QPointer<QQuickItem> activated(d.item(c.card(0)));
    require(activated->property("assetID").toString() == asset,
            "The current activated card belongs to a different asset");
    {
        d.click("main.libraryButton");
        moveGrid(c, .7, 2);
        require(offscreen(c, activated) && gate.holdingRealReader() &&
                    d.item("library.grid")->property("contentY").toDouble() > 1000,
                "Original card did not leave before completion");
        d.click("main.developButton");
    }
    QSignalSpy reset(&c.library, &QAbstractItemModel::modelReset);
    QStringList invalidated;
    QObject lifetime;
    QObject::connect(
        &c.library, &QAbstractItemModel::dataChanged, &lifetime,
        [&](const QModelIndex &firstRow, const QModelIndex &lastRow, const QList<int> &roles) {
            // Count explicit thumbnail invalidation, not generic role refresh
            // emitted by an unrelated lazy metadata page completion. modelReset
            // remains observed independently and is never discarded here.
            if (roles.contains(LibraryModel::Thumbnail))
                for (int row = firstRow.row(); row <= lastRow.row(); ++row)
                    invalidated.append(
                        c.library.data(c.library.index(row), LibraryModel::AssetID).toString());
        });
    editExposure(c, old, gate);
    const auto saved = snapshot(c, asset);
    const auto newURL = c.library.data(c.library.index(0), LibraryModel::Thumbnail).toString();
    require(
        !newURL.isEmpty() && newURL.startsWith("image://thumbnail/" + asset + "?generation=") &&
            newURL != oldURL && invalidated == QStringList{asset} && reset.isEmpty() &&
            (offscreen(c, activated) &&
             d.item("library.grid")->property("contentY").toDouble() > 1000),
        "Actual saved invalidation missed its sole asset, reset the model or occurred onscreen");
    d.click("main.libraryButton");
    moveGrid(c, 0, 2);
    const QPointer<QQuickItem> current(d.item(c.card(0)));
    require(current->property("assetID").toString() == asset &&
                current->property("thumbnailURL").toString() == newURL &&
                imageFor(current)->property("source").toUrl().toString() == newURL &&
                imageFor(current)->property("status").toInt() == 2 &&
                current->property("retryCount").toInt() == 0 && gate.holdingRealReader(),
            "Returned real card did not bind NEW generation before the OLD worker could finish");
    d.log({{"action", "thumbnail_invalidation_during_genuine_cache_read"},
           {"asset", asset},
           {"old_source", oldURL},
           {"new_source", newURL},
           {"offscreen_at_invalidation", true},
           {"old_reader_connected", true},
           {"old_payload_bytes_written", 0}});
    gate.release();
    visibleOracles(c, rows);
    cardPixels(c, current, saved);
    const auto after = c.metrics.json()["rss_samples"].toInteger();
    d.wait([&] { return c.metrics.json()["rss_samples"].toInteger() >= after + 2; },
           "additional GUI heartbeat samples while the settled thumbnail remains current");
    require(current && ready(current) && current->property("thumbnailURL").toString() == newURL &&
                c.editor.currentSettings() == saved.asset.edits &&
                snapshot(c, asset).asset.edits == saved.asset.edits &&
                c.library.selectedCount() == 1 && reset.isEmpty(),
            "Late old thumbnail crossed saved editor/card state, model identity or selection");
    cardPixels(c, current, saved);
    invariant(c, baseline, saved);
    writeJson(c.root + "/measurements.json",
              {{"scenario", "UX-LIB-018"},
               {"protected_assets", static_cast<qint64>(baseline.size())},
               {"old_source", oldURL},
               {"new_source", newURL},
               {"old_revision", static_cast<qint64>(old.revision)},
               {"saved_revision", static_cast<qint64>(saved.revision)},
               {"targeted_invalidations", invalidated.size()},
               {"model_resets", reset.count()},
               {"actual_old_cache_reader_held", true},
               {"old_after_new_callback_order_claimed", false},
               {"scene_mean_rgb_tolerance", 4},
               {"cache_pixels_tolerance", 0}});
}
void boundedRetry(Context &c) {
    auto &d = *c.driver;
    const auto rows = importHidden(c, 64);
    const auto baseline = freeze(c, rows);
    const auto asset = QString::fromStdString(rows.front().id);
    const auto original = QString::fromStdString(rows.front().path.string());
    const auto originalHash = hash(original);
    MissingOriginal missing(original);
    const QPointer<QQuickItem> card(showInitialGrid(c, rows));
    const QPointer<QQuickItem> image(imageFor(card));
    auto *timer = card->findChild<QObject *>("thumbnailRetryTimer");
    require(timer && timer->property("interval").toInt() == 1500,
            "Bounded retry fixture does not use the actual 1500 ms production timer");
    QSignalSpy retries(timer, SIGNAL(triggered()));
    require(retries.isValid(), "Cannot observe the actual thumbnail Timer triggered signal");
    const auto baseURL = card->property("thumbnailURL").toString();
    auto failed = [&](int retry) {
        const auto expected = baseURL + (retry ? "&retry=" + QString::number(retry) : QString{});
        return card && image && card->property("assetID").toString() == asset &&
               card->property("retryCount").toInt() == retry &&
               image->property("source").toUrl().toString() == expected &&
               image->property("status").toInt() == 3;
    };
    d.wait([&] { return failed(0); },
           "real missing-original thumbnail request failed before retries");
    require(retries.isEmpty(), "The first real failure was missed before automatic retry");
    auto responsive = [&] {
        require(card && image, "Retry fixture destroyed its original card");
        d.click(c.card(1));
        require(c.library.selectionIDs() == std::vector<std::string>{rows[1].id},
                "Grid selection stopped responding to real input during thumbnail error");
        auto *grid = d.item("library.grid");
        const double start = grid->property("contentY").toDouble();
        const double range = grid->property("contentHeight").toDouble() - grid->height();
        require(range > 100 && start <= 1,
                "Bounded retry response needs an actual scrollable initial grid");
        // A small real thumb drag keeps this same error delegate alive while
        // proving scroll/input responsiveness. OS wheel delivery is a separate
        // boundary; the failed coarse-wheel probe remains in raw evidence.
        moveGrid(c, 12.0 / range, 2);
        require(grid->property("contentY").toDouble() > start + 1,
                "Real grid thumb did not move during thumbnail failure");
        moveGrid(c, 0, 2);
        require(grid->property("contentY").toDouble() <= start + 1,
                "Real grid thumb did not return during thumbnail failure");
        require(card && image && card->property("assetID").toString() == asset,
                "Responsive input evicted/recreated the card and invalidated the bounded retry "
                "observation");
    };
    responsive();
    d.wait([&] { return failed(1) && retries.count() == 1; },
           "actual first automatic retry also failed");
    d.wait([&] { return failed(2) && retries.count() == 2; },
           "actual second automatic retry also failed");
    require(!timer->property("running").toBool(),
            "Actual retry timer stayed armed above its two-retry cap");
    const auto sample = c.metrics.json()["rss_samples"].toInteger();
    QElapsedTimer quiet;
    quiet.start();
    responsive();
    const int noRetryWindow = timer->property("interval").toInt() * 2 + 250;
    d.wait([&] { return quiet.elapsed() >= noRetryWindow; },
           "more than two additional production retry intervals passed");
    require(failed(2) && retries.count() == 2 && !timer->property("running").toBool() &&
                c.metrics.json()["rss_samples"].toInteger() >= sample + 3 && !c.library.working() &&
                !c.library.loading() && c.library.error().isEmpty(),
            "Missing-thumbnail retries were unbounded or stalled the actual grid/event loop");
    missing.restore();
    require(hash(original) == originalHash, "Restoring the owned original changed its bytes");
    d.doubleClick(card->objectName());
    c.loaded();
    activatedRowReady(c, asset, original, static_cast<int>(rows.size()));
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
    c.saved();
    const auto saved = snapshot(c, asset);
    const auto recoveredURL =
        c.library.data(c.library.index(0), LibraryModel::Thumbnail).toString();
    require(
        saved.asset.edits == c.editor.currentSettings() && !recoveredURL.isEmpty() &&
            recoveredURL.startsWith("image://thumbnail/" + asset + "?generation=") &&
            recoveredURL != baseURL,
        "Successful saved edit did not provide a real recovery invalidation after bounded errors");
    d.click("main.libraryButton");
    const QPointer<QQuickItem> recovered(d.item(c.card(0)));
    d.wait([&] { return !recovered || ready(recovered); },
           "new saved generation resets retry and loads the restored original");
    require(recovered && recovered->property("assetID").toString() == asset &&
                recovered->property("retryCount").toInt() == 0 && hash(original) == originalHash,
            "Retry bound detached the real model source binding or damaged private source bytes");
    cardPixels(c, recovered, saved);
    invariant(c, baseline, saved);
    writeJson(c.root + "/measurements.json",
              {{"scenario", "UX-LIB-006"},
               {"protected_assets", static_cast<qint64>(baseline.size())},
               {"production_retry_interval_ms", 1500},
               {"observed_timer_triggers", retries.count()},
               {"quiet_window_ms", noRetryWindow},
               {"quiet_window_elapsed_ms", quiet.elapsed()},
               {"same_error_delegate_preserved_through_retry_cap", true},
               {"scroll_input", "actual bounded ScrollBar thumb drag"},
               {"recovered_retry_count", recovered->property("retryCount").toInt()},
               {"scene_mean_rgb_tolerance", 4},
               {"cache_pixels_tolerance", 0}});
}
struct CacheCapacityFixture {
    std::filesystem::path older, newer;
    explicit CacheCapacityFixture(const std::filesystem::path &directory)
        : older(directory / "owned-older.jpg"), newer(directory / "owned-newer.jpg") {}
    ~CacheCapacityFixture() {
        std::error_code ignored;
        std::filesystem::remove(older, ignored);
        std::filesystem::remove(newer, ignored);
    }
    void seed() {
        const auto now = std::filesystem::file_time_type::clock::now();
        int hours = 48;
        for (const auto &path : {older, newer}) {
            require(!std::filesystem::exists(path), "Owned cache capacity fixture already exists");
            QFile file(QString::fromStdString(path.string()));
            require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                        file.seek(256LL * 1024 * 1024 - 1) && file.write("\0", 1) == 1,
                    "Cannot create sparse cache capacity fixture");
            file.close();
            struct stat info{};
            require(::stat(path.c_str(), &info) == 0 && info.st_size == 256LL * 1024 * 1024 &&
                        info.st_blocks * 512 < 1024 * 1024,
                    "Cache capacity fixture must be sparse, not a physical 256 MiB write");
            std::filesystem::last_write_time(path, now - std::chrono::hours(hours));
            hours -= 24;
        }
    }
};
void boundedThumbnailCache(Context &c) {
    auto &d = *c.driver;
    const auto rows = importHidden(c, 2);
    const auto baseline = freeze(c, rows);
    const auto &saved = baseline.front();
    const auto cached = cachePath(c, saved);
    const auto directory = std::filesystem::path(QFileInfo(cached).absolutePath().toStdString());
    std::filesystem::create_directories(directory);
    require(std::filesystem::canonical(directory).string().starts_with(
                std::filesystem::canonical(c.root.toStdString()).string() + '/'),
            "Cache capacity fixture escaped its owned root");
    const auto marker = QString::fromStdString((directory / "preserved.txt").string());
    {
        QFile file(marker);
        require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write("preserve") == 8,
                "Cannot create unrelated cache marker");
    }
    constexpr std::uintmax_t budget = 512ULL * 1024 * 1024;
    auto logicalBytes = [&] {
        std::uintmax_t total = 0;
        for (const auto &entry : std::filesystem::directory_iterator(directory))
            if (entry.is_regular_file() && entry.path().extension() == ".jpg")
                total += entry.file_size();
        return total;
    };
    auto showOne = [&](const luma::LibraryItem &row) {
        d.type("library.search", QString::fromStdString(row.filename));
        d.wait([&] { return !c.library.loading() && c.library.count() == 1; },
               "Actual filename filter exposes one cache fixture asset");
        auto *result = d.item(c.card(0));
        require(result->property("assetID").toString().toStdString() == row.id,
                "Cache fixture filter exposed another asset");
        return result;
    };
    auto bounded = [&](const CacheCapacityFixture &fixture, const char *phase,
                       const QString &currentCache) {
        const auto size = logicalBytes();
        d.log({{"action", "thumbnail_disk_capacity"},
               {"phase", phase},
               {"budget_bytes", static_cast<qint64>(budget)},
               {"logical_bytes", static_cast<qint64>(size)},
               {"oldest_removed", !std::filesystem::exists(fixture.older)},
               {"newer_preserved", std::filesystem::exists(fixture.newer)}});
        require(size <= budget && !std::filesystem::exists(fixture.older) &&
                    std::filesystem::exists(fixture.newer) && QFileInfo(currentCache).isFile() &&
                    bytes(marker) == "preserve",
                QStringLiteral("Thumbnail disk cache exceeds its bound or evicts wrong data: ") +
                    phase);
    };
    QPointer<QQuickItem> card;
    {
        CacheCapacityFixture capacity(directory);
        capacity.seed();
        require(logicalBytes() == budget, "Cold capacity fixture must start exactly at its cap");
        card = showOne(rows.front());
        d.wait([&] { return card && ready(card); }, "Cold thumbnail renders after cache eviction");
        bounded(capacity, "cold-render", cached);
        cardPixels(c, card, saved);
        invariant(c, baseline, saved);
    }
    const auto &warm = baseline.back();
    const auto warmCache = cachePath(c, warm);
    // Asset B has never appeared in QML, so its first Image cannot reuse Qt's
    // in-memory pixmap. Seed a real persisted JPEG with the independent oracle.
    {
        const auto jpeg = thumbnailJPEG(warm);
        QSaveFile file(warmCache);
        require(file.open(QIODevice::WriteOnly) && file.write(jpeg) == jpeg.size() && file.commit(),
                "Cannot prepare known-valid persisted thumbnail cache input");
    }
    const auto cachedHash = hash(warmCache);
    d.type("library.search", "no-such-cache-capacity-fixture");
    d.wait([&] { return !c.library.loading() && c.library.count() == 0 && !card; },
           "Actual search destroys the original thumbnail delegate");
    // Recreate the genuine engine provider at an idle boundary to exercise a
    // persisted cache on reopening. This is not a full process-restart test.
    c.engine.removeImageProvider("thumbnail");
    auto *reopened = new ThumbnailProvider(c.catalogFile.toStdString());
    c.engine.addImageProvider("thumbnail", reopened);
    thumbnail_trace::Session responses(reopened->uxTraceSlot());
    d.log({{"action", "actual_thumbnail_provider_recreated"},
           {"scope", "persisted disk cache; not whole application restart"}});
    {
        CacheCapacityFixture capacity(directory);
        capacity.seed();
        require(logicalBytes() > budget, "Warm cache fixture must start above its cap");
        // Without the original, a successful real provider response proves disk
        // cache reuse. No fake provider or direct cache-trim invocation is used.
        MissingOriginal missing(QString::fromStdString(warm.asset.path.string()));
        card = showOne(rows.back());
        d.wait([&] { return card && ready(card); }, "Warm cached thumbnail loads without original");
        const auto observed = responses.snapshot();
        require(!observed.diagnosticFailure && observed.requests.size() == 1 &&
                    std::any_of(observed.events.begin(), observed.events.end(),
                                [](const auto &e) {
                                    return e.kind == thumbnail_trace::Kind::WorkerResultReady &&
                                           e.successfulPixels;
                                }) &&
                    std::any_of(observed.events.begin(), observed.events.end(),
                                [](const auto &e) {
                                    return e.kind == thumbnail_trace::Kind::TextureFactoryCalled;
                                }),
                "Warm cache check did not observe an actual successful provider response");
        bounded(capacity, "warm-cache-hit", warmCache);
        require(hash(warmCache) == cachedHash, "Cache reuse changed the saved JPEG bytes");
        missing.restore();
        cardPixels(c, card, warm);
        invariant(c, baseline, warm);
    }
    require(logicalBytes() == static_cast<std::uintmax_t>(QFileInfo(cached).size() +
                                                          QFileInfo(warmCache).size()) &&
                c.library.error().isEmpty() && !c.editor.hasPhoto() && c.warnings.isEmpty(),
            "Cache capacity check left sparse ballast, changed Develop state or raised warnings");
    luma::Catalog(c.catalogFile.toStdString()).verify();
}
} // namespace

void thumbnailCardPixels(Context &c, QQuickItem *card, const luma::AssetSnapshot &saved) {
    cardPixels(c, card, saved);
}

bool thumbnailRemainingScenario(Context &c, const QString &id) {
    if (id == "UX-LIB-018")
        invalidation(c);
    else if (id == "UX-LIB-006")
        boundedRetry(c);
    else if (id == "UX-P06-LRU-002")
        boundedThumbnailCache(c);
    else
        return false; // 004/005/RACE002 candidates and 007 gap must not become inferred PASS.
    return true;
}
} // namespace ux
