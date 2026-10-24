#include "apps/desktop/BatchController.h"
#include "apps/desktop/EditorController.h"
#include "apps/desktop/LibraryModel.h"
#include "apps/desktop/PresetController.h"
#include "apps/desktop/ThumbnailProvider.h"
#include "core/image/Bayer.h"
#include "core/raw/RawDecoder.h"
#include "platform/macos/FileHasher.h"
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickTextureFactory>
#include <QThread>
#include <atomic>
#include <iostream>
#include <mutex>
#include <source_location>
#include <sqlite3.h>
#include <thread>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void wait(const std::function<bool()> &ready,
          std::source_location location = std::source_location::current()) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 30000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    if (!ready())
        throw std::runtime_error("Thumbnail wait timed out at line " +
                                 std::to_string(location.line()));
}
void sql(const std::filesystem::path &path, const char *text) {
    sqlite3 *db = nullptr;
    require(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "Cannot open fixture database.");
    const int result = sqlite3_exec(db, text, nullptr, nullptr, nullptr);
    sqlite3_close(db);
    require(result == SQLITE_OK, "Cannot update fixture database.");
}
QImage request(ThumbnailProvider &provider, const QString &id, QSize size = {384, 384}) {
    const std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(id, size));
    bool finished = false;
    QObject::connect(response.get(), &QQuickImageResponse::finished, QCoreApplication::instance(),
                     [&] { finished = true; });
    wait([&] { return finished; });
    if (!response->errorString().isEmpty())
        throw std::runtime_error(response->errorString().toStdString());
    const std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
    require(texture != nullptr, "No thumbnail texture was returned.");
    return texture->image();
}
std::size_t cachedCount(const std::filesystem::path &catalog) {
    const auto folder =
        catalog.parent_path() / (catalog.filename().string() + ".luma-cache") / "thumbnails";
    std::size_t count = 0;
    if (std::filesystem::exists(folder))
        for (const auto &entry : std::filesystem::directory_iterator(folder))
            count += entry.path().extension() == ".jpg";
    return count;
}
void compare(const QImage &thumbnail, const luma::DisplayImage &rendered) {
    require(thumbnail.width() == rendered.width && thumbnail.height() == rendered.height,
            "Thumbnail dimensions differ from the shared edited geometry.");
    double error = 0;
    for (int y = 0; y < thumbnail.height(); ++y)
        for (int x = 0; x < thumbnail.width(); ++x) {
            const auto offset = (static_cast<std::size_t>(y) * thumbnail.width() + x) * 4;
            const auto color = thumbnail.pixelColor(x, y);
            error += std::abs(color.red() - rendered.rgba[offset]) +
                     std::abs(color.green() - rendered.rgba[offset + 1]) +
                     std::abs(color.blue() - rendered.rgba[offset + 2]);
        }
    require(
        error / static_cast<double>(luma::pixelCount(thumbnail.width(), thumbnail.height()) * 3) <
            4,
        "JPEG thumbnail does not represent the shared saved-edit render.");
}
luma::EditDocument look(double exposure) {
    luma::EditDocument doc;
    doc.setParameters({{"exposure", exposure},
                       {"temperature", 5100},
                       {"tint", 12},
                       {"noise.luminance", 30},
                       {"noise.color", 55},
                       {"sharpen.amount", 70},
                       {"sharpen.radius", 1.5},
                       {"sharpen.masking", 25},
                       {"curve.lights", 8},
                       {"hsl.red.hue", 15},
                       {"hsl.blue.luminance", 12},
                       {"crop.x", 0.1},
                       {"crop.y", 0.15},
                       {"crop.width", 0.8},
                       {"crop.height", 0.7},
                       {"crop.quarterTurns", 1},
                       {"transform.rotate", 7},
                       {"transform.horizontal", 18},
                       {"transform.aspect", 10}});
    luma::LocalMask mask;
    mask.id = "01234567-89ab-cdef-0123-456789abcdef";
    mask.name = "Thumbnail gradient";
    mask.setParameter("exposure", 0.7);
    luma::MaskComponent range;
    range.id = "11234567-89ab-cdef-0123-456789abcdef";
    range.operation = luma::MaskOperation::intersect;
    range.geometry.shape = luma::MaskShape::luminance;
    range.geometry.rangeLow = .1;
    range.geometry.rangeHigh = .9;
    auto colors = range;
    colors.id = "21234567-89ab-cdef-0123-456789abcdef";
    colors.operation = luma::MaskOperation::subtract;
    colors.geometry.shape = luma::MaskShape::color;
    colors.geometry.colorTolerance = .1;
    auto brush = range;
    brush.id = "31234567-89ab-cdef-0123-456789abcdef";
    brush.operation = luma::MaskOperation::add;
    brush.geometry.shape = luma::MaskShape::brush;
    brush.geometry.strokes = {{false, .15, 75, {{.1, .2}, {.9, .7}}}, {true, .04, 50, {{.5, .5}}}};
    mask.components = {range, colors, brush};
    doc.localMasks = {mask};
    doc.customWhiteBalance = true;
    doc.setEnabled(luma::Correction::lens, true);
    doc.curves[0] = {{{0, 0.02}, {0.4, 0.46}, {1, 0.97}}};
    return doc;
}
void snapshots(const std::filesystem::path &catalog, const std::string &id) {
    auto first = look(0.1), second = look(0.8);
    second.curves[0].points[1].y = 0.58;
    second.hsl[0].hue = -20;
    luma::Catalog(catalog).saveEdits(id, first);
    std::atomic<bool> done{false};
    std::exception_ptr failure;
    std::thread writer([&] {
        try {
            luma::Catalog db(catalog);
            for (int i = 0; i < 60; ++i)
                db.saveEdits(id, i % 2 ? first : second);
        } catch (...) {
            failure = std::current_exception();
        }
        done = true;
    });
    struct JoinThread {
        std::thread &thread;
        ~JoinThread() {
            if (thread.joinable())
                thread.join();
        }
    } join{writer};
    luma::Catalog reader(catalog);
    for (int i = 0; i < 120 || !done; ++i) {
        const auto snapshot = reader.assetSnapshot(id);
        require(snapshot && (snapshot->asset.edits == first || snapshot->asset.edits == second),
                "Thumbnail snapshot combined parent and child edits from different commits.");
    }
    writer.join();
    if (failure)
        std::rethrow_exception(failure);
    require(!reader.assetSnapshot("missing"), "Missing snapshot fabricated a photo.");
}
class ManualResponse final : public QQuickImageResponse {
  public:
    explicit ManualResponse(QColor color) : image_(40, 40, QImage::Format_RGB32) {
        image_.fill(color);
    }
    QQuickTextureFactory *textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(image_);
    }
    QString errorString() const override {
        return error_;
    }
    void complete(bool error = false) {
        // The test driver runs on the GUI thread; the response belongs to the image
        // reader. Queue completion after that thread has registered the response.
        require(QMetaObject::invokeMethod(
                    this,
                    [this, error] {
                        error_ = error ? "Synthetic thumbnail failure" : QString();
                        emit finished();
                    },
                    Qt::QueuedConnection),
                "Cannot queue the controlled thumbnail completion.");
    }

  private:
    QImage image_;
    QString error_;
};
class ManualProvider final : public QQuickAsyncImageProvider {
  public:
    std::size_t count() const {
        std::lock_guard lock(mutex_);
        return requests_.size();
    }
    QPointer<ManualResponse> response(std::size_t index) const {
        std::lock_guard lock(mutex_);
        return requests_.at(index);
    }
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &) override {
        auto *response = new ManualResponse(id.startsWith("new") ? Qt::red : Qt::blue);
        std::lock_guard lock(mutex_);
        requests_.push_back(response);
        return response;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<QPointer<ManualResponse>> requests_;
};
void sourceBindings(const QString &qmlPath) {
    QQmlEngine engine;
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            if (!error.description().contains("Synthetic thumbnail failure"))
                warnings.append(error.toString());
    });
    auto *provider = new ManualProvider;
    engine.addImageProvider("controlled", provider);
    QQmlComponent component(&engine);
    component.setData(
        ("import QtQuick\nimport QtQuick.Controls\nimport \"" +
         QUrl::fromLocalFile(qmlPath).toString() +
         "\"\nApplicationWindow { id: fixtureWindow; width: 200; height: 240; "
         "visible: true; property var observedThumbnail: null; "
         "property int errorTransitions: 0; property url lastErrorSource; "
         "Connections { target: fixtureWindow.observedThumbnail; "
         "function onStatusChanged() { if (fixtureWindow.observedThumbnail.status === Image.Error) "
         "{ fixtureWindow.lastErrorSource = fixtureWindow.observedThumbnail.source; "
         "fixtureWindow.errorTransitions++; } } } PhotoCard "
         "{objectName: \"card\"; anchors.fill: parent; index: 0; assetID: \"a\"; "
         "filename: \"test\"; thumbnailURL: \"image://controlled/old\"; rating: 0; "
         "flagValue: 0; colorLabel: \"\"; selected: false; activePhoto: false; sourceMissing: "
         "false} }")
            .toUtf8(),
        QUrl());
    wait([&] { return component.status() != QQmlComponent::Loading; });
    if (component.isError())
        throw std::runtime_error(component.errorString().toStdString());
    const std::unique_ptr<QObject> window(component.create());
    require(window != nullptr, "PhotoCard QML did not load.");
    auto *card = window->findChild<QObject *>("card");
    auto *image = window->findChild<QObject *>("photoThumbnail");
    auto *timer = window->findChild<QObject *>("thumbnailRetryTimer");
    auto *quick = qobject_cast<QQuickWindow *>(window.get());
    require(card && image && timer && quick, "PhotoCard test controls are missing.");
    require(timer->property("interval").toInt() == 1500,
            "Production thumbnail retry interval changed.");
    // This fixture triggers the retry itself to verify its source binding. Keep the
    // production timer from consuming Image.Error before a delayed test observer.
    require(timer->setProperty("interval", 60000), "Cannot isolate the manual retry fixture.");
    require(window->setProperty("observedThumbnail", QVariant::fromValue(image)),
            "Cannot observe the thumbnail error transition.");
    wait([&] { return provider->count() == 1; });
    card->setProperty("thumbnailURL", "image://controlled/new");
    wait([&] { return provider->count() == 2; });
    require(provider->response(1) != nullptr, "New image response disappeared.");
    provider->response(1)->complete();
    wait([&] { return image->property("status").toInt() == 1 && quick->isExposed(); });
    if (provider->response(0))
        provider->response(0)->complete();
    QCoreApplication::processEvents();
    const auto frame = quick->grabWindow();
    require(!frame.isNull() && frame.pixelColor(frame.width() / 2, frame.height() / 3).red() > 200,
            "Late original thumbnail replaced a newer edited source.");
    card->setProperty("thumbnailURL", "image://controlled/failure");
    wait([&] { return provider->count() == 3; });
    const int errorBaseline = window->property("errorTransitions").toInt();
    require(errorBaseline == 0, "An unrelated thumbnail failed before the error fixture.");
    provider->response(2)->complete(true);
    try {
        wait([&] { return image->property("status").toInt() == 3; });
    } catch (...) {
        std::cerr << "Thumbnail failure observer status=" << image->property("status").toInt()
                  << " transitions=" << window->property("errorTransitions").toInt()
                  << " source=" << image->property("source").toUrl().toString().toStdString()
                  << " requests=" << provider->count()
                  << " response_alive=" << static_cast<bool>(provider->response(2))
                  << " warnings=" << warnings.join('\n').toStdString() << '\n';
        throw;
    }
    require(window->property("errorTransitions").toInt() == errorBaseline + 1 &&
                window->property("lastErrorSource").toUrl().toString() ==
                    "image://controlled/failure",
            "The expected thumbnail Error transition/source was not observed.");
    require(image->property("status").toInt() == 3 && provider->count() == 3,
            "Manual retry fixture did not retain the observed thumbnail error.");
    require(QMetaObject::invokeMethod(timer, "triggered"), "Cannot trigger thumbnail retry.");
    wait([&] { return provider->count() == 4; });
    require(image->property("source").toUrl().toString().contains("retry=1"),
            "Retry did not change source.");
    card->setProperty("thumbnailURL", "image://controlled/new-after-edit?generation=2");
    wait([&] { return provider->count() == 5; });
    require(card->property("retryCount").toInt() == 0 &&
                image->property("source").toUrl().toString() ==
                    "image://controlled/new-after-edit?generation=2",
            "Thumbnail retry detached the model URL binding.");
    provider->response(4)->complete();
    if (provider->response(3))
        provider->response(3)->complete();
    wait([&] { return image->property("status").toInt() == 1; });
    if (!warnings.isEmpty())
        throw std::runtime_error(warnings.join('\n').toStdString());
}
} // namespace
void thumbnailTests(const QString &source, const QString &root, const QString &qmlPath) {
    const auto catalog = std::filesystem::path((root + "/thumbnail.sqlite").toStdString());
    const auto path = std::filesystem::path(source.toStdString());
    const auto checksum = luma::macos::sha256File(path);
    const auto decoded = luma::RawDecoder().decode(path);
    luma::Catalog db(catalog);
    const auto asset = db.addAndSelect(path, checksum, decoded.metadata);
    const auto copyPath = std::filesystem::path((root + "/duplicate.DNG").toStdString());
    std::filesystem::copy_file(path, copyPath);
    const auto duplicate = db.addAndSelect(copyPath, checksum, decoded.metadata);
    auto edits = look(0.25), other = look(-0.8);
    db.saveEdits(asset.id, edits);
    db.saveEdits(duplicate.id, other);
    ThumbnailProvider provider(catalog);
    const auto id = QString::fromStdString(asset.id);
    const auto first = request(provider, id + "?generation=1");
    const auto small = luma::standardPreview(decoded.image, 384);
    const double scale = std::min(static_cast<double>(small.width) / decoded.image.width,
                                  static_cast<double>(small.height) / decoded.image.height);
    compare(first,
            luma::renderCPU(small, edits, decoded.metadata.orientation, {}, {}, decoded.calibration,
                            luma::DetailDiagnostic::none, scale, decoded.metadata));
    const auto cached = request(provider, id);
    require(cached == request(provider, id) && cachedCount(catalog) == 1,
            "Repeated thumbnail request did not reuse its cache.");
    const auto different = request(provider, QString::fromStdString(duplicate.id));
    require(different != cached && cachedCount(catalog) == 2,
            "Identical source checksums with different edits aliased the cache.");
    db.saveEdits(asset.id, other);
    sql(catalog, "UPDATE edit_documents SET revision=1");
    require(request(provider, id) == different,
            "Reused revision ignored the canonical edit fingerprint.");
    const auto cache =
        catalog.parent_path() / (catalog.filename().string() + ".luma-cache") / "thumbnails";
    for (const auto &entry : std::filesystem::directory_iterator(cache)) {
        QFile corrupt(QString::fromStdString(entry.path().string()));
        require(corrupt.open(QIODevice::WriteOnly) && corrupt.write("invalid") == 7,
                "Cannot corrupt owned cache fixture.");
    }
    compare(request(provider, id),
            luma::renderCPU(small, other, decoded.metadata.orientation, {}, {}, decoded.calibration,
                            luma::DetailDiagnostic::none, scale, decoded.metadata));
    ThumbnailProvider restarted(catalog);
    require(request(restarted, id) == request(provider, id),
            "Restart changed saved thumbnail pixels.");
    require(request(provider, id, {2000, 2000}).width() <= 384,
            "Thumbnail request exceeded bounded output size.");
    snapshots(catalog, asset.id);
    sourceBindings(qmlPath);
    PreviewProvider preview;
    EditorController editor(catalog, &preview);
    LibraryModel library(catalog);
    PresetController presets(root + "/thumb-presets.json", editor);
    BatchController batch(catalog, editor, library, presets);
    QObject::connect(&editor, &EditorController::editsSaved, &library,
                     &LibraryModel::refreshThumbnails);
    editor.openFile(QUrl::fromLocalFile(source));
    wait([&] {
        return editor.hasPhoto() && !editor.busy() && !library.working() && library.count() == 2;
    });
    library.selectAll();
    wait([&] { return library.selectedCount() == 2 && !library.selecting(); });
    const auto thumbnailURL = [&] {
        QString url;
        // Folder notifications can unload the asynchronous metadata page. An
        // empty placeholder is neither a saved thumbnail nor invalidation.
        wait([&] {
            for (int row = 0; row < library.count(); ++row)
                if (library.data(library.index(row), LibraryModel::AssetID).toString() == id) {
                    url = library.data(library.index(row), LibraryModel::Thumbnail).toString();
                    break;
                }
            return !url.isEmpty();
        });
        return url;
    };
    const auto initialURL = thumbnailURL();
    QStringList invalidatedIDs;
    QObject::connect(
        &library, &QAbstractItemModel::dataChanged,
        [&](const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
            if (roles == QList<int>{LibraryModel::Thumbnail})
                for (int row = first.row(); row <= last.row(); ++row)
                    invalidatedIDs.append(
                        library.data(library.index(row), LibraryModel::AssetID).toString());
        });
    editor.setExposure(-0.4);
    wait([&] { return !editor.saving(); });
    require(thumbnailURL() != initialURL, "Autosave did not invalidate the active thumbnail.");
    require(library.selectedCount() == 2 && library.count() == 2,
            "Autosave thumbnail update reset the library selection/count.");
    require(invalidatedIDs == QStringList{id},
            "Autosave notified unrelated rows or missed the saved asset.");
    const auto afterSave = thumbnailURL();
    sql(catalog, "CREATE TRIGGER thumb_fail_save BEFORE UPDATE ON edit_documents BEGIN SELECT "
                 "RAISE(ABORT,'Owned test save failure'); END");
    editor.setExposure(0.2);
    wait([&] { return !editor.error().isEmpty(); });
    require(thumbnailURL() == afterSave, "Failed autosave invalidated saved thumbnails.");
    sql(catalog, "DROP TRIGGER thumb_fail_save");
    editor.retrySave();
    wait([&] { return !editor.saving(); });
    const auto afterRetry = thumbnailURL();
    require(afterRetry != afterSave && editor.error().isEmpty(),
            "Successful autosave retry did not invalidate thumbnails.");
    presets.preview("builtin:warm-film");
    require(thumbnailURL() == afterRetry, "Hover invalidated saved thumbnails.");
    presets.clearPreview();
    batch.applyPreset("builtin:landscape");
    wait([&] { return !batch.working(); });
    const auto afterBatch = thumbnailURL();
    require(batch.error().isEmpty() && afterBatch != afterRetry && library.selectedCount() == 2,
            "Batch commit did not invalidate thumbnails or lost selection.");
    batch.undo();
    wait([&] { return !batch.working(); });
    const auto afterUndo = thumbnailURL();
    require(afterUndo != afterBatch && batch.error().isEmpty(),
            "Batch Undo did not invalidate thumbnails.");
    batch.redo();
    wait([&] { return !batch.working(); });
    const auto afterRedo = thumbnailURL();
    require(batch.error().isEmpty() && afterRedo != afterUndo,
            "Batch Redo did not invalidate thumbnails.");
    batch.applyPreset("builtin:landscape");
    wait([&] { return !batch.working(); });
    require(batch.error().isEmpty() && thumbnailURL() == afterRedo,
            "No-op batch invalidated unchanged thumbnails.");
    require(luma::macos::sha256File(path) == checksum &&
                luma::macos::sha256File(copyPath) == checksum,
            "Thumbnail generation changed original RAW bytes.");
    std::vector<std::unique_ptr<QQuickImageResponse>> cancelled;
    int completed = 0;
    {
        ThumbnailProvider closing(catalog);
        for (int i = 0; i < 8; ++i) {
            auto *response = closing.requestImageResponse(id, {});
            cancelled.emplace_back(response);
            response->cancel();
            QObject::connect(response, &QQuickImageResponse::finished, QCoreApplication::instance(),
                             [&] { ++completed; });
        }
    }
    wait([&] { return completed == 8; });
    QCoreApplication::processEvents();
}
