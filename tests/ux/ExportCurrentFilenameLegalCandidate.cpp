#include "apps/shared/PresetStore.h"
#include "tests/ux/QueuedCallGate.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QPointer>
#include <QQuickItemGrabResult>
#include <QSet>
#include <QSignalSpy>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <sqlite3.h>
#include <tuple>
#include <vector>

// Bounded fixture for the explicitly amended VIS003 contract.
// Production cap230 UTF-8 bytes and middle elision remain unchanged.
// Qualification is reported by the runner, never inferred from this source.
namespace ux {
namespace {
constexpr auto candidateID = "CANDIDATE-VIS003-LEGAL-CURRENT-FILENAME";
constexpr auto officialID = "UX-VIS-003";
constexpr int width = 64, height = 48, count = 4;
QString sourceStem(int variant) {
    require(variant >= 0 && variant < count, "VIS003 unknown source variant");
    if (variant == 0)
        return QStringLiteral("Short-control");
    if (variant == 1)
        return QString(230, QChar('a'));
    const auto multibyte = QStringLiteral("\u044f").repeated(115);
    return variant == 2 ? multibyte : multibyte + QChar('a');
}
QString sourceName(int variant) {
    return sourceStem(variant) + QStringLiteral(".png");
}
QColor sourcePixel(int x, int y, int variant) {
    const std::array<QColor, count> colors{QColor(40, 40, 48), QColor(190, 40, 190),
                                           QColor(40, 190, 190), QColor(190, 190, 48)};
    require(variant >= 0 && variant < count, "VIS003 unknown literal pixel variant");
    const auto color = colors[static_cast<std::size_t>(variant)];
    return QColor(color.red() + x % 13, color.green() + y % 17, color.blue() + (x + y) % 11);
}
QByteArray bytes(const QString &path) {
    QFile file(path);
    require(QFileInfo(path).isFile() && !QFileInfo(path).isSymLink() &&
                file.open(QIODevice::ReadOnly),
            "Cannot read an owned VIS003 regular file: " + path);
    return file.readAll();
}
QString digest(const QString &path) {
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256).toHex());
}
QJsonObject documentJSON(const luma::EditDocument &document) {
    const auto encoded = QJsonDocument::fromJson(
        luma::encodePreset({"VIS003 full pinned document", {}, document, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot serialize VIS003 pinned document");
    auto result = encoded.object();
    result["local_masks_serialized"] =
        QString::fromStdString(luma::encodeLocalMasks(document.localMasks));
    return result;
}
QJsonArray sqlRows(sqlite3 *database, const char *sql) {
    sqlite3_stmt *raw = nullptr;
    require(sqlite3_prepare_v2(database, sql, -1, &raw, nullptr) == SQLITE_OK,
            "Cannot prepare VIS003 durable row observation");
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw,
                                                                               sqlite3_finalize);
    QJsonArray rows;
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) {
        QJsonArray row;
        for (int index = 0; index < sqlite3_column_count(statement.get()); ++index) {
            const auto type = sqlite3_column_type(statement.get(), index);
            QJsonValue value = QJsonValue::Null;
            if (type == SQLITE_INTEGER)
                value = QString::number(sqlite3_column_int64(statement.get(), index));
            else if (type == SQLITE_FLOAT)
                value = QString::number(sqlite3_column_double(statement.get(), index), 'g', 17);
            else if (type == SQLITE_TEXT)
                value = QString::fromUtf8(
                    reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), index)),
                    sqlite3_column_bytes(statement.get(), index));
            else if (type == SQLITE_BLOB)
                value =
                    QString::fromLatin1(QByteArray(static_cast<const char *>(
                                                       sqlite3_column_blob(statement.get(), index)),
                                                   sqlite3_column_bytes(statement.get(), index))
                                            .toHex());
            row.append(QJsonObject{{"sqlite_type", type}, {"value", value}});
        }
        rows.append(row);
    }
    require(step == SQLITE_DONE, "Cannot finish VIS003 durable row observation");
    return rows;
}
QJsonObject catalogFacts(Context &c) {
    sqlite3 *raw = nullptr;
    const int opened =
        sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &raw, SQLITE_OPEN_READONLY, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    require(opened == SQLITE_OK &&
                sqlite3_exec(database.get(), "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK,
            "Cannot begin consistent VIS003 catalog observation");
    return {
        {"assets", sqlRows(database.get(), "SELECT * FROM assets ORDER BY id")},
        {"session", sqlRows(database.get(), "SELECT * FROM session ORDER BY slot")},
        {"imports", sqlRows(database.get(), "SELECT * FROM import_sessions ORDER BY id")},
        {"items",
         sqlRows(database.get(), "SELECT * FROM import_items ORDER BY session_id,position")},
        {"collections", sqlRows(database.get(), "SELECT * FROM collections ORDER BY id")},
        {"membership", sqlRows(database.get(),
                               "SELECT * FROM collection_assets ORDER BY collection_id,asset_id")}};
}
struct Pin {
    luma::AssetSnapshot snapshot;
    QString source, sha;
};
using Pins = std::array<Pin, count>;
Pins pins(Context &c, const std::array<QString, count> &sources,
          const std::array<QString, count> &hashes) {
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto rows = catalog.libraryPage({}, 0, count + 1);
    require(rows.size() == static_cast<std::size_t>(count) && catalog.libraryCount() == count,
            "VIS003 must own exactly four imported sources");
    Pins result;
    for (int variant = 0; variant < count; ++variant) {
        const auto row = std::find_if(rows.begin(), rows.end(), [&](const auto &value) {
            return QString::fromStdString(value.path.string()) == sources[variant];
        });
        require(row != rows.end(), "VIS003 imported source identity disappeared");
        const auto saved = catalog.assetSnapshot(row->id);
        require(saved && saved->revision == 0 && saved->asset.edits == luma::EditDocument{} &&
                    saved->asset.metadata.width == width &&
                    saved->asset.metadata.height == height &&
                    saved->asset.checksum == hashes[variant].toStdString() &&
                    row->filename == sourceName(variant).toStdString(),
                "VIS003 fresh import changed source checksum, neutral document or dimensions");
        result[variant] = {*saved, sources[variant], hashes[variant]};
    }
    return result;
}
void unchangedPins(Context &c, const Pins &expected) {
    luma::Catalog catalog(c.catalogFile.toStdString());
    for (const auto &pin : expected) {
        const auto actual = catalog.assetSnapshot(pin.snapshot.asset.id);
        require(actual && actual->revision == pin.snapshot.revision &&
                    actual->asset.edits == pin.snapshot.asset.edits &&
                    actual->asset.id == pin.snapshot.asset.id &&
                    actual->asset.path == pin.snapshot.asset.path &&
                    actual->asset.checksum == pin.snapshot.asset.checksum &&
                    std::tie(actual->asset.metadata.camera, actual->asset.metadata.lens,
                             actual->asset.metadata.iso, actual->asset.metadata.shutterSeconds,
                             actual->asset.metadata.aperture, actual->asset.metadata.focalLength,
                             actual->asset.metadata.captureTimestamp, actual->asset.metadata.width,
                             actual->asset.metadata.height, actual->asset.metadata.orientation) ==
                        std::tie(pin.snapshot.asset.metadata.camera,
                                 pin.snapshot.asset.metadata.lens, pin.snapshot.asset.metadata.iso,
                                 pin.snapshot.asset.metadata.shutterSeconds,
                                 pin.snapshot.asset.metadata.aperture,
                                 pin.snapshot.asset.metadata.focalLength,
                                 pin.snapshot.asset.metadata.captureTimestamp,
                                 pin.snapshot.asset.metadata.width,
                                 pin.snapshot.asset.metadata.height,
                                 pin.snapshot.asset.metadata.orientation) &&
                    digest(pin.source) == pin.sha,
                "VIS003 export changed a pinned full document/revision/identity/source SHA");
    }
}
void importOwned(Context &c, const std::array<QString, count> &sources) {
    auto &d = *c.driver;
    QList<QUrl> urls;
    for (const auto &source : sources)
        urls.append(QUrl::fromLocalFile(source));
    // Only native chooser RESULT is the fixture boundary. Import acceptance is actual UI input.
    require(d.object("library.importDialog")->setProperty("sources", QVariant::fromValue(urls)),
            "VIS003 chooser-result boundary rejected owned source URLs");
    d.log({{"action", "fixture_file_chooser_result"},
           {"source_count", count},
           {"native_chooser_exercised", false}});
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "VIS003 actual Import dialog opened");
    d.choose("library.importMode", 0);
    if (d.object("library.importDuplicates")->property("checked").toBool())
        d.click("library.importDuplicates");
    d.click("library.importStart");
    d.wait(
        [&] {
            return !c.library.working() && !c.library.loading() && !c.library.importing() &&
                   c.library.count() == count;
        },
        "VIS003 four owned sources actually imported");
    d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
           "VIS003 Import exit transition completed");
    require(c.library.error().isEmpty(), "VIS003 actual Import failed");
}
void open(Context &c, const Pin &pin) {
    auto &d = *c.driver;
    // All four actual Library cards fit this supported setup width; minimum-window
    // label checks start only AFTER photo activation/metadata/preview settle.
    d.resize({1320, 840});
    if (!c.window->property("libraryMode").toBool())
        d.click("main.libraryButton");
    int index = -1;
    d.wait(
        [&] {
            if (c.library.loading() || c.library.working() || c.library.count() != count)
                return false;
            for (int row = 0; row < count; ++row)
                if (c.library.data(c.library.index(row), LibraryModel::AssetID).toString() ==
                        QString::fromStdString(pin.snapshot.asset.id) &&
                    c.library.data(c.library.index(row), LibraryModel::Path)
                            .toUrl()
                            .toLocalFile() == pin.source) {
                    index = row;
                    return true;
                }
            return false;
        },
        "VIS003 exact owned card is populated");
    const auto cardID = c.card(index);
    d.frame();
    auto *card = d.item(cardID);
    auto *grid = d.item("library.grid");
    require(card->isVisible() && card->isEnabled() && card->width() > 0 && card->height() > 0 &&
                grid->boundingRect().contains(card->mapRectToItem(grid, card->boundingRect())) &&
                QRectF(0, 0, c.window->width(), c.window->height())
                    .contains(card->mapRectToScene(card->boundingRect())),
            "VIS003 setup card is not genuinely within the visible Library viewport");
    d.doubleClick(cardID);
    c.loaded();
    d.wait(
        [&] {
            auto *preview = d.item("develop.preview");
            return !c.library.loading() && !c.library.working() && !c.editor.saving() &&
                   c.editor.photoID() == QString::fromStdString(pin.snapshot.asset.id) &&
                   c.editor.sourcePath() == pin.source &&
                   c.editor.currentSettings() == pin.snapshot.asset.edits && preview->isVisible() &&
                   preview->property("status").toInt() == 1 &&
                   preview->property("source").toUrl() == c.editor.previewUrl();
        },
        "VIS003 actual photo and post-open metadata/preview are ready");
    d.resize({1000, 640});
    require(c.window->width() == 1000 && c.window->height() == 640 && !c.editor.canUndo() &&
                !c.editor.canRedo(),
            "VIS003 neutral minimum-window setup differs from its literal contract");
}
struct Baseline {
    QString active, source;
    luma::EditDocument document;
    std::vector<std::string> members, selected;
    QJsonObject facts;
    bool undo = false, redo = false;
};
Baseline baseline(Context &c) {
    return {c.editor.photoID(),         c.editor.sourcePath(),
            c.editor.currentSettings(), luma::Catalog(c.catalogFile.toStdString()).libraryIDs({}),
            c.library.selectionIDs(),   catalogFacts(c),
            c.editor.canUndo(),         c.editor.canRedo()};
}
void unchanged(Context &c, const Pins &all, const Baseline &before) {
    require(c.editor.photoID() == before.active && c.editor.sourcePath() == before.source &&
                c.editor.currentSettings() == before.document &&
                c.editor.canUndo() == before.undo && c.editor.canRedo() == before.redo &&
                !c.editor.original() && !c.editor.busy() && !c.editor.saving() &&
                !c.editor.presetPreview() && !c.editor.cropMode() && !c.editor.localMode() &&
                !c.editor.localGestureActive() && !c.editor.localSampling() &&
                c.editor.error().isEmpty() && !c.library.loading() && c.library.error().isEmpty() &&
                !c.batch.working() && !c.library.working() && !c.library.importing() &&
                !c.library.selecting() && c.library.count() == count &&
                c.library.selectionIDs() == before.selected &&
                luma::Catalog(c.catalogFile.toStdString()).libraryIDs({}) == before.members &&
                catalogFacts(c) == before.facts,
            "VIS003 UI/export changed full editor/history/metadata/journal/session/membership");
    unchangedPins(c, all);
}
QQuickItem *scroll(Context &c) {
    auto *result = qobject_cast<QQuickItem *>(
        qvariant_cast<QObject *>(c.driver->object("export.scroll")->property("contentItem")));
    require(result && result->property("contentY").isValid(), "Actual Export viewport is absent");
    return result;
}
void reveal(Context &c, const QString &id) {
    auto *view = scroll(c);
    auto *target = c.driver->item(id);
    require(target->isVisible() && target->isEnabled(), "VIS003 export control unavailable: " + id);
    for (int step = 0; step < 100; ++step) {
        c.driver->wait([&] { return !view->property("moving").toBool(); },
                       "VIS003 Export discrete wheel settled");
        c.driver->frame();
        const auto bounds = target->mapRectToItem(view, target->boundingRect());
        if (bounds.top() >= 0 && bounds.bottom() <= view->height())
            return;
        c.driver->wheel("export.scroll", bounds.top() < 0 ? 120 : -120);
    }
    throw std::runtime_error("Actual Export input did not expose VIS003 control: " +
                             id.toStdString());
}
QJsonObject configure(Context &c, const QString &destination) {
    auto &d = *c.driver;
    d.click("exportButton");
    d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
           "VIS003 actual Export settings opened");
    for (const auto &[name, value] :
         std::array{std::pair{QString("export.destination"), destination},
                    std::pair{QString("exportNaming"), QString("{filename}")}}) {
        reveal(c, name);
        d.type(name, value);
        d.key(Qt::Key_Tab);
    }
    for (const auto &[name, index] :
         std::array{std::pair{"exportFormat", 1}, std::pair{"export.conflict", 0},
                    std::pair{"export.colorSpace", 0}, std::pair{"export.resize", 0},
                    std::pair{"export.outputMedium", 0}, std::pair{"export.metadata", 0},
                    std::pair{"export.watermarkKind", 0}}) {
        reveal(c, name);
        d.choose(name, index);
    }
    const auto value = d.object("exportPanel")->property("form");
    const auto form =
        (value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant()
                                                             : value)
            .toMap();
    require(form.value("destination").toString() == destination &&
                form.value("naming").toString() == "{filename}" &&
                form.value("format").toInt() == 1 && form.value("conflict").toInt() == 0 &&
                form.value("colorSpace").toInt() == 0 && form.value("resize").toInt() == 0 &&
                form.value("outputMedium").toInt() == 0 && form.value("metadata").toInt() == 0 &&
                form.value("watermarkKind").toInt() == 0,
            "VIS003 actual form does not contain its exact PNG/neutral/skip recipe");
    return QJsonObject::fromVariantMap(form);
}
QRectF box(Context &c, const QString &id, bool painted = false) {
    auto *item = c.driver->item(id);
    require(item->isVisible() && item->width() > 0 && item->height() > 0,
            "VIS003 visual item is not visible/nonempty: " + id);
    // QQuickText::boundingRect describes laid-out text, not the allocated control.
    // Check both separately; stable layout comparisons use the actual item size.
    const auto local = painted ? item->boundingRect() : QRectF(QPointF{}, item->size());
    const auto result = item->mapRectToScene(local);
    require(std::isfinite(result.x()) && std::isfinite(result.y()) &&
                std::isfinite(result.width()) && std::isfinite(result.height()) &&
                QRectF(0, 0, c.window->width(), c.window->height())
                    .adjusted(-.5, -.5, .5, .5)
                    .contains(result),
            "VIS003 visual item lies outside the actual window: " + id);
    for (auto *ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem())
        if (ancestor->clip())
            require(ancestor->boundingRect()
                        .adjusted(-.5, -.5, .5, .5)
                        .contains(ancestor->mapRectFromScene(result)),
                    "VIS003 actual ancestor clips an observed item: " + id);
    return result;
}
QJsonObject geometry(Context &c, const QString &expected, int variant) {
    const QStringList ids{"export.currentFilename", "export.progress", "export.pause",
                          "export.cancel"};
    QJsonObject result, boxes, paintedBoxes;
    for (const auto &id : ids) {
        const auto bounds = box(c, id);
        boxes[id] = QJsonArray{bounds.x(), bounds.y(), bounds.width(), bounds.height()};
        const auto painted = box(c, id, true);
        require(bounds.adjusted(-.5, -.5, .5, .5).contains(painted),
                "VIS003 painted content escapes its allocated control: " + id);
        paintedBoxes[id] = QJsonArray{painted.x(), painted.y(), painted.width(), painted.height()};
    }
    for (int first = 0; first < ids.size(); ++first)
        for (int second = first + 1; second < ids.size(); ++second)
            require(!box(c, ids[first]).intersects(box(c, ids[second])),
                    "VIS003 filename/progress/actions overlap");
    auto *label = c.driver->item("export.currentFilename");
    auto *row = c.driver->item("export.pause")->parentItem();
    require(row && row == c.driver->item("export.cancel")->parentItem() && row->isVisible(),
            "VIS003 real queue action row identity is unavailable");
    const auto rowBounds = row->mapRectToScene(row->boundingRect());
    require(row->width() > 0 && row->height() > 0,
            "VIS003 queue action row has no positive geometry");
    for (auto *child : row->childItems())
        if (child->isVisible() && child->width() > 0 && child->height() > 0)
            require(row->boundingRect()
                        .adjusted(-.5, -.5, .5, .5)
                        .contains(child->mapRectToItem(row, child->boundingRect())),
                    "VIS003 a queue status/action child escaped its real row");
    require(QRectF(0, 0, c.window->width(), c.window->height())
                    .adjusted(-.5, -.5, .5, .5)
                    .contains(rowBounds) &&
                !box(c, "export.currentFilename").intersects(rowBounds),
            "VIS003 current filename overlaps or pushes the actual action row outside the window");
    for (auto *ancestor = label; ancestor; ancestor = ancestor->parentItem())
        require(ancestor->opacity() > 0, "VIS003 filename is transparent through an ancestor");
    require(
        label->property("text").toString() == expected && c.exporter.currentFile() == expected &&
            label->property("textFormat").toInt() == Qt::PlainText &&
            label->property("elide").toInt() == Qt::ElideMiddle &&
            label->property("truncated").isValid() && label->property("lineCount").toInt() == 1 &&
            label->property("contentHeight").toDouble() <= label->height() + .5 &&
            c.driver->item("export.pause")->isEnabled() &&
            c.driver->item("export.cancel")->isEnabled(),
        "VIS003 current label lost full logical text, permitted elision, vertical fit or controls");
    // No truncated=true requirement:115 Cyrillic glyphs can fit while230 ASCII glyphs elide.
    // Nonempty actual item readback is UI paint evidence, not OCR or physical scanout.
    QPointer<QQuickItem> guarded(label);
    const auto logicalSize = label->size();
    const auto capture = readyItemGrab(c, *label, "VIS003 actual current-label node readback");
    require(guarded && guarded.data() == c.driver->item("export.currentFilename") &&
                guarded->size() == logicalSize &&
                guarded->property("text").toString() == expected &&
                c.exporter.currentFile() == expected,
            "VIS003 label rebound during current-file readback");
    const auto image = capture->image().convertToFormat(QImage::Format_RGBA8888);
    int visiblePixels = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            visiblePixels += image.pixelColor(x, y).alpha() > 0;
    require(visiblePixels > 0, "VIS003 current filename did not paint any visible text pixels");
    const auto imagePath = c.root + QStringLiteral("/vis003-current-label-%1.png").arg(variant);
    require(image.save(imagePath), "VIS003 cannot preserve current-label paint evidence");
    result["boxes"] = boxes;
    result["geometry_basis"] =
        "allocated item rectangles; laid-out text bounds recorded separately";
    result["painted_boxes"] = paintedBoxes;
    result["logical_text"] = expected;
    result["logical_characters"] = static_cast<qint64>(expected.size());
    result["logical_utf8_bytes"] = static_cast<qint64>(expected.toUtf8().size());
    result["middle_elision_allowed"] = true;
    result["actually_truncated"] = label->property("truncated").toBool();
    result["actual_content_width"] = label->property("contentWidth").toDouble();
    result["paint_readback"] = QJsonObject{{"path", imagePath},
                                           {"sha256", digest(imagePath)},
                                           {"width", image.width()},
                                           {"height", image.height()},
                                           {"nontransparent_pixels", visiblePixels}};
    return result;
}
QJsonObject output(const QString &path, int variant) {
    require(QFileInfo(path).isFile() && !QFileInfo(path).isSymLink(),
            "VIS003 exact committed output is absent/not regular");
    auto image = QImage(path);
    require(image.size() == QSize(width, height) && image.colorSpace().isValid(),
            "VIS003 output lost literal dimensions/embedded color profile");
    image.convertToColorSpace(QColorSpace::SRgb);
    require(!image.isNull(), "Independent Qt ICC output decode failed");
    int maximum = 0;
    std::array<double, count> wrongTotals{};
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const auto actual = image.pixelColor(x, y), expected = sourcePixel(x, y, variant);
            require(actual.alpha() == 255, "VIS003 opaque output gained transparent pixels");
            for (int channel = 0; channel < 3; ++channel) {
                const int value = channel == 0   ? actual.red()
                                  : channel == 1 ? actual.green()
                                                 : actual.blue();
                const int own = channel == 0   ? expected.red()
                                : channel == 1 ? expected.green()
                                               : expected.blue();
                maximum = std::max(maximum, std::abs(value - own));
                for (int otherVariant = 0; otherVariant < count; ++otherVariant) {
                    if (otherVariant == variant)
                        continue;
                    const auto wrong = sourcePixel(x, y, otherVariant);
                    const int other = channel == 0   ? wrong.red()
                                      : channel == 1 ? wrong.green()
                                                     : wrong.blue();
                    wrongTotals[static_cast<std::size_t>(otherVariant)] += std::abs(value - other);
                }
            }
        }
    double wrongMean = std::numeric_limits<double>::infinity();
    for (int otherVariant = 0; otherVariant < count; ++otherVariant)
        if (otherVariant != variant)
            wrongMean = std::min(wrongMean, wrongTotals[static_cast<std::size_t>(otherVariant)] /
                                                (width * height * 3));
    require(
        maximum <= 1 && wrongMean > 80,
        "VIS003 output differs from independent literal full-image pixels or wrong-source control");
    return {{"path", path},
            {"sha256", digest(path)},
            {"width", width},
            {"height", height},
            {"maximum_own_rgb_delta", maximum},
            {"wrong_source_mean_rgb_delta", wrongMean}};
}
void exactOutputs(const QString &directory, const QMap<QString, QString> &expected) {
    const auto names = QDir(directory).entryList(
        QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    require(names == expected.keys(),
            "VIS003 exported unexpected files or leaked temporary output");
    for (auto it = expected.cbegin(); it != expected.cend(); ++it)
        require(digest(directory + '/' + it.key()) == it.value(),
                "VIS003 export replaced a pinned existing output/sentinel");
}
QJsonObject queue(Context &c) {
    return {{"working", c.exporter.working()},
            {"waiting_for_save", c.exporter.waitingForSave()},
            {"paused", c.exporter.paused()},
            {"pause_pending", c.exporter.pausePending()},
            {"total", c.exporter.total()},
            {"completed", c.exporter.completed()},
            {"succeeded", c.exporter.succeeded()},
            {"skipped", c.exporter.skipped()},
            {"progress", c.exporter.progress()},
            {"current_file", c.exporter.currentFile()},
            {"error", c.exporter.error()},
            {"failures", QJsonArray::fromStringList(c.exporter.failures())}};
}
void run(Context &c, const QString &id) {
    auto &d = *c.driver;
    require(c.library.count() == 0 && !c.editor.hasPhoto() && !c.exporter.working(),
            "VIS003 requires a fresh isolated Context");
    require(QDir(c.fixture)
                        .entryList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                        .size() == count &&
                sourceStem(1).toUtf8().size() == 230 && sourceStem(1).size() == 230 &&
                sourceStem(2).toUtf8().size() == 230 && sourceStem(2).size() == 115 &&
                sourceStem(3).toUtf8().size() == 231 && sourceStem(3).size() == 116,
            "VIS003 exact four-source ASCII/UTF8 boundary fixture differs");
    std::array<QString, count> sources, hashes;
    for (int variant = 0; variant < count; ++variant) {
        sources[variant] = c.fixture + '/' + sourceName(variant);
        require(QFileInfo(sources[variant]).isFile() && !QFileInfo(sources[variant]).isSymLink() &&
                    QFileInfo(sources[variant]).canonicalFilePath() == sources[variant],
                "VIS003 source is not its owned canonical regular fixture");
        const QImage image(sources[variant]);
        require(image.size() == QSize(width, height), "VIS003 source dimensions differ");
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                require(image.pixelColor(x, y) == sourcePixel(x, y, variant),
                        "VIS003 source differs from the independent literal RGB recipe");
        hashes[variant] = digest(sources[variant]);
    }
    importOwned(c, sources);
    const auto all = pins(c, sources, hashes);
    QJsonArray originals;
    for (const auto &pin : all)
        originals.append(QJsonObject{{"uuid", QString::fromStdString(pin.snapshot.asset.id)},
                                     {"source", pin.source},
                                     {"sha256", pin.sha},
                                     {"revision", QString::number(pin.snapshot.revision)},
                                     {"full_document", documentJSON(pin.snapshot.asset.edits)}});
    const auto destination = c.root + "/exports";
    require(!QFileInfo::exists(destination) && QDir().mkpath(destination),
            "VIS003 requires a fresh owned output directory");
    QFile sentinel(destination + "/Preserve-existing.txt");
    const QByteArray sentinelBytes("Owned VIS003 preservation control\n");
    require(sentinel.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                sentinel.write(sentinelBytes) == sentinelBytes.size(),
            "VIS003 cannot create its sentinel");
    sentinel.close();
    QMap<QString, QString> committed{{"Preserve-existing.txt", digest(sentinel.fileName())}};
    const auto settingsPath = QFileInfo(c.catalogFile).absolutePath() + "/export-settings.json";
    QJsonArray attempts;
    QJsonObject evidence{
        {"scenario", id},
        {"specification_case", "UX-VIS-003"},
        {"inventory_domain", id == officialID ? "specification" : "supplemental"},
        {"full_release_qualified", false},
        {"expanded_stem_utf8_limit", 230},
        {"actual_format_extension", ".png"},
        {"intentional_middle_elision_allowed", true},
        {"production_cap_changed", false},
        {"originals", originals},
        {"native_file_picker_exercised", false},
        {"output_oracle",
         "all literal RGB pixels + Qt PNG/ICC decode, max1/255 and every wrong-source mean>80"},
        {"observation_boundary", "actual committed PNG before original queued GUI acknowledgement"},
        {"physical_scanout_OCR_or_all_formats_claimed", false}};
    const auto persist = [&] {
        evidence["attempts"] = attempts;
        writeJson(c.root + "/vis003-approved-evidence.json", evidence);
    };
    persist();
    QJsonObject shortBoxes, previousLiveSettings;
    QByteArray previousStore;
    QSignalSpy allSaves(&c.editor, &EditorController::editsSaved);
    require(allSaves.isValid(), "VIS003 cannot observe unexpected editor writes");
    for (int variant = 0; variant < count; ++variant) {
        open(c, all[variant]);
        const auto before = baseline(c);
        unchanged(c, all, before);
        require(allSaves.count() == 0, "VIS003 activation/export unexpectedly saved an edit");
        exactOutputs(destination, committed);
        const auto requested = configure(c, destination);
        const auto expected = sourceName(variant), target = destination + '/' + expected;
        require(!QFileInfo::exists(target), "VIS003 target must not already exist");
        const bool reject = variant == 3;
        if (reject)
            require(!previousLiveSettings.isEmpty() && requested == previousLiveSettings &&
                        QJsonObject::fromVariantMap(c.exporter.settings()) ==
                            previousLiveSettings &&
                        bytes(settingsPath) == previousStore,
                    "VIS003 rejection setup changed the preceding actual export recipe/store");
        QJsonObject attempt{
            {"variant", variant},
            {"expected_current_file", expected},
            {"expanded_stem_utf8_bytes", static_cast<qint64>(sourceStem(variant).toUtf8().size())},
            {"source_basename_utf8_bytes", static_cast<qint64>(expected.toUtf8().size())},
            {"source_basename_characters", static_cast<qint64>(expected.size())},
            {"before_catalog_facts", before.facts},
            {"requested_full_settings", requested},
            {"before_queue", queue(c)},
            {"rejection_expected", reject}};
        QSignalSpy clicked(d.object("export.current"), SIGNAL(clicked()));
        require(clicked.isValid(), "VIS003 actual current-photo button lacks clicked signal");
        bool started = false;
        QJsonArray states;
        QObject observer;
        QObject::connect(&c.exporter, &ExportController::changed, &observer, [&] {
            if (c.exporter.working())
                started = true;
            if (started)
                states.append(queue(c)); // Exclude preceding terminal state emitted by configure.
        });
        QJsonObject heldGeometry, heldOutput, liveSettings;
        QByteArray liveStore;
        QueuedCallGate gate(
            c.exporter,
            [&] {
                return c.exporter.working() && !c.exporter.waitingForSave() &&
                       !c.exporter.paused() && c.exporter.total() == 1 &&
                       c.exporter.completed() == 0 && c.exporter.progress() == 1 &&
                       c.exporter.currentFile() == expected;
            },
            [&] {
                require(
                    !reject && c.exporter.error().isEmpty() && c.exporter.failures().isEmpty() &&
                        !c.editor.editingLocked(),
                    "VIS003 legal-file observation did not reach healthy real committed output");
                heldOutput = output(target, variant);
                liveSettings = QJsonObject::fromVariantMap(c.exporter.settings());
                liveStore = bytes(settingsPath);
                require(liveSettings == requested,
                        "VIS003 active export silently changed full configured settings");
                // A small real job can finish before the Dialog's close animation. Observe
                // genuine current-file state after the existing popup transition, while the
                // original completion delivery remains on its event-filter stack.
                d.wait([&] { return !d.object("exportPanel")->property("visible").toBool(); },
                       "VIS003 Export popup close transition", 5000);
                d.frame();
                heldGeometry = geometry(c, expected, variant);
                const auto boxes = heldGeometry["boxes"].toObject();
                if (variant > 0) {
                    require(boxes.keys() == shortBoxes.keys(),
                            "VIS003 long filename changed observed layout membership");
                    for (auto it = shortBoxes.constBegin(); it != shortBoxes.constEnd(); ++it) {
                        const auto first = it.value().toArray(), second = boxes[it.key()].toArray();
                        require(first.size() == 4 && second.size() == 4,
                                "VIS003 layout rectangle shape differs");
                        for (int axis = 0; axis < 4; ++axis)
                            require(std::abs(first[axis].toDouble() - second[axis].toDouble()) <=
                                        .5,
                                    "VIS003 longest legal filename displaced its progress/action "
                                    "layout");
                    }
                }
                unchanged(c, all, before);
                d.screenshot(c.root + QStringLiteral("/vis003-active-export-%1.png").arg(variant));
            });
        try {
            d.click("export.current");
            d.wait([&] { return started && !c.exporter.working(); }, "VIS003 actual queue terminal",
                   30000);
            require(clicked.count() == 1,
                    "VIS003 did not enqueue with exactly one actual UI click");
            if (gate.failure)
                std::rethrow_exception(gate.failure);
            unchanged(c, all, before);
            require(allSaves.count() == 0, "VIS003 export wrote an edit/history unit");
            attempt["observed_states"] = states;
            attempt["terminal_queue"] = queue(c);
            attempt["real_enqueue_clicks"] = clicked.count();
            attempt["after_catalog_facts"] = catalogFacts(c);
            attempt["full_source_document_history_metadata_journal_guard"] = true;
            attempt["commit_boundary_caught"] = gate.caught;
            if (reject) {
                // startIDs lawfully clears the preceding successful queue's counters.
                // Require no new accepted item/next()/file execution, not preservation
                // of those historical counters or status strings.
                bool noFileStarted = started && !states.isEmpty();
                for (const auto &entry : states) {
                    const auto state = entry.toObject();
                    noFileStarted =
                        noFileStarted && state["current_file"].toString().isEmpty() &&
                        state["total"].toInt() == 0 && state["completed"].toInt() == 0 &&
                        state["succeeded"].toInt() == 0 && state["skipped"].toInt() == 0;
                }
                require(!gate.caught && noFileStarted &&
                            c.exporter.error() ==
                                "Generated export filename is invalid or too long." &&
                            !QFileInfo::exists(target),
                        "VIS003 231-byte expanded stem was not rejected before file execution");
                exactOutputs(destination, committed);
                require(QJsonObject::fromVariantMap(c.exporter.settings()) ==
                                previousLiveSettings &&
                            bytes(settingsPath) == previousStore,
                        "VIS003 planning rejection changed live/persisted full export settings");
                attempt["no_new_file_execution_observed"] = true;
                attempt["existing_outputs_sentinel_and_settings_unchanged"] = true;
                attempt["coverage"] = "UTF8_231_REJECTED_BEFORE_EXECUTION";
            } else {
                require(
                    gate.caught && !heldGeometry.isEmpty() && !heldOutput.isEmpty() &&
                        c.exporter.total() == 1 && c.exporter.completed() == 1 &&
                        c.exporter.succeeded() == 1 && c.exporter.skipped() == 0 &&
                        c.exporter.error().isEmpty() && c.exporter.failures().isEmpty(),
                    "VIS003 actual legal export missed observable currentFile/one real PNG result");
                require(output(target, variant) == heldOutput &&
                            QFileInfo(target).fileName() == expected &&
                            c.exporter.currentFile() == expected &&
                            QJsonObject::fromVariantMap(c.exporter.settings()) == liveSettings &&
                            bytes(settingsPath) == liveStore,
                        "VIS003 acknowledged output basename/pixels/full recipe differs from held "
                        "state");
                committed.insert(expected, digest(target));
                exactOutputs(destination, committed);
                previousLiveSettings = liveSettings;
                previousStore = liveStore;
                if (variant == 0)
                    shortBoxes = heldGeometry["boxes"].toObject();
                attempt["actual_current_label"] = heldGeometry;
                attempt["actual_output"] = heldOutput;
                attempt["full_settings_after"] = liveSettings;
                attempt["settings_store_sha256"] = digest(settingsPath);
                attempt["coverage"] = variant == 0   ? "SHORT_POSITIVE_CONTROL"
                                      : variant == 1 ? "ASCII_EXPANDED_STEM_230"
                                                     : "MULTIBYTE_EXPANDED_STEM_230";
            }
            attempts.append(attempt);
            persist();
        } catch (const std::exception &error) {
            const auto original = std::current_exception();
            attempt["observed_states"] = states;
            attempt["terminal_queue"] = queue(c);
            attempt["commit_boundary_caught"] = gate.caught;
            attempt["actual_label"] = heldGeometry;
            attempt["actual_output"] = heldOutput;
            attempt["failure"] = QString::fromUtf8(error.what());
            attempts.append(attempt);
            evidence["status"] = "FAIL";
            evidence["failure_classification"] = "UNCLASSIFIED_CANDIDATE_FAILURE";
            try {
                persist();
            } catch (const std::exception &saveError) {
                std::fprintf(stderr, "VIS003 failure-evidence capture failed: %s\n",
                             saveError.what());
            }
            std::rethrow_exception(original);
        }
    }
    evidence["status"] = "PASS";
    persist();
}
} // namespace
bool exportCurrentFilenameLegalCandidate(Context &c, const QString &id) {
    if (id != candidateID && id != officialID)
        return false;
    run(c, id);
    return true;
}
} // namespace ux
