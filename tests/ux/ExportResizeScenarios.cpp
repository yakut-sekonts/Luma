#include "apps/shared/PhotoDecoder.h"
#include "core/export/LinearOutput.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/ExportColor.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QColor>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSignalSpy>
#include <QStyleHints>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <sqlite3.h>
#include <stdexcept>
#include <vector>

// Actual mode/scalar inputs and complete PNG outputs; no simultaneous-axis UI is assumed.
namespace ux {
namespace {
struct KeyboardNavigation {
    const Qt::TabFocusBehavior previous = QGuiApplication::styleHints()->tabFocusBehavior();
    KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    }
    ~KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(previous);
    }
};
QByteArray bytes(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read owned resize evidence: " + path);
    return file.readAll();
}
QByteArray digest(const QString &path) {
    return QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256);
}
QVariantMap form(Context &c) {
    const auto value = c.driver->object("exportPanel")->property("form");
    return (value.metaType() == QMetaType::fromType<QJSValue>()
                ? value.value<QJSValue>().toVariant()
                : value)
        .toMap();
}
QQuickItem *exportScroll(Context &c) {
    auto *item = qobject_cast<QQuickItem *>(
        qvariant_cast<QObject *>(c.driver->object("export.scroll")->property("contentItem")));
    require(item && item->property("contentY").isValid(), "Export Flickable is not ready");
    return item;
}
void reveal(Context &c, const QString &id) {
    auto *target = c.driver->item(id);
    auto *view = exportScroll(c);
    require(target->isVisible() && target->isEnabled(), "Export input is unavailable: " + id);
    for (int attempt = 0; attempt < 100; ++attempt) {
        c.driver->wait([&] { return !view->property("moving").toBool(); },
                       "resize form wheel settled");
        c.driver->frame();
        const auto rectangle = target->mapRectToItem(view, target->boundingRect());
        if (rectangle.top() >= 0 && rectangle.bottom() <= view->height())
            return;
        c.driver->wheel("export.scroll", rectangle.top() < 0 ? 120 : -120);
    }
    throw std::runtime_error("Actual Export wheel input failed to reveal " + id.toStdString());
}
void choose(Context &c, const QString &id, int index) {
    reveal(c, id);
    c.driver->choose(id, index);
    require(c.driver->object(id)->property("currentIndex").toInt() == index,
            "Actual resize form choice did not reach the requested index");
}
void text(Context &c, const QString &id, const QString &value) {
    reveal(c, id);
    c.driver->type(id, value);
    c.driver->key(Qt::Key_Tab); // Real editingFinished, no form/controller mutation.
}
QJsonArray sqlRows(sqlite3 *database, const char *sql) {
    sqlite3_stmt *raw = nullptr;
    const int prepared = sqlite3_prepare_v2(database, sql, -1, &raw, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw,
                                                                               sqlite3_finalize);
    require(prepared == SQLITE_OK, "Cannot prepare resize catalog observation");
    QJsonArray rows;
    int step = SQLITE_ROW;
    while ((step = sqlite3_step(statement.get())) == SQLITE_ROW) {
        QJsonArray row;
        for (int column = 0; column < sqlite3_column_count(statement.get()); ++column) {
            const int type = sqlite3_column_type(statement.get(), column);
            QJsonValue value = QJsonValue::Null;
            if (type == SQLITE_INTEGER)
                value = QString::number(sqlite3_column_int64(statement.get(), column));
            else if (type == SQLITE_FLOAT)
                value = QString::number(sqlite3_column_double(statement.get(), column), 'g', 17);
            else if (type == SQLITE_TEXT)
                value = QString::fromUtf8(
                    reinterpret_cast<const char *>(sqlite3_column_text(statement.get(), column)),
                    sqlite3_column_bytes(statement.get(), column));
            else if (type == SQLITE_BLOB)
                value = QString::fromLatin1(
                    QByteArray(
                        static_cast<const char *>(sqlite3_column_blob(statement.get(), column)),
                        sqlite3_column_bytes(statement.get(), column))
                        .toHex());
            row.append(QJsonObject{{"sqlite_type", type}, {"value", value}});
        }
        rows.append(row);
    }
    require(step == SQLITE_DONE, "Cannot finish resize catalog observation");
    return rows;
}
QByteArray catalogFacts(Context &c) {
    sqlite3 *raw = nullptr;
    const int opened =
        sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &raw, SQLITE_OPEN_READONLY, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    require(opened == SQLITE_OK &&
                sqlite3_exec(database.get(), "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK,
            "Cannot begin consistent resize catalog observation");
    // Full metadata/import/collection rows, one read snapshot. Edit documents are
    // compared through their public typed Catalog API, without revision assumptions.
    return QJsonDocument(
               QJsonObject{
                   {"assets", sqlRows(database.get(), "SELECT * FROM assets ORDER BY id")},
                   {"session", sqlRows(database.get(), "SELECT * FROM session ORDER BY slot")},
                   {"imports",
                    sqlRows(database.get(), "SELECT * FROM import_sessions ORDER BY id")},
                   {"items", sqlRows(database.get(),
                                     "SELECT * FROM import_items ORDER BY session_id,position")},
                   {"collections",
                    sqlRows(database.get(), "SELECT * FROM collections ORDER BY id")},
                   {"membership",
                    sqlRows(database.get(),
                            "SELECT * FROM collection_assets ORDER BY collection_id,asset_id")}})
        .toJson(QJsonDocument::Compact);
}
void generatedSource(const QString &path, QSize size, int variant) {
    require(!QFileInfo::exists(path), "Resize source must be a new private file");
    QImage image(size, QImage::Format_RGBA8888);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y,
                                QColor(20 + (x * 3 + y * 7 + variant * 11) % 190,
                                       25 + (x * 11 + y * 2) % 180,
                                       30 + (x * 5 + y * 13 + variant * 37) % 170));
    require(image.save(path, "PNG"), "Cannot encode private resize source");
}
void metadataReady(Context &c, const QStringList &sources) {
    c.driver->wait(
        [&] {
            if (c.library.working() || c.library.loading() || c.library.count() != sources.size())
                return false;
            for (int row = 0; row < sources.size(); ++row)
                if (c.library.data(c.library.index(row), LibraryModel::AssetID)
                        .toString()
                        .isEmpty() ||
                    c.library.data(c.library.index(row), LibraryModel::Path)
                            .toUrl()
                            .toLocalFile() != sources[row])
                    return false;
            return true;
        },
        "exact owned landscape/portrait Library rows are loaded");
    c.driver->frame();
}
void importOwned(Context &c, const QStringList &sources) {
    require(c.library.count() == 0 && c.exporter.profiles().isEmpty(),
            "Resize scenario requires a fresh private Context");
    QList<QUrl> urls;
    for (const auto &source : sources)
        urls.append(QUrl::fromLocalFile(source));
    require(
        c.driver->object("library.importDialog")->setProperty("sources", QVariant::fromValue(urls)),
        "Owned chooser-result boundary rejected source URLs");
    c.driver->log({{"action", "fixture_file_chooser_result"},
                   {"source_count", 2},
                   {"native_chooser_exercised", false}});
    c.driver->click("library.importButton");
    c.driver->wait(
        [&] { return c.driver->object("library.importDialog")->property("opened").toBool(); },
        "resize source Import opened");
    c.driver->choose("library.importMode", 0);
    if (c.driver->object("library.importDuplicates")->property("checked").toBool())
        c.driver->click("library.importDuplicates");
    c.driver->click("library.importStart");
    metadataReady(c, sources);
    c.driver->wait(
        [&] { return !c.driver->object("library.importDialog")->property("visible").toBool(); },
        "resize setup Import exit transition completed");
    require(c.library.error().isEmpty(), "Owned resize source import failed");
}
void naturalSave(Context &c, const luma::EditDocument &expected) {
    const auto id = c.editor.photoID().toStdString();
    c.driver->wait(
        [&] {
            const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
            return !c.editor.saving() && !c.editor.busy() && c.editor.error().isEmpty() && active &&
                   active->id == id && active->edits == expected;
        },
        "resize sentinel naturally committed in full");
}
void readyPixels(Context &c, const QImage &expected) {
    c.driver->wait(
        [&] {
            auto *image = c.driver->item("develop.preview");
            return !c.window->property("libraryMode").toBool() && image->isVisible() &&
                   image->property("status").toInt() == 1 &&
                   image->property("source").toUrl().toString() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == expected;
        },
        "resize sentinel exact Ready preview");
    c.driver->frame();
}
luma::EditDocument prepareSentinel(Context &c) {
    require(c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo() &&
                !c.editor.canRedo() && !c.editor.original(),
            "Resize photo did not open with its pristine full document/history");
    c.reveal("sharpen.amountSlider");
    auto *slider = c.driver->item("sharpen.amountSlider");
    require(slider->isEnabled() && slider->property("stepSize").toDouble() == 1 &&
                slider->property("from").toDouble() == 0 &&
                slider->property("to").toDouble() == 150,
            "Resize sentinel Slider has changed semantics");
    for (int tab = 0; tab < 240 && !slider->hasActiveFocus(); ++tab)
        c.driver->key(Qt::Key_Tab);
    c.reveal("sharpen.amountSlider");
    luma::EditDocument expected;
    for (int value = 1; value <= 2; ++value) {
        require(slider->hasActiveFocus() && c.window->activeFocusItem() == slider &&
                    QGuiApplication::focusWindow() == c.window,
                "Resize sentinel lacks the real Slider keyboard target");
        c.driver->key(Qt::Key_Right);
        expected.sharpenAmount = value;
        require(c.editor.currentSettings() == expected && !c.editor.canRedo() &&
                    !slider->property("pressed").toBool(),
                "Resize sentinel key failed to create its independent full document");
    }
    c.driver->click("main.undoButton");
    expected.sharpenAmount = 1;
    require(c.editor.currentSettings() == expected && c.editor.canUndo() && c.editor.canRedo(),
            "Resize sentinel did not retain both history branches");
    naturalSave(c, expected);
    return expected;
}
struct Baseline {
    QString source, active;
    luma::EditDocument document;
    std::vector<std::string> members, selected;
    QMap<QString, luma::EditDocument> documents;
    QMap<QString, QByteArray> sourceHashes;
    QByteArray catalogRows;
};
Baseline baseline(Context &c, const QMap<QString, QByteArray> &originalHashes) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    Baseline result;
    result.source = c.editor.sourcePath();
    result.active = c.editor.photoID();
    result.document = c.editor.currentSettings();
    result.members = catalog.libraryIDs({});
    result.selected = c.library.selectionIDs();
    result.catalogRows = catalogFacts(c);
    result.sourceHashes = originalHashes; // Frozen BEFORE any production import/edit input.
    require(result.members.size() == 2 && originalHashes.size() == 2,
            "Resize baseline lost its exact two owned assets");
    for (const auto &id : result.members) {
        const auto asset = catalog.assetByID(id);
        if (!asset)
            throw std::runtime_error("Resize baseline asset disappeared");
        require(QString::fromStdString(asset->path.string()).startsWith(c.root + "/owned-input/"),
                "Resize baseline contains an unowned source");
        const auto path = QString::fromStdString(asset->path.string());
        result.documents.insert(QString::fromStdString(id), asset->edits);
        require(originalHashes.contains(path) && digest(path) == originalHashes.value(path) &&
                    QString::fromLatin1(originalHashes.value(path).toHex()).toStdString() ==
                        asset->checksum,
                "Resize baseline source SHA disagrees with immutable pre-import bytes or Catalog");
    }
    return result;
}
void invariant(Context &c, const Baseline &before) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    const auto active = catalog.activeAsset();
    require(c.editor.photoID() == before.active && c.editor.sourcePath() == before.source &&
                c.editor.currentSettings() == before.document && !c.editor.original() &&
                c.editor.canUndo() && c.editor.canRedo() && c.editor.hasPhoto() &&
                !c.editor.saving() && !c.editor.busy() && !c.editor.editingLocked() &&
                c.editor.error().isEmpty() && !c.exporter.working() && !c.library.working() &&
                !c.library.loading() && !c.batch.working() && c.library.error().isEmpty() &&
                !c.window->property("libraryMode").toBool() && active &&
                active->id == before.active.toStdString() && active->edits == before.document &&
                catalog.libraryIDs({}) == before.members &&
                c.library.selectionIDs() == before.selected &&
                catalogFacts(c) == before.catalogRows,
            "Resize UI/export changed full document/history, active identity or catalog "
            "metadata/journal");
    for (auto it = before.documents.cbegin(); it != before.documents.cend(); ++it) {
        const auto asset = catalog.assetByID(it.key().toStdString());
        require(asset && asset->edits == it.value(),
                "Resize altered an owned durable full document");
    }
    for (auto it = before.sourceHashes.cbegin(); it != before.sourceHashes.cend(); ++it)
        require(digest(it.key()) == it.value(), "Resize altered private original source bytes");
}
struct Recipe {
    QString name;
    int mode, seedMode;
    QString text;
    double scalar;
    QSize landscape, portrait;
};

