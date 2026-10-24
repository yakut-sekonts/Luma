#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QQmlContext>
#include <QQmlProperty>
#include <QSet>
#include <QSignalSpy>
#include <QStyleHints>
#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <memory>
#include <sqlite3.h>

namespace ux {
namespace {
QByteArray read(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read a visual fixture: " + path);
    return file.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    require(!QFileInfo::exists(path) && file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                file.write(bytes) == bytes.size(),
            "Cannot write a new owned visual fixture: " + path);
}
QString sha256(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QRectF box(QQuickItem *item) {
    if (!item || !item->isVisible() || item->width() <= 0 || item->height() <= 0)
        throw std::runtime_error("Visual geometry oracle requires a nonempty visible item");
    const auto result = item->mapRectToScene(item->boundingRect());
    require(std::isfinite(result.x()) && std::isfinite(result.y()) &&
                std::isfinite(result.width()) && std::isfinite(result.height()),
            "Visual geometry is not finite");
    return result;
}
void onScreen(Context &c, const QString &id, bool enabled = true) {
    auto *item = c.driver->item(id);
    require((!enabled || item->isEnabled()) && QRectF(0, 0, c.window->width(), c.window->height())
                                                   .adjusted(-.5, -.5, .5, .5)
                                                   .contains(box(item)),
            "Visual action is unavailable or outside the window: " + id);
}
void toolbar(Context &c) {
    const QStringList ids{"main.libraryButton", "main.developButton", "settingsButton",
                          "updates.bell",       "main.undoButton",    "main.redoButton",
                          "exportButton",       "openPhotoButton"};
    for (const auto &id : ids)
        onScreen(c, id, false);
    for (int first = 0; first < ids.size(); ++first)
        for (int second = first + 1; second < ids.size(); ++second)
            require(!box(c.driver->item(ids[first])).intersects(box(c.driver->item(ids[second]))),
                    "Visual toolbar actions overlap");
}
void importStart(Context &c, const QList<QUrl> &sources) {
    auto &d = *c.driver;
    // Explicit native file-chooser RESULT fixture boundary, exactly as Context::import.
    // The import acceptance action is the actual production Import button.
    d.object("library.importDialog")->setProperty("sources", QVariant::fromValue(sources));
    d.log({{"action", "fixture_file_chooser_result"},
           {"source_count", static_cast<qint64>(sources.size())}});
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "visual Import dialog opened");
    d.choose("library.importMode", 0);
    if (d.object("library.importDuplicates")->property("checked").toBool())
        d.click("library.importDuplicates");
}
void importComplete(Context &c, int expected) {
    auto &d = *c.driver;
    d.click("library.importStart");
    d.wait([&] { return !c.library.importing() && !c.library.working() && !c.library.loading(); },
           "visual Import terminal state", 60000);
    require(c.library.error().isEmpty() && c.library.count() == expected &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == expected,
            "Visual fixture Import has incorrect error/cardinality state");
}
void pixels(Context &c, const QImage &expected) {
    c.driver->wait(
        [&] {
            auto *preview = c.driver->item("develop.preview");
            return preview->isVisible() && preview->property("status").toInt() == 1 &&
                   preview->property("source").toUrl() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == expected;
        },
        "visual document has its exact Ready preview pixels");
}
void metadataReady(Context &c, int expected = 10) {
    c.driver->wait(
        [&] {
            return !c.library.loading() && c.library.count() == expected &&
                   !c.library.data(c.library.index(0), LibraryModel::AssetID)
                        .toString()
                        .isEmpty() &&
                   !c.library.data(c.library.index(0), LibraryModel::Path).toUrl().isEmpty();
        },
        "visual baseline waits for the post-photoOpened Library metadata");
    c.driver->frame();
}
struct Snapshot {
    QString source, asset;
    luma::EditDocument document;
    std::vector<std::string> members, selected;
    bool undo, redo;
};
Snapshot snapshot(Context &c) {
    return {c.editor.sourcePath(),      c.editor.photoID(),
            c.editor.currentSettings(), luma::Catalog(c.catalogFile.toStdString()).libraryIDs({}),
            c.library.selectionIDs(),   c.editor.canUndo(),
            c.editor.canRedo()};
}
void unchanged(Context &c, const Snapshot &expected) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    require(c.editor.sourcePath() == expected.source && c.editor.photoID() == expected.asset &&
                c.editor.currentSettings() == expected.document &&
                catalog.libraryIDs({}) == expected.members &&
                c.library.selectionIDs() == expected.selected &&
                c.editor.canUndo() == expected.undo && c.editor.canRedo() == expected.redo,
            "Visual interaction changed photo identity, full edits, history or membership");
    if (!expected.asset.isEmpty()) {
        const auto durable = catalog.assetByID(expected.asset.toStdString());
        require(durable && durable->edits == expected.document,
                "Visual interaction changed the full durable document");
    }
}
QQuickItem *flickable(Context &c, const QString &scrollID) {
    auto *result = qobject_cast<QQuickItem *>(
        qvariant_cast<QObject *>(c.driver->object(scrollID)->property("contentItem")));
    require(result && result->property("contentY").isValid() &&
                result->property("contentHeight").isValid() && result->property("moving").isValid(),
            "Visual scroll oracle was given a non-Flickable content item");
    return result;
}
double maximumY(QQuickItem &item) {
    const double content = item.property("contentHeight").toDouble();
    require(std::isfinite(content) && content >= 0 && item.height() > 0,
            "Visual scroll range is invalid");
    return item.property("originY").toDouble() + std::max(0.0, content - item.height());
}
void wheelEnd(Context &c, const QString &id, bool end = true) {
    auto &d = *c.driver;
    auto *view = flickable(c, id);
    for (int step = 0; step < 200; ++step) {
        d.wait([&] { return !view->property("moving").toBool(); }, "visual wheel settled");
        d.frame();
        const double target = end ? maximumY(*view) : view->property("originY").toDouble();
        if (std::abs(view->property("contentY").toDouble() - target) <= 1)
            return;
        d.wheel(id, end ? -480 : 480);
    }
    throw std::runtime_error("Visual discrete wheel input never reached the scroll endpoint");
}
void textEndpoint(Context &c, const QString &scrollID, const QString &textID, int position) {
    auto *text = c.driver->item(textID);
    QRectF caret;
    require(QMetaObject::invokeMethod(text, "positionToRectangle", Q_RETURN_ARG(QRectF, caret),
                                      Q_ARG(int, position)),
            "Actual TextArea does not expose its read-only text-position geometry");
    const auto mapped = text->mapRectToItem(flickable(c, scrollID), caret);
    auto *viewport = flickable(c, scrollID);
    require(mapped.top() >= -.5 && mapped.bottom() <= viewport->height() + .5 &&
                mapped.left() >= -.5 && mapped.left() <= viewport->width() + .5,
            "The claimed first/last report text is not inside the actual scroll viewport");
}
void revealTextPosition(Context &c, const QString &scrollID, const QString &textID, int position) {
    auto &d = *c.driver;
    auto *text = d.item(textID);
    auto *view = flickable(c, scrollID);
    for (int step = 0; step < 400; ++step) {
        d.wait([&] { return !view->property("moving").toBool(); },
               "report target-line wheel settled");
        d.frame();
        QRectF caret;
        require(QMetaObject::invokeMethod(text, "positionToRectangle", Q_RETURN_ARG(QRectF, caret),
                                          Q_ARG(int, position)),
                "Actual report cannot resolve its requested text-position geometry");
        const auto mapped = text->mapRectToItem(view, caret);
        if (mapped.top() >= -.5 && mapped.bottom() <= view->height() + .5) {
            textEndpoint(c, scrollID, textID, position);
            return;
        }
        d.wheel(scrollID, mapped.top() < 0 ? 120 : -120);
    }
    throw std::runtime_error("Actual report input never exposed the requested logical line");
}
void reportScroll(Context &c, const QString &scrollID, const QString &textID, const QString &text) {
    auto &d = *c.driver;
    d.frame();
    auto *view = flickable(c, scrollID);
    require(d.object(textID)->property("text").toString() == text &&
                maximumY(*view) > view->property("originY").toDouble() + 40,
            "Long report did not reach the actual TextArea or never overflowed");
    wheelEnd(c, scrollID, false);
    textEndpoint(c, scrollID, textID, 0);
    int hundredth = -1;
    for (int line = 1; line < 100; ++line) {
        hundredth = static_cast<int>(text.indexOf('\n', hundredth + 1));
        require(hundredth >= 0, "Long report has fewer than the required 100 logical lines");
    }
    ++hundredth;
    revealTextPosition(c, scrollID, textID, hundredth);
    d.screenshot(c.root + "/report-logical-line-100.png");
    wheelEnd(c, scrollID, true);
    textEndpoint(c, scrollID, textID, static_cast<int>(text.size() - 1));
    const double after = view->property("contentY").toDouble();
    require(after > view->property("originY").toDouble() + 40,
            "Report wheel sequence never changed the real scroll position");
    d.screenshot(c.root + "/report-last-entry.png");
    wheelEnd(c, scrollID, false);
    textEndpoint(c, scrollID, textID, 0);
    d.log({{"action", "actual_report_text_endpoints"},
           {"scroll", scrollID},
           {"logical_lines", static_cast<qint64>(text.count('\n') + 1)},
           {"logical_line_100_position", hundredth},
           {"maximum_content_y", after}});
}
void exportSettings(Context &c, const QString &destination) {
    auto &d = *c.driver;
    d.click("exportButton");
    d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
           "visual Export settings opened");
    d.type("export.destination", destination);
    d.type("exportNaming", "{filename}");
    d.choose("exportFormat", 1);
    d.choose("export.conflict", 0);
}
QString importSession(Context &c) {
    sqlite3 *raw = nullptr;
    const int opened =
        sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &raw, SQLITE_OPEN_READONLY, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    require(opened == SQLITE_OK, "Cannot read visual import journal");
    sqlite3_stmt *statement = nullptr;
    require(sqlite3_prepare_v2(database.get(), "SELECT id,state,finished_at FROM import_sessions",
                               -1, &statement, nullptr) == SQLITE_OK,
            "Cannot read visual import terminal session");
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> query(statement,
                                                                           sqlite3_finalize);
    require(sqlite3_step(query.get()) == SQLITE_ROW &&
                QString::fromUtf8(reinterpret_cast<const char *>(
                    sqlite3_column_text(query.get(), 1))) == "completed" &&
                sqlite3_column_type(query.get(), 2) != SQLITE_NULL,
            "Visual report lacks a completed durable import session");
    const auto id =
        QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(query.get(), 0)));
    require(!id.isEmpty() && sqlite3_step(query.get()) == SQLITE_DONE,
            "Visual report fixture has an ambiguous session identity");
    return id;
}
QString translation(const QString &language, const QString &source) {
    QFile file(":/luma/i18n/" + language + ".json");
    require(file.open(QIODevice::ReadOnly), "Cannot read the independent translation oracle");
    const auto messages = QJsonDocument::fromJson(file.readAll()).object()["messages"].toObject();
    require(messages.contains(source), "Localized visual fixture lacks a translation entry");
    return messages[source].toString();
}
void chooseLanguage(Context &c, int index) {
    auto &d = *c.driver;
    d.click("settingsButton");
    d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
           "visual language preferences opened");
    d.choose("languageChoice", index);
    d.key(Qt::Key_Escape);
    d.wait([&] { return !d.object("preferencesPanel")->property("visible").toBool(); },
           "visual language preferences closed");
    require(c.localization->error().isEmpty(), "Visual language change failed");
}
using Geometry = QMap<QString, QRectF>;
Geometry geometry(Context &c) {
    Geometry result;
    for (const auto *id :
         {"main.toolbar", "main.footer", "develop.panels", "develop.presetsPane",
          "develop.canvasPane", "develop.adjustmentsPane", "develop.preview", "develop.filmstrip"})
        result.insert(id, box(c.driver->item(id)));
    return result;
}
void sameGeometry(const Geometry &first, const Geometry &second) {
    require(first.keys() == second.keys(), "Top-level visual geometry membership changed");
    for (auto it = first.begin(); it != first.end(); ++it) {
        const auto other = second[it.key()];
        require(std::abs(it->x() - other.x()) <= .5 && std::abs(it->y() - other.y()) <= .5 &&
                    std::abs(it->width() - other.width()) <= .5 &&
                    std::abs(it->height() - other.height()) <= .5,
                "Status text moved top-level geometry: " + it.key());
    }
}
QRectF popupBox(Context &c, const QString &id) {
    auto *content = qobject_cast<QQuickItem *>(
        qvariant_cast<QObject *>(c.driver->object(id)->property("contentItem")));
    require(content && content->parentItem() && content->window() == c.window,
            "Popup fixture is not an in-window production Quick popup");
    return box(content->parentItem());
}
void focusByActualTab(Context &c, const QString &id) {
    struct RestoreTabBehavior {
        Qt::TabFocusBehavior previous = QGuiApplication::styleHints()->tabFocusBehavior();
        RestoreTabBehavior() {
            QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
        }
        ~RestoreTabBehavior() {
            QGuiApplication::styleHints()->setTabFocusBehavior(previous);
        }
    } restore;
    const auto document = c.editor.currentSettings();
    const auto photo = c.editor.photoID();
    const auto source = c.editor.sourcePath();
    const bool undo = c.editor.canUndo(), redo = c.editor.canRedo();
    auto *target = c.driver->item(id);
    // macOS AbstractButton uses Qt::TabFocus; a real pointer click need not focus it.
    // Query actual focus after each Tab, never predict a route through Qt focus proxies.
    for (int step = 0; step < 240 && !target->hasActiveFocus(); ++step) {
        c.driver->key(Qt::Key_Tab);
        auto *actual = c.window->activeFocusItem();
        c.driver->log({{"action", "visual_actual_tab_focus"},
                       {"target", id},
                       {"step", step},
                       {"actual", actual ? actual->objectName() : QString{}}});
        require(c.editor.currentSettings() == document && c.editor.photoID() == photo &&
                    c.editor.sourcePath() == source && c.editor.canUndo() == undo &&
                    c.editor.canRedo() == redo,
                "Actual Tab focus acquisition changed photo/document/history");
    }
    require(target->hasActiveFocus() && c.window->activeFocusItem() == target,
            "Actual bounded Tab input did not focus its target: " + id);
}
} // namespace

