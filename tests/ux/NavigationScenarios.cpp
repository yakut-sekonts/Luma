#include "tests/ux/UxContext.h"
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QMimeData>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

namespace ux {
namespace {
void unchanged(Context &c, const luma::EditDocument &document, const QString &source,
               qsizetype count, bool undo, bool redo) {
    require(c.editor.currentSettings() == document && c.editor.sourcePath() == source &&
                c.library.count() == count && c.editor.canUndo() == undo &&
                c.editor.canRedo() == redo && !c.editor.busy(),
            "Navigation changed photo, edits, history or catalog membership");
}
void edit(Context &c) {
    c.openFirst();
    c.reveal("develop.exposureSlider");
    c.driver->drag("develop.exposureSlider", c.driver->sliderThumb("develop.exposureSlider"),
                   {.65, .5});
    c.saved();
}
void drop(Context &c, const QString &path) {
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    const QPoint position(c.window->width() / 2, c.window->height() / 2);
    QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(c.window, &enter);
    require(enter.isAccepted(), "Drop fixture's drag-enter was not accepted");
    QDragMoveEvent move(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(c.window, &move);
    require(move.isAccepted(), "Drop fixture's drag-move was not accepted");
    QDropEvent event(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(c.window, &event);
    require(event.isAccepted(), "Single URL drop was not accepted");
    c.driver->log({{"action", "qt_window_url_drop"}, {"path", path}});
}
void exportState(Context &c, bool current, bool selection) {
    auto &d = *c.driver;
    require(d.item("exportButton")->isEnabled() == (current || selection),
            "Export toolbar availability is inconsistent");
    if (!current && !selection)
        return;
    d.click("exportButton");
    d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
           "export availability panel opened");
    require(d.item("export.current")->isEnabled() == current &&
                d.item("export.selection")->isEnabled() == selection,
            "Current/selection export actions have incorrect availability");
    d.click("export.close");
    d.wait([&] { return !d.object("exportPanel")->property("visible").toBool(); },
           "export panel closed without an export");
    require(!c.exporter.working(), "Availability probe unexpectedly started export");
}
} // namespace
bool navigationScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-NAV-005", "UX-NAV-006", "UX-NAV-007", "UX-NAV-010",
                                         "UX-NAV-011", "UX-NAV-012", "UX-NAV-014", "UX-NAV-015"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    if (id == "UX-NAV-005" || id == "UX-NAV-006") {
        // Positive control: the very same event stream must open a supported image once.
        QSignalSpy loaded(&c.editor, &EditorController::photoLoaded);
        drop(c, c.fixture + "/Photo-0000.png");
        c.loaded();
        d.wait([&] { return !c.library.loading() && c.library.count() == 1; },
               "drop added exactly one asset");
        require(loaded.count() == 1 &&
                    luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 1,
                "One supported drop duplicated asset activation or membership");
        if (id == "UX-NAV-005")
            return true;
        const auto source = c.editor.sourcePath();
        const auto document = c.editor.currentSettings();
        const auto path = c.root + "/unsupported.xyz";
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write("Not an image") == 12,
                "Cannot write unsupported drop fixture");
        file.close();
        drop(c, path);
        d.wait([&] { return !c.editor.busy() && !c.editor.error().isEmpty(); },
               "unsupported drop produced a visible error");
        require(d.item("main.editorError")->isVisible() && loaded.count() == 1 &&
                    c.editor.sourcePath() == source && c.editor.currentSettings() == document &&
                    luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 1,
                "Unsupported drop lost the active photo or silently mutated the catalog");
        // Recover through real input before the harness's final successful-save check.
        drop(c, source);
        c.loaded();
        require(loaded.count() == 2 && c.library.count() == 1,
                "Recovery from an unsupported drop duplicated the original asset");
        return true;
    }
    if (id == "UX-NAV-010") {
        exportState(c, false, false);
        c.import(10);
        d.click(c.card(0));
        require(c.library.selectedCount() == 1, "Single selection fixture failed");
        exportState(c, false, true);
        d.click("library.selectAll");
        d.wait([&] { return !c.library.selecting() && c.library.selectedCount() == 10; },
               "multiple selection fixture ready");
        exportState(c, false, true);
        d.doubleClick(c.card(0));
        c.loaded();
        exportState(c, true, c.library.selectedCount() > 0);
        require(c.exporter.status().isEmpty(), "Availability checks created an export job");
        return true;
    }
    if (id == "UX-NAV-007") {
        const auto name = QString(251, 'a') + ".png";
        require(name.toUtf8().size() == 255, "Long filename fixture is not 255 bytes");
        const auto path = c.root + '/' + name;
        QFile original(c.fixture + "/Photo-0000.png"), target(path);
        require(original.open(QIODevice::ReadOnly), "Cannot read long filename source");
        const auto bytes = original.readAll();
        require(!bytes.isEmpty() && target.open(QIODevice::WriteOnly) &&
                    target.write(bytes) == bytes.size(),
                "Cannot write long filename directly: " + target.errorString());
        target.close();
        drop(c, path);
        c.loaded();
        d.resize({1000, 640});
        auto *label = d.item("main.filename");
        require(label->property("text").toString() == name && label->property("truncated").toBool(),
                "Long filename was lost or not elided");
        double right = 0;
        for (const auto *control :
             {"settingsButton", "updates.bell", "main.filename", "main.undoButton",
              "main.redoButton", "exportButton", "openPhotoButton"}) {
            auto *item = d.item(control);
            const auto rectangle = item->mapRectToScene(item->boundingRect());
            require(item->isVisible() && rectangle.left() >= right &&
                        rectangle.right() <= c.window->width(),
                    "Long filename overlapped or displaced toolbar controls: " + QString(control));
            right = rectangle.right();
        }
        return true;
    }
    edit(c);
    const auto document = c.editor.currentSettings();
    const auto source = c.editor.sourcePath();
    const auto count = c.library.count();
    const auto undo = c.editor.canUndo(), redo = c.editor.canRedo();
    if (id == "UX-NAV-011" || id == "UX-NAV-014") {
        QSignalSpy opened(d.object("preferencesPanel"), SIGNAL(opened()));
        QSignalSpy closed(d.object("preferencesPanel"), SIGNAL(closed()));
        require(opened.isValid() && closed.isValid(), "Preferences popup signals unavailable");
        QSignalSpy showing(d.object("preferencesPanel"), SIGNAL(aboutToShow()));
        QSignalSpy hiding(d.object("preferencesPanel"), SIGNAL(aboutToHide()));
        require(showing.isValid() && hiding.isValid(), "Popup lifecycle signals unavailable");
        const int cycles = id == "UX-NAV-011" ? 50 : 25;
        for (int repeat = 0; repeat < cycles; ++repeat) {
            if (id == "UX-NAV-014") {
                // Eight immediate Qt pointer clicks per cycle, with no frame wait between them.
                const auto position = d.point("settingsButton");
                const auto beforeBurst = showing.count();
                for (int click = 0; click < 8; ++click) {
                    const auto beforeClick = showing.count();
                    QTest::mouseClick(c.window, Qt::LeftButton, Qt::NoModifier, position, 0);
                    require(showing.count() - beforeClick <= 1,
                            "One pointer click requested duplicate popup openings");
                }
                require(showing.count() > beforeBurst,
                        "Fast clicks never requested a Settings popup opening");
            } else
                d.click("settingsButton");
            d.frame();
            if (id == "UX-NAV-011") {
                d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
                       "preferences opened once");
                require(opened.count() == repeat + 1, "Settings click duplicated popup opening");
            }
            if (d.object("preferencesPanel")->property("visible").toBool())
                d.key(Qt::Key_Escape);
            d.wait([&] { return !d.object("preferencesPanel")->property("visible").toBool(); },
                   "preferences closed");
            require(showing.count() == hiding.count(), "Preferences leaked an unclosed popup");
            if (id == "UX-NAV-011")
                require(opened.count() == closed.count(), "Settings popup did not finish closing");
            d.log({{"action", "settings_popup_lifecycle"},
                   {"cycle", repeat},
                   {"show_requests", showing.count()},
                   {"hide_requests", hiding.count()},
                   {"opened", opened.count()},
                   {"closed", closed.count()}});
        }
        // A leaked overlay/focus grab would prevent a real click on the underlying toolbar.
        d.click("main.libraryButton");
        require(c.window->property("libraryMode").toBool(), "Popup leaked its input grab");
        d.click("main.developButton");
        require(!c.window->property("libraryMode").toBool(), "Navigation lost input after popup");
    } else if (id == "UX-NAV-012") {
        for (int repeat = 0; repeat < 20; ++repeat) {
            d.click("updates.bell");
            d.wait([&] { return d.object("updates.popup")->property("opened").toBool(); },
                   "update notification opened");
            d.log({{"action", "update_popup_focus"},
                   {"focus", d.object("updates.popup")->property("focus").toBool()},
                   {"active_focus", d.object("updates.popup")->property("activeFocus").toBool()},
                   {"focused_item", c.window->activeFocusItem()
                                        ? c.window->activeFocusItem()->objectName()
                                        : QString{}}});
            d.key(Qt::Key_Escape);
            d.wait([&] { return !d.object("updates.popup")->property("visible").toBool(); },
                   "update notification dismissed", 3000);
            unchanged(c, document, source, count, undo, redo);
        }
    } else {
        require(id == "UX-NAV-015", "Navigation scenario has no adapter");
        const auto height = d.item("main.footer")->height();
        const auto y = d.item("main.footer")->y();
        QSet<QString> statuses;
        for (int repeat = 0; repeat < 10; ++repeat) {
            c.reveal("develop.exposureSlider");
            d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"),
                   {repeat % 2 ? .65 : .75, .5});
            statuses.insert(d.object("main.editorStatus")->property("text").toString());
            d.frame();
            require(d.item("main.footer")->height() == height && d.item("main.footer")->y() == y,
                    "Saving status changed footer geometry");
            c.saved();
            d.frame();
            statuses.insert(d.object("main.editorStatus")->property("text").toString());
            require(d.item("main.footer")->height() == height && d.item("main.footer")->y() == y,
                    "Saved status changed footer geometry");
        }
        require(statuses.size() >= 2, "Footer fixture never observed distinct status text");
        return true;
    }
    unchanged(c, document, source, count, undo, redo);
    require(luma::Catalog(c.catalogFile.toStdString()).activeAsset().value().edits == document,
            "Navigation altered durable edits");
    return true;
}
} // namespace ux