QImage expectedOutput(const QString &source, const luma::EditDocument &document, QSize dimensions,
                      bool original) {
    const auto decoded = luma::PhotoDecoder().decode(source.toStdString());
    require((QSize(decoded.image.width, decoded.image.height) == QSize(160, 96) ||
             QSize(decoded.image.width, decoded.image.height) == QSize(96, 160)) &&
                decoded.image.alpha.empty(),
            "Resize reference requires known small opaque inputs");
    if (original) {
        const auto pixels =
            luma::renderCPU(decoded.image, document, decoded.metadata.orientation, {}, {},
                            decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
        require(QSize(pixels.width, pixels.height) == dimensions,
                "Literal Original dimensions are wrong");
        return QImage(pixels.rgba.data(), pixels.width, pixels.height,
                      static_cast<qsizetype>(pixels.width) * 4, QImage::Format_RGBA8888)
            .copy();
    }
    // Render only the typed document. Never call ExportOptions::dimensions(),
    // resizeOutput(), ImageExporter or a queue runner from this oracle.
    const auto developed = luma::renderLinearOutput(decoded, document);
    require(QSize(developed.width, developed.height) ==
                QSize(decoded.image.width, decoded.image.height),
            "Resize-only fixture unexpectedly changed developed geometry");
    for (std::size_t index = 3; index < developed.rgba.size(); index += 4)
        require(developed.rgba[index] == 1, "Opaque resize reference gained coverage holes");
    const bool shrinking =
        dimensions.width() < developed.width && dimensions.height() < developed.height;
    require(shrinking ||
                (dimensions.width() > developed.width && dimensions.height() > developed.height),
            "Reference fixture must strictly shrink or enlarge both axes");
    std::vector<float> linear(static_cast<std::size_t>(dimensions.width()) * dimensions.height() *
                              3);
    for (int y = 0; y < dimensions.height(); ++y)
        for (int x = 0; x < dimensions.width(); ++x) {
            std::array<double, 3> sum{};
            if (shrinking) {
                // Direct 2D box integration; production uses two separable float passes.
                const double left = double(x) * developed.width / dimensions.width();
                const double right = double(x + 1) * developed.width / dimensions.width();
                const double top = double(y) * developed.height / dimensions.height();
                const double bottom = double(y + 1) * developed.height / dimensions.height();
                const double area = (right - left) * (bottom - top);
                for (int sy = std::max(0, int(std::floor(top)));
                     sy < std::min(developed.height, int(std::ceil(bottom))); ++sy)
                    for (int sx = std::max(0, int(std::floor(left)));
                         sx < std::min(developed.width, int(std::ceil(right))); ++sx) {
                        const double overlap = std::max(0., std::min(right, double(sx + 1)) -
                                                                std::max(left, double(sx))) *
                                               std::max(0., std::min(bottom, double(sy + 1)) -
                                                                std::max(top, double(sy)));
                        const auto offset =
                            (static_cast<std::size_t>(sy) * developed.width + sx) * 4;
                        for (int channel = 0; channel < 3; ++channel)
                            sum[channel] += developed.rgba[offset + channel] * overlap / area;
                    }
            } else {
                const double cx = std::clamp((x + .5) * developed.width / dimensions.width() - .5,
                                             0., double(developed.width - 1));
                const double cy = std::clamp((y + .5) * developed.height / dimensions.height() - .5,
                                             0., double(developed.height - 1));
                const int left = int(std::floor(cx)), top = int(std::floor(cy));
                const double fx = cx - left, fy = cy - top;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const int sx = std::min(left + dx, developed.width - 1);
                        const int sy = std::min(top + dy, developed.height - 1);
                        const double weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
                        const auto offset =
                            (static_cast<std::size_t>(sy) * developed.width + sx) * 4;
                        for (int channel = 0; channel < 3; ++channel)
                            sum[channel] += developed.rgba[offset + channel] * weight;
                    }
            }
            const auto offset = (static_cast<std::size_t>(y) * dimensions.width() + x) * 3;
            for (int channel = 0; channel < 3; ++channel)
                linear[offset + channel] = static_cast<float>(sum[channel]);
        }
    // ICC encoding is deliberately shared; this scenario does not qualify ICC
    // numerical correctness. Dimension literals and 2D resampling are independent.
    const luma::macos::ExportColor color(luma::ExportColorSpace::srgb);
    std::vector<float> encoded(linear.size());
    color.convert(linear, encoded, false);
    QImage result(dimensions, QImage::Format_RGBA8888);
    for (int y = 0; y < result.height(); ++y) {
        auto *row = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            const auto offset = (static_cast<std::size_t>(y) * result.width() + x) * 3;
            for (int channel = 0; channel < 3; ++channel)
                row[x * 4 + channel] = static_cast<uchar>(
                    std::lround(std::clamp(double(encoded[offset + channel]), 0., 1.) * 255));
            row[x * 4 + 3] = 255;
        }
    }
    return result;
}
int comparePixels(const QString &output, const QImage &reference) {
    const auto actual = QImage(output).convertToFormat(QImage::Format_RGBA8888);
    require(!actual.isNull() && actual.size() == reference.size(),
            QString("Actual PNG %1 has %2x%3; independent expected dimensions are %4x%5")
                .arg(output)
                .arg(actual.width())
                .arg(actual.height())
                .arg(reference.width())
                .arg(reference.height()));
    int maximum = 0;
    for (int y = 0; y < actual.height(); ++y)
        for (int x = 0; x < actual.width(); ++x) {
            const auto a = actual.pixelColor(x, y), e = reference.pixelColor(x, y);
            require(a.alpha() == 255 && e.alpha() == 255, "Opaque resize output gained alpha");
            maximum = std::max({maximum, std::abs(a.red() - e.red()),
                                std::abs(a.green() - e.green()), std::abs(a.blue() - e.blue())});
        }
    // One RGBA8 level accounts for double-integral vs separable-float rounding.
    // Dimensions and the complete raster remain independently checked.
    require(maximum <= 1,
            QString("Actual resize PNG %1 exceeds independent full-raster reference: RGB delta=%2")
                .arg(output)
                .arg(maximum));
    return maximum;
}
void checkOutputs(const QString &folder, const QMap<QString, QByteArray> &committed) {
    auto names =
        QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    auto expected = committed.keys();
    std::sort(expected.begin(), expected.end());
    require(names == expected, "Resize queue created missing, unexpected or temporary outputs");
    for (auto it = committed.cbegin(); it != committed.cend(); ++it)
        require(digest(folder + '/' + it.key()) == it.value(),
                "A later resize changed a committed PNG");
}
void proveHistory(Context &c, const Baseline &before, const QImage &preview) {
    auto second = before.document;
    second.sharpenAmount = 2;
    const luma::EditDocument empty;
    const std::vector<luma::EditDocument> expected{second, before.document, empty, before.document,
                                                   second, before.document};
    const QStringList inputs{"main.redoButton", "main.undoButton", "main.undoButton",
                             "main.redoButton", "main.redoButton", "main.undoButton"};
    for (int index = 0; index < inputs.size(); ++index) {
        c.driver->click(inputs[index]);
        require(c.editor.currentSettings() == expected[static_cast<std::size_t>(index)],
                "Resize settings/export changed exact prepared Undo/Redo order");
        if (index == 2)
            require(!c.editor.canUndo() && c.editor.canRedo(),
                    "Resize created a spurious Undo entry");
        if (index == 4)
            require(c.editor.canUndo() && !c.editor.canRedo(),
                    "Resize created a spurious Redo entry");
    }
    naturalSave(c, before.document);
    readyPixels(c, preview);
    invariant(c, before);
}
void resizeScenario(Context &c, const QString &id) {
    const std::vector<Recipe> recipes{{"original", 0, 6, "37", 37, {160, 96}, {96, 160}},
                                      {"width", 1, 2, "61", 61, {61, 37}, {61, 102}},
                                      {"height", 2, 1, "61", 61, {102, 61}, {37, 61}},
                                      {"long", 3, 4, "91", 91, {91, 55}, {55, 91}},
                                      {"short", 4, 3, "43", 43, {72, 43}, {43, 72}},
                                      {"megapixels", 5, 1, "0.02", .02, {183, 110}, {110, 183}},
                                      {"percentage", 6, 1, "75", 75, {120, 72}, {72, 120}}};
    const KeyboardNavigation navigation;
    auto &d = *c.driver;
    const auto input = c.root + "/owned-input", folder = c.root + "/exports";
    require(QDir().mkpath(input) && QDir().mkpath(folder) &&
                QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(),
            "Resize scenario needs fresh owned input/output directories");
    const QStringList sources{input + "/A-Landscape.png", input + "/B-Portrait.png"};
    generatedSource(sources[0], {160, 96}, 0);
    generatedSource(sources[1], {96, 160}, 1);
    require(digest(sources[0]) != digest(sources[1]),
            "Resize orientation sources must be distinct");
    const QMap<QString, QByteArray> originalHashes{{sources[0], digest(sources[0])},
                                                   {sources[1], digest(sources[1])}};
    importOwned(c, sources);
    QMap<QString, QByteArray> committed;
    QJsonArray evidence;
    for (int orientation = 0; orientation < 2; ++orientation) {
        if (!c.window->property("libraryMode").toBool())
            d.click("main.libraryButton");
        metadataReady(c, sources);
        const auto id =
            c.library.data(c.library.index(orientation), LibraryModel::AssetID).toString();
        d.doubleClick(c.card(orientation));
        c.loaded();
        metadataReady(c, sources); // photoOpened resets/reloads the Library model.
        require(c.editor.photoID() == id && c.editor.sourcePath() == sources[orientation],
                "Resize scenario opened the wrong exact owned photo");
        const auto document = prepareSentinel(c);
        const auto before = baseline(c, originalHashes);
        require(before.document == document,
                "Resize baseline lost its independently typed sentinel");
        const auto preview = fixtureReference(c, document, before.source);
        readyPixels(c, preview);
        std::vector<QImage> references;
        references.reserve(recipes.size());
        for (const auto &recipe : recipes)
            references.push_back(expectedOutput(before.source, document,
                                                orientation ? recipe.portrait : recipe.landscape,
                                                recipe.mode == 0));
        for (std::size_t index = 0; index < recipes.size(); ++index) {
            const auto &recipe = recipes[index];
            const auto name = QString(orientation ? "portrait-" : "landscape-") + recipe.name;
            auto expectedForm = c.exporter.settings();
            expectedForm["destination"] = folder;
            expectedForm["naming"] = name;
            expectedForm["conflict"] = 0;
            expectedForm["format"] = 1;
            expectedForm["compression"] = 0;
            expectedForm["colorSpace"] = 0;
            expectedForm["resize"] = recipe.mode;
            expectedForm["resizeValue"] = recipe.scalar;
            expectedForm["outputMedium"] = 0;
            expectedForm["metadata"] = 0;
            expectedForm["watermarkKind"] = 0;
            d.click("exportButton");
            d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
                   "resize Export panel opened");
            text(c, "export.destination", folder);
            text(c, "exportNaming", name);
            choose(c, "export.conflict", 0);
            choose(c, "exportFormat", 1);
            choose(c, "export.colorSpace", 0);
            choose(c, "export.outputMedium", 0);
            choose(c, "export.metadata", 0);
            choose(c, "export.watermarkKind", 0);
            // Real conflicting unit/mode sequence: type under a different mode,
            // switch final mode, leave and return; never retype the scalar after it.
            choose(c, "export.resize", recipe.seedMode);
            text(c, "export.resizeValue", recipe.text);
            require(form(c).value("resize").toInt() == recipe.seedMode &&
                        form(c).value("resizeValue").toDouble() == recipe.scalar,
                    "Typed scalar did not commit under the real preceding resize mode");
            const int opposite = recipe.mode == 1 ? 2 : 1;
            for (const int mode : {recipe.mode, opposite, recipe.mode}) {
                choose(c, "export.resize", mode);
                require(form(c).value("resize").toInt() == mode &&
                            form(c).value("resizeValue").toDouble() == recipe.scalar,
                        "Mode switching silently changed or restored a stale resize scalar");
            }
            require(form(c) == expectedForm &&
                        d.object("export.resizeValue")->property("visible").toBool() ==
                            (recipe.mode != 0),
                    "Actual full resize form disagrees with the independently expected settings");
            invariant(c, before);
            const auto output = folder + '/' + name + ".png";
            require(!QFileInfo::exists(output),
                    "Resize output collision invalidates the candidate");
            QSignalSpy clicked(d.object("export.current"), SIGNAL(clicked()));
            QSignalSpy closed(d.object("exportPanel"), SIGNAL(closed()));
            require(clicked.isValid() && closed.isValid(),
                    "Resize export lacks real observable UI signals");
            bool sawWorking = false, previousWorking = false;
            int idleTransitions = 0;
            QObject observerLifetime; // Disconnect before captured counters are destroyed.
            QObject::connect(&c.exporter, &ExportController::changed, &observerLifetime, [&] {
                const bool working = c.exporter.working();
                sawWorking = sawWorking || working;
                if (previousWorking && !working)
                    ++idleTransitions;
                previousWorking = working;
            });
            d.click("export.current");
            d.wait([&] { return !c.exporter.working() && c.exporter.completed() == 1; },
                   "resize exact single real queue item completed", 60000);
            d.wait(
                [&] {
                    return !d.object("exportPanel")->property("visible").toBool() &&
                           closed.count() == 1;
                },
                "resize Export exit transition completed before later input");
            require(clicked.count() == 1 && sawWorking && idleTransitions == 1 &&
                        c.exporter.total() == 1 && c.exporter.succeeded() == 1 &&
                        c.exporter.skipped() == 0 && c.exporter.progress() == 1 &&
                        c.exporter.currentFile() == name + ".png" &&
                        c.exporter.failures().isEmpty() && c.exporter.error().isEmpty() &&
                        c.exporter.settings() == expectedForm,
                    "Resize failed to execute exactly one immutable configured PNG item");
            const int maxDelta = comparePixels(output, references[index]);
            committed.insert(name + ".png", digest(output));
            checkOutputs(folder, committed);
            naturalSave(c, before.document);
            metadataReady(c, sources);
            readyPixels(c, preview);
            invariant(c, before);
            const auto store =
                QJsonDocument::fromJson(bytes(c.stateRoot + "/export-settings.json")).object();
            require(
                store.value("schema").toInt() == 1 &&
                    store.value("last").toObject() == QJsonObject::fromVariantMap(expectedForm) &&
                    store.value("profiles").isObject() &&
                    store.value("profiles").toObject().isEmpty() && c.exporter.profiles().isEmpty(),
                "Resize export did not persist its exact last settings or mutated profiles");
            const auto dimensions = orientation ? recipe.portrait : recipe.landscape;
            const QJsonObject result{
                {"orientation", orientation ? "portrait" : "landscape"},
                {"mode", recipe.mode},
                {"preceding_mode", recipe.seedMode},
                {"scalar", recipe.scalar},
                {"width", dimensions.width()},
                {"height", dimensions.height()},
                {"output", output},
                {"sha256", QString::fromLatin1(committed[name + ".png"].toHex())},
                {"maximum_rgb_delta", maxDelta},
                {"real_export_clicks", clicked.count()},
                {"real_closed_signals", closed.count()}};
            evidence.append(result);
            d.log(
                QJsonObject{{"action", "candidate_resize_mode_scalar_output"}, {"result", result}});
        }
        proveHistory(c, before, preview);
        checkOutputs(folder, committed);
    }
    require(committed.size() == 14 && evidence.size() == 14,
            "Resize scenario lost a mode/orientation");
    c.checkData();
    writeJson(
        c.root + "/measurements.json",
        {{"scenario", id},
         {"input_contract", "one mutually exclusive resize mode and retained scalar"},
         {"outputs", 14},
         {"actual_modes", 7},
         {"orientations", 2},
         {"scalar_retyped_after_final_mode_switch", false},
         {"derived_size_ui_label_claimed", false},
         {"simultaneous_axis_fields_claimed", false},
         {"original_source_sha_guard", true},
         {"full_document_history_catalog_guard", true},
         {"pixel_oracle",
          "literal dimensions + direct 2D area/bilinear integration; shared CPU develop and ICC"},
         {"rgb_tolerance_8bit", 1},
         {"evidence", evidence}});
}
} // namespace
bool exportResizeScenario(Context &c, const QString &id) {
    if (id != "UX-EXP-026")
        return false;
    resizeScenario(c, id);
    return true;
}
} // namespace ux