bool visualRemainingScenario(Context &c, const QString &id) {
    const QSet<QString> supported{"UX-VIS-001", "UX-VIS-002", "UX-VIS-004", "UX-VIS-005",
                                  "UX-VIS-006", "UX-VIS-009", "UX-VIS-010", "UX-VIS-011",
                                  "UX-VIS-012", "UX-REC-011"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    if (id == "UX-VIS-001" || id == "UX-VIS-002") {
        const bool longName = id == "UX-VIS-001";
        const auto name = longName ? QString(251, 'a') + ".png" : QString("long-path.png");
        QString directory = c.root + "/visual-owned";
        if (!longName) {
            const auto target = 1000 - 1 - name.size();
            require(directory.size() + 2 < target,
                    "Evidence root leaves no room for an exact 1000-character owned path");
            while (directory.size() < target) {
                const auto remaining = target - directory.size();
                auto length = std::min<qsizetype>(63, remaining - 1);
                if (remaining - length - 1 == 1)
                    --length;
                require(length > 0, "Cannot complete exact owned long-path fixture");
                directory += '/' + QString(length, 'p');
            }
        }
        require(QDir().mkpath(directory), "Cannot create private visual path directories");
        const auto source = directory + '/' + name;
        require(longName ? name.size() == 255 && name.toUtf8().size() == 255
                         : source.size() == 1000 && source.toUtf8().size() == 1000,
                "Visual character-count fixture does not meet its exact contract");
        const auto bytes = read(c.fixture + "/Photo-0000.png");
        write(source, bytes);
        importStart(c, {QUrl::fromLocalFile(source)});
        const auto importText = d.object("library.importSources")->property("text").toString();
        const auto separator = importText.indexOf(" · ");
        require(separator >= 0 && QUrl(importText.sliced(separator + 3)).toLocalFile() == source,
                "Long source path was lost or truncated in the actual Import summary");
        onScreen(c, "library.importSources");
        onScreen(c, "library.importStart");
        importComplete(c, 1);
        const auto card = c.card(0);
        const auto uuid = d.object(card)->property("assetID").toString();
        auto *caption = d.item("library.filename." + uuid);
        require(d.object(card)->property("filename").toString() == name &&
                    caption->property("text").toString() == name &&
                    box(d.item(card)).adjusted(-.5, -.5, .5, .5).contains(box(caption)),
                "Long filename is missing or clipped outside its Library card");
        if (longName)
            require(caption->property("truncated").toBool(),
                    "255-character Library caption never exercised actual elision");
        d.doubleClick(card);
        c.loaded();
        d.resize({1000, 640});
        d.frame();
        require(c.editor.sourcePath() == source && c.editor.filename() == name &&
                    c.editor.photoID() == uuid &&
                    d.object("main.filename")->property("text").toString() == name &&
                    d.object("develop.sourceFilename")->property("text").toString() == name,
                "Long visual source identity/text changed after Develop activation");
        toolbar(c);
        onScreen(c, "develop.sourceFilename");
        auto *label = d.item("develop.sourceFilename");
        require(label->property("contentHeight").toDouble() <= label->height() + .5 &&
                    !label->property("truncated").toBool(),
                "Source filename label lost wrapped text");
        if (longName)
            require(d.object("main.filename")->property("truncated").toBool() &&
                        label->property("lineCount").toInt() > 1,
                    "255-character filename did not exercise toolbar elision/source wrapping");
        if (longName) {
            auto *copy = d.item("preset.copy");
            auto *pane = d.item("develop.presetsPane");
            if (d.find("develop.presetsScroll")) {
                auto *view = flickable(c, "develop.presetsScroll");
                auto *bar = d.item("develop.presetsBar");
                require(view->property("contentHeight").toDouble() > view->height(),
                        "Long filename did not exercise the overflowing sidebar");
                d.log({{"action", "long_filename_sidebar_gutter"},
                       {"viewport_right", box(view).right()},
                       {"filename_right", box(label).right()},
                       {"scrollbar_left", box(bar).left()}});
                require(box(view).right() <= box(bar).left() + .5 &&
                            box(label).right() <= box(bar).left() + .5 &&
                            !box(label).intersected(box(view)).intersects(box(bar)),
                        "Sidebar scrollbar overlaps the full wrapped source filename");
            }
            const auto inside = [&] {
                QRectF viewport = box(pane);
                for (auto *ancestor = copy->parentItem(); ancestor;
                     ancestor = ancestor->parentItem())
                    if (ancestor->clip())
                        viewport = viewport.intersected(box(ancestor));
                return viewport.adjusted(-.5, -.5, .5, .5).contains(box(copy));
            };
            d.log({{"action", "long_filename_preset_reachability"},
                   {"copy_y", box(copy).y()},
                   {"copy_bottom", box(copy).bottom()},
                   {"pane_bottom", box(pane).bottom()},
                   {"initially_contained", inside()}});
            if (!inside()) {
                require(d.find("develop.presetsScroll"),
                        "Long filename pushes Copy outside the sidebar with no scrolling route");
                wheelEnd(c, "develop.presetsScroll");
            }
            require(inside(), "Actual sidebar scrolling did not expose the full Copy action");
            if (d.find("develop.presetsScroll"))
                require(box(copy).right() <= box(d.item("develop.presetsBar")).left() + .5,
                        "Sidebar scrollbar overlaps the reachable Copy action");
            QSignalSpy clicked(copy, SIGNAL(clicked()));
            d.click("preset.copy");
            d.wait([&] { return d.object("presetEditor")->property("opened").toBool(); },
                   "long filename Copy dialog opened through actual pointer input");
            require(clicked.count() == 1, "Long filename Copy hitbox did not receive one click");
            d.key(Qt::Key_Escape);
            d.wait([&] { return !d.object("presetEditor")->property("opened").toBool(); },
                   "long filename Copy dialog dismissed");
            if (d.find("develop.presetsScroll")) {
                wheelEnd(c, "develop.presetsScroll", false);
                // Closing Copy restores its focus. Manual scrolling deliberately does
                // not follow an unchanged focus item: leave it through actual Tab,
                // then traverse back to prove a fresh keyboard-focus transition.
                d.key(Qt::Key_Tab);
                require(!copy->hasActiveFocus(), "Tab did not leave the restored Copy focus");
                focusByActualTab(c, "preset.copy");
                d.wait(inside, "Tab exposes Copy inside the actual clipped sidebar viewport", 5000);
                d.frame();
                require(inside(), "Tab focused Copy outside the actual clipped sidebar viewport");
                d.key(Qt::Key_Space);
                d.wait([&] { return d.object("presetEditor")->property("opened").toBool(); },
                       "long filename keyboard Copy activation");
                d.key(Qt::Key_Escape);
                d.wait([&] { return !d.object("presetEditor")->property("opened").toBool(); },
                       "long filename keyboard Copy dismissed");
            }
            require(!c.presets.hasCopiedSettings() && !c.editor.canUndo() && !c.editor.canRedo(),
                    "Opening/dismissing Copy changed clipboard or history");
        }
        const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
        require(active && active->id == uuid.toStdString() &&
                    active->path == std::filesystem::path(source.toStdString()) &&
                    active->checksum == sha256(bytes).toStdString() &&
                    active->edits == luma::EditDocument{} && read(source) == bytes,
                "Long visual path lost exact catalog identity/checksum or changed source bytes");
        pixels(c, fixtureReference(c));
        d.screenshot(c.root + "/long-source-develop.png");
        return true;
    }
    if (id == "UX-VIS-004" || id == "UX-VIS-005") {
        const bool exportErrors = id == "UX-VIS-004";
        const auto directory = c.root + "/visual-errors";
        require(QDir().mkpath(directory), "Cannot create private visual error inputs");
        const auto original = read(c.fixture + "/Photo-0000.png");
        const QByteArray broken("Generated invalid image; visual report fixture\n");
        QList<QUrl> sources;
        QStringList paths;
        for (int index = 0; index < 100; ++index) {
            const auto path = directory + QString("/Report-%1.png").arg(index, 3, 10, QChar('0'));
            write(path, exportErrors ? original : broken);
            sources.append(QUrl::fromLocalFile(path));
            paths.append(path);
        }
        importStart(c, sources);
        importComplete(c, exportErrors ? 100 : 0);
        if (exportErrors) {
            const auto before = snapshot(c);
            const auto rows = luma::Catalog(c.catalogFile.toStdString()).libraryPage({}, 0, 101);
            require(rows.size() == 100, "Visual failure fixture did not import all 100 assets");
            for (const auto &path : paths)
                require(path.startsWith(directory + '/') && QFile::remove(path),
                        "Cannot remove only the owned visual export source");
            d.click("library.selectAll");
            d.wait([&] { return !c.library.selecting() && c.library.selectedCount() == 100; },
                   "100 visual export failures selected by actual UI input");
            const auto destination = c.root + "/exports";
            require(QDir().mkpath(destination), "Cannot create visual export destination");
            exportSettings(c, destination);
            d.click("export.selection");
            d.wait([&] { return !c.exporter.working() && c.exporter.completed() == 100; },
                   "100 real missing-source export failures completed", 60000);
            const auto failures = c.exporter.failures();
            require(c.exporter.total() == 100 && c.exporter.succeeded() == 0 &&
                        c.exporter.skipped() == 0 && failures.size() == 100 &&
                        c.exporter.error().isEmpty() &&
                        QDir(destination)
                            .entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)
                            .isEmpty(),
                    "Visual export fixture did not produce exactly 100 isolated per-file failures");
            QSet<QString> observed;
            for (const auto &failure : failures) {
                require(!failure.contains('\n'), "Visual export error is not one logical line");
                const auto name = failure.section(": ", 0, 0);
                require(!observed.contains(name) && paths.contains(directory + '/' + name) &&
                            failure.size() > name.size() + 2,
                        "Visual export report duplicated or lost a concrete source identity");
                observed.insert(name);
            }
            const auto text = failures.join('\n');
            require(text.count('\n') + 1 == 100, "Export failure list is not exactly 100 lines");
            reportScroll(c, "export.failuresScroll", "export.failures", text);
            for (const auto &path : paths)
                write(path, original); // Restore only our copies after the observed fault.
            const auto after = luma::Catalog(c.catalogFile.toStdString()).libraryPage({}, 0, 101);
            require(after.size() == rows.size() &&
                        luma::Catalog(c.catalogFile.toStdString()).libraryIDs({}) == before.members,
                    "100 visual export failures mutated catalog membership");
            for (const auto &row : after) {
                const auto prior = std::find_if(
                    rows.begin(), rows.end(), [&](const auto &item) { return item.id == row.id; });
                const auto asset = luma::Catalog(c.catalogFile.toStdString()).assetByID(row.id);
                require(prior != rows.end() && prior->path == row.path &&
                            prior->checksum == row.checksum && asset &&
                            asset->edits == luma::EditDocument{} &&
                            sha256(read(QString::fromStdString(row.path.string()))).toStdString() ==
                                row.checksum,
                        "Visual failures changed a source identity/checksum or its full document");
            }
        } else {
            const auto session = importSession(c);
            const luma::Catalog catalog(c.catalogFile.toStdString());
            const auto summary = catalog.importSummary(session.toStdString());
            const auto problems = catalog.importProblems(session.toStdString());
            require(summary.added == 0 && summary.duplicates == 0 && summary.failed == 100 &&
                        summary.cancelled == 0 && problems.size() == 100 &&
                        catalog.unfinishedImports().empty(),
                    "Visual Import report lacks exactly 100 durable failed items");
            QStringList ordered;
            QSet<QString> observed;
            for (const auto &problem : problems) {
                const auto text = QString::fromStdString(problem);
                const auto path = text.section('\n', 0, 0);
                require(paths.contains(path) && !observed.contains(path) &&
                            text.size() > path.size() + 1,
                        "Visual Import report lost a concrete failed path/error");
                observed.insert(path);
                ordered.append(text);
            }
            const auto report = ordered.join("\n\n");
            require(c.library.importReport() == report && report.count('\n') + 1 >= 100,
                    "Actual Import report does not preserve the full long journal text");
            d.click("library.importReportButton");
            d.wait(
                [&] { return d.object("library.importReportDialog")->property("opened").toBool(); },
                "100-entry visual Import report opened");
            reportScroll(c, "library.importReportScroll", "library.importReportText", report);
            d.key(Qt::Key_Escape);
            d.wait(
                [&] {
                    return !d.object("library.importReportDialog")->property("visible").toBool();
                },
                "long Import report closed without a leaked modal grab");
            onScreen(c, "library.importButton");
        }
        c.checkData();
        return true;
    }
    if (id == "UX-VIS-006") {
        c.openFirst();
        c.saved();
        metadataReady(c);
        d.resize({1000, 640});
        c.reveal("develop.exposureSlider");
        d.frame();
        const auto before = snapshot(c);
        const auto baseline = geometry(c);
        QSet<QString> statuses;
        std::exception_ptr failure;
        int frames = 0;
        bool sawPendingSave = false, sawSavedAfterPending = false;
        QObject observer; // Dies before every object captured below; no dangling callbacks.
        QObject::connect(&c.editor, &EditorController::stateChanged, &observer, [&] {
            if (c.editor.saving())
                sawPendingSave = true;
            else if (sawPendingSave)
                sawSavedAfterPending = true;
        });
        QObject::connect(c.window, &QQuickWindow::afterAnimating, &observer, [&] {
            try {
                sameGeometry(baseline, geometry(c));
                toolbar(c);
                statuses.insert(d.object("main.editorStatus")->property("text").toString());
                ++frames;
            } catch (...) {
                if (!failure)
                    failure = std::current_exception();
            }
        });
        for (int index = 0; index < 20; ++index) {
            const auto previous = c.editor.currentSettings();
            d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"),
                   {index % 2 ? .58 : .67, .5});
            const auto changed = c.editor.currentSettings();
            auto expected = previous;
            expected.exposure = changed.exposure;
            require(changed != previous && changed == expected,
                    "Visual status fixture missed a real isolated edit");
            d.frame();
            c.saved();
            d.frame();
            const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
            require(active && active->id == before.asset.toStdString() && active->edits == changed,
                    "Visual status update lost its exact durable document");
            if (failure)
                std::rethrow_exception(failure);
        }
        require(frames >= 20 && statuses.size() >= 2 && sawPendingSave && sawSavedAfterPending &&
                    c.editor.sourcePath() == before.source && c.editor.photoID() == before.asset &&
                    c.library.selectionIDs() == before.selected &&
                    luma::Catalog(c.catalogFile.toStdString()).libraryIDs({}) == before.members,
                "Visual status observer never saw distinct states or changed identity/selection");
        pixels(c, fixtureReference(c));
        if (failure)
            std::rethrow_exception(failure);
        QJsonArray texts;
        for (const auto &text : statuses)
            texts.append(text);
        writeJson(c.root + "/measurements.json",
                  {{"scenario", id},
                   {"observed_polishes", frames},
                   {"distinct_statuses", texts},
                   {"top_level_regions", static_cast<qint64>(baseline.size())}});
        return true;
    }
    if (id == "UX-VIS-009") {
        c.openFirst();
        c.reveal("curves.target");
        d.click("curves.target");
        require(c.editor.targetedTool() == "curve.rgb", "Popup input fixture has no target tool");
        const auto baseline = c.editor.currentSettings();
        const auto asset = c.editor.photoID();
        const auto source = c.editor.sourcePath();
        const auto expectedPixels = fixtureReference(c);
        const QList<QPair<QString, QString>> popups{{"settingsButton", "preferencesPanel"},
                                                    {"exportButton", "exportPanel"},
                                                    {"updates.bell", "updates.popup"}};
        for (int cycle = 0; cycle < 10; ++cycle) {
            for (const auto &[button, popup] : popups) {
                d.click(button);
                d.wait([&] { return d.object(popup)->property("opened").toBool(); },
                       "production popup opened before invisible-hitbox acceptance");
                const auto former = popupBox(c, popup);
                if (popup == "exportPanel")
                    d.click("export.close");
                else
                    d.key(Qt::Key_Escape);
                d.wait([&] { return !d.object(popup)->property("visible").toBool(); },
                       "production popup fully closed before underlying input");
                auto *preview = d.item("develop.preview");
                const double width = preview->property("paintedWidth").toDouble();
                const double height = preview->property("paintedHeight").toDouble();
                const QRectF painted(preview->mapToScene({(preview->width() - width) / 2,
                                                          (preview->height() - height) / 2}),
                                     QSizeF(width, height));
                const auto intersection = former.intersected(painted).adjusted(8, 8, -8, -8);
                require(intersection.width() > 40 && intersection.height() > 50,
                        "Popup positive control does not cover a usable underlying image region");
                auto *area = d.item("develop.targetedArea");
                const auto start = area->mapFromScene(intersection.center() + QPointF(0, 15));
                const auto finish = area->mapFromScene(intersection.center() - QPointF(0, 15));
                d.drag("develop.targetedArea",
                       {start.x() / area->width(), start.y() / area->height()},
                       {finish.x() / area->width(), finish.y() / area->height()});
                const auto changed = c.editor.currentSettings();
                auto expected = baseline;
                expected.curves = changed.curves;
                require(changed != baseline && changed == expected && c.editor.photoID() == asset &&
                            c.editor.sourcePath() == source &&
                            !area->property("adjusting").toBool(),
                        "Closed popup retained a hitbox/grab or misrouted its underlying gesture");
                c.saved();
                d.click("main.undoButton");
                require(c.editor.currentSettings() == baseline,
                        "Underlying post-popup gesture was not exactly one Undo");
                c.saved();
                pixels(c, expectedPixels);
                const auto durable = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
                require(durable && durable->id == asset.toStdString() &&
                            durable->edits == baseline && !c.exporter.working(),
                        "Popup close/underlying input damaged durable edits or started an export");
                d.log({{"action", "former_popup_region_accepts_real_gesture"},
                       {"popup", popup},
                       {"cycle", cycle + 1},
                       {"former_left", former.left()},
                       {"former_top", former.top()},
                       {"gesture_delta_pixels", 30}});
            }
        }
        return true;
    }
    if (id == "UX-VIS-010") {
        const auto empty = snapshot(c);
        QSignalSpy exportClicks(d.object("exportButton"), SIGNAL(clicked()));
        QSignalSpy undoClicks(d.object("main.undoButton"), SIGNAL(clicked()));
        QSignalSpy redoClicks(d.object("main.redoButton"), SIGNAL(clicked()));
        require(exportClicks.isValid() && undoClicks.isValid() && redoClicks.isValid(),
                "Disabled-action signals absent");
        for (int cycle = 0; cycle < 10; ++cycle) {
            for (const auto *button : {"exportButton", "main.undoButton", "main.redoButton"}) {
                require(!d.item(button)->isEnabled(), "Empty disabled-button fixture is enabled");
                // Driver::click intentionally rejects disabled targets. Press/release
                // still deliver real pointer events at the rendered disabled hit area.
                d.press(button);
                d.release(button);
            }
            d.key(Qt::Key_Z, Qt::ControlModifier);
            d.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
            d.key(Qt::Key_Return);
            d.key(Qt::Key_Enter);
            require(exportClicks.isEmpty() && undoClicks.isEmpty() && redoClicks.isEmpty() &&
                        !c.exporter.working() &&
                        !d.object("exportPanel")->property("visible").toBool(),
                    "Disabled empty action activated from actual pointer or keyboard input");
            unchanged(c, empty);
        }
        c.openFirst();
        c.reveal("curves.target");
        auto *target = d.item("curves.target");
        QSignalSpy clicks(target, SIGNAL(clicked()));
        require(clicks.isValid() && target->isEnabled(),
                "Target action positive control is absent");
        d.click("curves.target");
        require(c.editor.targetedTool() == "curve.rgb" && clicks.count() == 1,
                "Actual enabled pointer input did not activate the target button");
        focusByActualTab(c, "curves.target");
        require(c.window->activeFocusItem() == target,
                "Enabled keyboard positive control requires actual Tab-focused button");
        d.key(Qt::Key_Return);
        require(c.editor.targetedTool() == "none" && clicks.count() == 2,
                "Enabled Return shortcut positive control did not activate its focused button");
        d.key(Qt::Key_Enter);
        require(c.editor.targetedTool() == "curve.rgb" && clicks.count() == 3,
                "Enabled Enter shortcut positive control did not activate its focused button");
        const auto before = snapshot(c);
        d.key(Qt::Key_Backslash); // Disable the FOCUSED action without clicking another control.
        d.wait([&] { return c.editor.original() && !target->isEnabled(); },
               "Original mode disabled the formerly focused target action");
        for (int cycle = 0; cycle < 20; ++cycle) {
            d.press("curves.target");
            d.release("curves.target");
            d.key(Qt::Key_Return);
            d.key(Qt::Key_Enter);
            d.key(Qt::Key_Space);
            require(
                clicks.count() == 3 && c.editor.original() &&
                    c.editor.targetedTool() == "curve.rgb" && !target->isEnabled(),
                "Disabled formerly focused action activated through pointer/Return/Enter/Space");
            unchanged(c, before);
        }
        d.key(Qt::Key_Backslash);
        d.wait([&] { return !c.editor.original() && target->isEnabled(); },
               "Original release re-enabled the target action");
        d.click("curves.target");
        require(clicks.count() == 4 && c.editor.targetedTool() == "none",
                "Re-enabled pointer positive control did not activate exactly once");
        unchanged(c, before);
        pixels(c, fixtureReference(c));
        return true;
    }
    if (id == "UX-VIS-011") {
        c.openFirst();
        const auto before = snapshot(c);
        for (int cycle = 0; cycle < 10; ++cycle) {
            for (const auto &[button, popup] : QList<QPair<QString, QString>>{
                     {"exportButton", "exportPanel"}, {"updates.bell", "updates.popup"}}) {
                auto *control = d.item(button);
                const QQmlProperty attached(control, "ToolTip.toolTip", qmlContext(control));
                const QQmlProperty attachedVisible(control, "ToolTip.visible", qmlContext(control));
                require(attached.isValid() && attachedVisible.isValid(),
                        "Actual attached ToolTip public QML properties are unavailable");
                auto *tooltip = qvariant_cast<QObject *>(attached.read());
                require(tooltip && tooltip->inherits("QQuickToolTip"),
                        "Attached ToolTip fixture did not resolve the production popup");
                QSignalSpy clicks(control, SIGNAL(clicked()));
                require(clicks.isValid(), "Tooltip click positive-control signal is absent");
                d.move(button, {.5, .5});
                d.wait(
                    [&] {
                        return control->property("hovered").toBool() &&
                               attachedVisible.read().toBool() &&
                               tooltip->property("visible").toBool() &&
                               tooltip->property("opened").toBool() &&
                               !tooltip->property("text").toString().isEmpty();
                    },
                    "actual attached tooltip visibly opened before the acceptance click", 5000);
                require(!d.object(popup)->property("visible").toBool(),
                        "Tooltip positive control began with its target dialog already open");
                d.click(button); // Same hit area, while the actual tooltip is still visible.
                d.wait([&] { return d.object(popup)->property("opened").toBool(); },
                       "a visible tooltip did not block its subsequent actual button click");
                require(clicks.count() == 1, "Tooltip intercepted or duplicated the target click");
                if (popup == "exportPanel")
                    d.click("export.close");
                else
                    d.key(Qt::Key_Escape);
                d.wait([&] { return !d.object(popup)->property("visible").toBool(); },
                       "tooltip click target popup closed");
                d.move("develop.preview", {.5, .5});
                d.wait([&] { return !tooltip->property("visible").toBool(); },
                       "actual tooltip disappeared after pointer leave");
                unchanged(c, before);
                require(!c.exporter.working(),
                        "Tooltip click probe unexpectedly started an export");
            }
        }
        return true;
    }
    if (id == "UX-VIS-012") {
        c.openFirst();
        metadataReady(c);
        d.resize({1000, 640});
        c.reveal("localPanelToggle");
        auto *toggle = d.item("localPanelToggle");
        require(!toggle->property("checked").toBool(), "Dynamic relayout must start collapsed");
        auto *view = flickable(c, "develop.adjustmentsScroll");
        auto *bar = d.item("develop.adjustmentsBar");
        auto *thumb =
            qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(bar->property("contentItem")));
        require(thumb, "Dynamic relayout has no physical scrollbar thumb");
        const auto before = snapshot(c);
        auto check = [&] {
            d.wait([&] { return !view->property("moving").toBool(); }, "dynamic scrollbar settled");
            d.frame();
            const double content = view->property("contentHeight").toDouble();
            const double origin = view->property("originY").toDouble();
            const double offset = view->property("contentY").toDouble();
            require(content > view->height() && offset >= origin - 1 &&
                        offset <= maximumY(*view) + 1,
                    "Dynamic scrollbar range/offset became invalid");
            const double expectedSize = view->height() / content;
            const double expectedPosition = (offset - origin) / content;
            const double available = bar->property("availableHeight").toDouble();
            const double minimum = bar->property("minimumSize").toDouble();
            require(
                expectedSize > minimum + .001,
                "Dynamic scrollbar fixture cannot distinguish thumb ratio from minimum-size clamp");
            const auto top = thumb->mapToItem(bar, {0, 0}).y();
            const double expectedTop =
                bar->property("topPadding").toDouble() + available * expectedPosition;
            require(
                std::abs(bar->property("size").toDouble() - expectedSize) < .002 &&
                    std::abs(bar->property("position").toDouble() - expectedPosition) < .002 &&
                    std::abs(thumb->height() - available * expectedSize) <= 1 &&
                    std::abs(top - expectedTop) <= 1,
                "Dynamic content range did not update the actual scrollbar size/position/thumb");
            return content;
        };
        const double collapsed = check();
        focusByActualTab(c, "localPanelToggle"); // Acquire while visible, before deep wheel input.
        QSignalSpy toggledClicks(toggle, SIGNAL(clicked()));
        require(toggledClicks.isValid(), "Dynamic relayout toggle click signal is absent");
        for (int cycle = 0; cycle < 20; ++cycle) {
            // Each previous cycle returns to the Histogram at the top. Local
            // adjustments can then be clipped even when its scene point still
            // lies inside the window, over an unrelated Filmstrip card.
            c.reveal("localPanelToggle");
            const auto center = toggle->mapToScene({toggle->width() / 2, toggle->height() / 2});
            for (auto *ancestor = toggle->parentItem(); ancestor; ancestor = ancestor->parentItem())
                require(!ancestor->clip() ||
                            ancestor->boundingRect().contains(ancestor->mapFromScene(center)),
                        "Dynamic relayout click target is clipped by its scroll viewport");
            d.log({{"action", "dynamic_scrollbar_toggle_target"},
                   {"cycle", cycle + 1},
                   {"center_x", center.x()},
                   {"center_y", center.y()},
                   {"content_y", view->property("contentY").toDouble()}});
            d.click("localPanelToggle");
            require(toggledClicks.count() == cycle * 2 + 1,
                    "Dynamic relayout pointer did not activate the actual toggle once");
            d.wait(
                [&] {
                    return toggle->property("checked").toBool() &&
                           view->property("contentHeight").toDouble() > collapsed + 60;
                },
                "real Local panel expansion increased scroll content");
            const auto expanded = check();
            wheelEnd(c, "develop.adjustmentsScroll");
            check();
            require(c.window->activeFocusItem() == toggle,
                    "Discrete wheel input did not retain the actually focused toggle button");
            d.key(Qt::Key_Space); // Real focused keyboard action while scrolled to the old maximum.
            require(toggledClicks.count() == cycle * 2 + 2,
                    "Dynamic relayout keyboard did not activate the actual toggle once");
            d.wait(
                [&] {
                    return !toggle->property("checked").toBool() &&
                           std::abs(view->property("contentHeight").toDouble() - collapsed) <= 1;
                },
                "focused panel collapse reduced the existing scroll range");
            require(std::abs(check() - collapsed) <= 1,
                    "Dynamic panel collapse retained its expanded content size");
            require(view->property("contentY").toDouble() <= maximumY(*view) + 1,
                    "Shrinking content left the old out-of-range scroll offset");
            wheelEnd(c, "develop.adjustmentsScroll", false);
            unchanged(c, before);
            d.log({{"action", "dynamic_scrollbar_relayout"},
                   {"cycle", cycle + 1},
                   {"collapsed_content_height", collapsed},
                   {"expanded_content_height", expanded},
                   {"final_thumb_height", thumb->height()}});
        }
        return true;
    }
    if (id == "UX-REC-011") {
        c.openFirst();
        c.saved();
        d.resize({1000, 640});
        const auto before = snapshot(c);
        struct Reset {
            SqliteFault &fault;
            ~Reset() {
                fault.diskFull(false);
            }
        } reset{c.storageFault};
        c.storageFault.diskFull(true);
        c.reveal("develop.exposureSlider");
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
        const auto changed = c.editor.currentSettings();
        auto expected = before.document;
        expected.exposure = changed.exposure;
        require(changed == expected && changed != before.document && c.editor.saving(),
                "Localized banner fixture missed an isolated pending UI edit");
        d.wait([&] { return !c.editor.error().isEmpty() && c.storageFault.rejectedWrites() > 0; },
               "actual catalog SQLITE_FULL caused a recoverable save failure");
        d.quitShortcut(); // Production requestClose replaces raw SQLite text with a translated
                          // error.
        const QString source("The last edit is not saved. Retry saving before closing.");
        d.wait([&] { return c.window->isVisible() && c.editor.error() == source; },
               "real Quit preserved the unsaved window and exposed the localized error template");
        const auto languages = c.localization->languages();
        require(languages.size() == 8, "Expected eight localized error-banner fixtures");
        int longest = -1;
        qsizetype length = 0;
        for (int language = 0; language < languages.size(); ++language) {
            const auto code = languages[language].toMap()["code"].toString();
            chooseLanguage(c, language);
            const auto text = translation(code, source);
            auto *label = d.item("main.editorError");
            require(c.localization->language() == code &&
                        label->property("text").toString() == text && !text.isEmpty() &&
                        !label->property("truncated").toBool() &&
                        label->property("contentHeight").toDouble() <= label->height() + .5,
                    "Localized long error is missing, clipped or not actually retranslated");
            onScreen(c, "main.editorError");
            onScreen(c, "editor.retrySave");
            require(d.object("editor.retrySave")->property("text").toString() ==
                            translation(code, "Retry save") &&
                        !box(label).intersects(box(d.item("editor.retrySave"))) &&
                        c.editor.currentSettings() == changed && c.editor.saving(),
                    "Localized error displaced Retry or discarded the pending document");
            const auto durable = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
            require(durable && durable->id == before.asset.toStdString() &&
                        durable->edits == before.document,
                    "Localized failed save partially committed or lost the prior durable document");
            if (text.size() > length) {
                length = text.size();
                longest = language;
            }
        }
        require(longest >= 0 && length >= source.size() + 15,
                "Localized banner test never exercised a materially longer real translation");
        chooseLanguage(c, longest);
        d.screenshot(c.root + "/longest-localized-save-error.png");
        c.storageFault.diskFull(false);
        d.click("editor.retrySave");
        c.saved();
        d.frame();
        const auto durable = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
        require(
            durable && durable->id == before.asset.toStdString() && durable->edits == changed &&
                !d.item("main.editorError")->isVisible() && c.editor.error().isEmpty(),
            "Actual localized Retry did not persist the exact full document and clear its error");
        d.click("main.undoButton");
        require(c.editor.currentSettings() == before.document && !c.editor.canUndo(),
                "Localized retry added duplicate edit-history entries");
        d.click("main.redoButton");
        require(c.editor.currentSettings() == changed, "Localized retry damaged Redo");
        c.saved();
        pixels(c, fixtureReference(c));
        writeJson(c.root + "/measurements.json",
                  {{"scenario", id},
                   {"languages", 8},
                   {"longest_actual_translation_characters", static_cast<qint64>(length)},
                   {"minimum_window_width", c.window->width()},
                   {"fault_rejected_writes", c.storageFault.rejectedWrites()}});
        return true;
    }
    return false;
}
} // namespace ux
