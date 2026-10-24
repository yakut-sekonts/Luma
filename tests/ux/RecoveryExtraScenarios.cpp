#include "core/render/DevelopPipeline.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QRegularExpression>

namespace ux {
namespace {
QQuickItem *thumbnail(QQuickItem *parent) {
    if (parent->objectName() == "photoThumbnail")
        return parent;
    for (auto *child : parent->childItems())
        if (auto *found = thumbnail(child))
            return found;
    return nullptr;
}
QByteArray bytes(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read isolated recovery fixture");
    return file.readAll();
}
} // namespace
bool recoveryExtraScenario(Context &c, const QString &id) {
    if (id != "UX-REC-008")
        return false;
    auto &d = *c.driver;
    const auto source = c.root + "/removable.png";
    require(QFile::copy(c.fixture + "/Photo-0000.png", source),
            "Cannot create a removable isolated source");
    c.editor.openFile(QUrl::fromLocalFile(source));
    c.loaded();
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
    c.saved();
    const auto assetID = c.editor.photoID().toStdString();
    const auto saved = c.editor.currentSettings();
    const auto rendered = fixtureReference(c);
    const auto pixelsAgree = [&](const QImage &expected) {
        return previewPixelsAgree(
            c.provider->requestImage({}, nullptr, {}), expected,
            c.editor.metalPreview() &&
                luma::DevelopPipeline(c.editor.currentSettings()).gpuCompatible());
    };
    d.wait([&] { return pixelsAgree(rendered); }, "removable source has current preview");
    require(QFile::remove(source), "Cannot remove isolated source while Develop is open");
    d.log({{"action", "isolated_source_removed"}, {"source", source}});
    const auto durable = luma::Catalog(c.catalogFile.toStdString()).assetByID(assetID);
    require(durable && durable->edits == saved && c.editor.hasPhoto() &&
                c.editor.currentSettings() == saved && c.editor.sourcePath() == source,
            "Removing a source discarded committed edits or active metadata");
    const auto beforeContrastUrl = c.editor.previewUrl();
    c.reveal("develop.contrastSlider");
    d.drag("develop.contrastSlider", d.sliderThumb("develop.contrastSlider"), {.65, .5});
    const auto after = c.editor.currentSettings();
    auto expected = saved;
    expected.contrast = after.contrast;
    require(after != saved && after == expected, "Missing source prevented an in-memory edit");
    c.saved();
    const auto stored = luma::Catalog(c.catalogFile.toStdString()).assetByID(assetID);
    require(stored && stored->edits == after && c.editor.error().isEmpty(),
            "Missing source prevented durable catalog edits");
    const auto afterPixels = fixtureReference(c, after, c.fixture + "/Photo-0000.png");
    require(afterPixels != rendered, "Contrast fixture has no distinct visual effect");
    auto ready = [&](const QImage &pixels, const QUrl &previousUrl) {
        auto *preview = d.item("develop.preview");
        return c.editor.previewUrl() != previousUrl && preview->isVisible() &&
               preview->property("status").toInt() == 1 &&
               preview->property("source").toUrl() == c.editor.previewUrl() && pixelsAgree(pixels);
    };
    try {
        d.wait([&] { return ready(afterPixels, beforeContrastUrl); },
               "new Contrast pixels render from retained image after source removal");
    } catch (...) {
        recordPreviewDifference(c, afterPixels, "retained source Contrast preview");
        auto *preview = d.item("develop.preview");
        writeJson(c.root + "/retained-source-preview.json",
                  {{"previous_url", beforeContrastUrl},
                   {"current_url", c.editor.previewUrl()},
                   {"image_source", preview->property("source").toUrl().toString()},
                   {"image_status", preview->property("status").toInt()},
                   {"image_visible", preview->isVisible()},
                   {"editor_error", c.editor.error()}});
        throw;
    }
    const auto beforeUndoUrl = c.editor.previewUrl();
    d.click("main.undoButton");
    require(c.editor.currentSettings() == saved, "Missing source damaged Undo history");
    d.wait([&] { return ready(rendered, beforeUndoUrl); },
           "Undo still renders the retained decoded image after source removal");
    return true;
}
bool thumbnailRestartScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-REC-009")
        return false;
    auto &d = *c.driver;
    if (stage == "prepare")
        c.import(1);
    else
        require(stage == "verify" && c.library.count() == 1,
                "Thumbnail restart did not restore the isolated catalog");
    auto *image = thumbnail(d.item(c.card(0)));
    require(image, "Card's actual thumbnail Image is absent");
    d.wait([&] { return image->property("status").toInt() == 1; },
           "actual card thumbnail became Ready");
    const auto directory = c.catalogFile + ".luma-cache/thumbnails";
    const auto names = QDir(directory).entryList({"*.jpg"}, QDir::Files);
    require(names.size() == 1, "Single-asset thumbnail fixture has an ambiguous cache file");
    const auto path = directory + '/' + names.front();
    if (stage == "prepare") {
        const auto original = bytes(path);
        require(!QImageReader(path, "JPEG").read().isNull(), "Baseline thumbnail is invalid");
        writeJson(c.stateRoot + "/thumbnail-oracle.json",
                  {{"filename", names.front()},
                   {"sha256",
                    QString::fromLatin1(
                        QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex())}});
        d.quitShortcut();
        d.wait(
            [&] { return !c.window->isVisible() && !c.library.importing() && !c.editor.saving(); },
            "clean shutdown before cache corruption");
    } else {
        const auto expected =
            QJsonDocument::fromJson(bytes(c.stateRoot + "/thumbnail-oracle.json")).object();
        require(
            names.front() == expected["filename"].toString() &&
                !QImageReader(path, "JPEG").read().isNull() &&
                QString::fromLatin1(
                    QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256).toHex()) ==
                    expected["sha256"].toString(),
            "Restart did not regenerate the exact valid thumbnail after cache corruption");
        require(image->property("source").toUrl().toString().startsWith("image://thumbnail/"),
                "Repaired thumbnail did not come from the production provider");
        d.doubleClick(c.card(0));
        c.loaded();
        require(c.editor.error().isEmpty(), "Cache corruption prevented normal photo activation");
    }
    return true;
}
void prepareThumbnailCorruption(const QString &root, const QString &stateRoot) {
    // This runs in the new verify process BEFORE any Context/ThumbnailProvider exists,
    // after the prepare process has exited. No old worker can repair the fixture early.
    require(!stateRoot.isEmpty(), "Cache corruption requires isolated restart state");
    const auto oracle =
        QJsonDocument::fromJson(bytes(stateRoot + "/thumbnail-oracle.json")).object();
    const auto filename = oracle["filename"].toString();
    const QRegularExpression namePattern("^[0-9a-f]{64}-edited-v4\\.jpg$");
    require(namePattern.match(filename).hasMatch(), "Unsafe thumbnail fixture filename");
    const auto path = stateRoot + "/catalog.sqlite.luma-cache/thumbnails/" + filename;
    const auto beforeHash = QString::fromLatin1(
        QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256).toHex());
    require(beforeHash == oracle["sha256"].toString(),
            "Baseline thumbnail changed before corruption");
    QFile cache(path);
    const QByteArray corruption("Not a JPEG\n");
    require(cache.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                cache.write(corruption) == corruption.size(),
            "Cannot corrupt isolated thumbnail cache");
    cache.close();
    require(bytes(path) == corruption && QImageReader(path, "JPEG").read().isNull(),
            "Cache fixture is not corrupt before startup");
    writeJson(root + "/cache-corruption-before-context.json",
              {{"filename", filename},
               {"baseline_sha256", beforeHash},
               {"corrupt_sha256",
                QString::fromLatin1(
                    QCryptographicHash::hash(corruption, QCryptographicHash::Sha256).toHex())},
               {"before_context", true}});
}
} // namespace ux
