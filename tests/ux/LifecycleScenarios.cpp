#include "tests/ux/UxContext.h"
#include "tests/ux/UxTestIds.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
namespace ux {
namespace {
void closed(Context &c) {
    c.driver->wait(
        [&] {
            return !c.window->isVisible() && !c.editor.saving() && !c.library.importing() &&
                   !c.batch.working() && !c.exporter.working();
        },
        "clean coordinated shutdown");
    c.checkData();
}
} // namespace
bool lifecycleScenario(Context &c, const QString &id) {
    auto &d = *c.driver;
    if (id == "UX-KEY-001") {
        auto *dialog = d.object("main.openDialog");
        // Restrict the native chooser to generated public files; no selection is made.
        dialog->setProperty("currentFolder", QUrl::fromLocalFile(c.fixture));
        for (int mode = 0; mode < 2; ++mode) {
            if (mode == 1)
                c.openFirst();
            const auto before = c.editor.currentSettings();
            const auto source = c.editor.sourcePath();
            require(!dialog->property("visible").toBool(), "Open dialog was already visible");
            d.key(Qt::Key_O, Qt::ControlModifier);
            d.wait([&] { return dialog->property("visible").toBool(); },
                   "Cmd+O opened the photo chooser");
            QJsonArray windows;
            for (const auto *window : QGuiApplication::allWindows())
                if (window->isVisible())
                    windows.append(QJsonObject{{"class", window->metaObject()->className()},
                                               {"title", window->title()},
                                               {"modal", static_cast<int>(window->modality())}});
            d.log({{"action", "open_shortcut_dialog_visible"},
                   {"mode", mode == 0 ? "empty_library" : "develop"},
                   {"visible_windows", windows}});
            // Cleanup only: this scenario qualifies opening, not native file selection/cancel
            // input.
            d.log({{"action", "fixture_cleanup_dialog_reject"}});
            require(QMetaObject::invokeMethod(dialog, "reject"), "Cannot clean up native chooser");
            d.wait([&] { return !dialog->property("visible").toBool(); },
                   "native chooser cleanup complete");
            c.window->requestActivate();
            d.wait([&] { return c.window->isActive(); }, "application focus restored");
            require(c.editor.currentSettings() == before && c.editor.sourcePath() == source,
                    "Opening the chooser changed active edits");
        }
        return true;
    }

    if (id == "UX-UND-011" || id == "UX-UND-012") {
        c.openFirst();
        c.reveal(ids::exposure);
        d.drag(ids::exposure, {.5, .5}, {.65, .5});
        const auto firstPath = c.editor.sourcePath();
        const auto first = c.editor.currentSettings();
        c.saved();
        const auto secondPath =
            c.library.data(c.library.index(1), LibraryModel::Path).toUrl().toLocalFile();
        require(!secondPath.isEmpty() && secondPath != firstPath, "Missing second photo fixture");
        d.click(c.card(1, true));
        d.wait([&] { return c.editor.sourcePath() == secondPath && !c.editor.busy(); },
               "second asset loaded");
        c.loaded();
        require(!c.editor.canUndo() && !c.editor.canRedo() &&
                    c.editor.currentSettings() == luma::EditDocument{},
                "Previous asset history/settings leaked into second photo");
        c.reveal(ids::exposure);
        d.drag(ids::exposure, {.5, .5}, {.35, .5});
        d.click(ids::undo);
        require(c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo(),
                "Second asset Undo crossed its boundary");
        c.saved();
        d.click(c.card(0, true));
        d.wait([&] { return c.editor.sourcePath() == firstPath && !c.editor.busy(); },
               "first asset reloaded");
        c.loaded();
        require(c.editor.currentSettings() == first && !c.editor.canUndo() && !c.editor.canRedo(),
                "A-B-A switch lost saved edits or retained another session's history");
        return true;
    }
    if (id == "UX-APP-006") {
        d.quitShortcut();
        closed(c);
        return true;
    }
    if (id == "UX-APP-007") {
        c.openFirst();
        c.reveal(ids::exposure);
        d.drag(ids::exposure, {.5, .5}, {.65, .5});
        const auto edited = c.editor.currentSettings();
        require(c.editor.saving(), "Autosave-close fixture missed pending save window");
        d.quitShortcut();
        closed(c);
        const auto saved = luma::Catalog((c.root + "/catalog.sqlite").toStdString()).activeAsset();
        require(saved && saved->edits == edited, "Quit during autosave lost final edit document");
        return true;
    }
    if (id == "UX-NAV-013") {
        c.openFirst();
        const auto large = c.fixture + "/stress/Large.png";
        require(QFile::exists(large), "Missing generated large decode fixture");
        c.editor.openFile(QUrl::fromLocalFile(large));
        require(c.editor.busy() && !c.editor.original(),
                "Busy Original fixture did not start loading");
        d.key(Qt::Key_Backslash);
        require(c.editor.busy(), "Large decode finished before shortcut assertion");
        require(!c.editor.original(), "Original shortcut changed hidden state during decode");
        c.loaded();
        require(c.metrics.json()["main_loop_stall_max_ms"].toDouble() < 2000,
                "P1 UI stall while decoding 24 MP");
        return true;
    }
    if (id == "UX-PRV-013" || id == "UX-PRV-006") {
        c.import(1);
        d.doubleClick(c.card(0));
        c.loaded();
        const auto previous = c.provider->requestImage({}, nullptr, {});
        require(!previous.isNull(), "Missing initial preview fixture");
        const auto large = c.fixture + "/stress/Large.png";
        c.library.importFiles({QUrl::fromLocalFile(large)}, false, {}, false, false);
        d.wait(
            [&] { return !c.library.working() && !c.library.loading() && c.library.count() == 2; },
            "large asset import");
        int row = -1;
        d.wait(
            [&] {
                for (int i = 0; i < c.library.count(); ++i)
                    if (c.library.data(c.library.index(i), LibraryModel::Path)
                            .toUrl()
                            .toLocalFile() == large)
                        row = i;
                return row >= 0;
            },
            "large asset filmstrip row");
        auto *preview = d.item("develop.preview");
        bool mismatch = false, currentFrame = false;
        QObject observer;
        QObject::connect(c.window, &QQuickWindow::afterAnimating, &observer, [&] {
            if (c.editor.sourcePath() != large || c.editor.busy())
                return;
            const auto displayed = c.provider->requestImage({}, nullptr, {});
            const bool ready = preview->isVisible() &&
                               !preview->property("source").toUrl().isEmpty() &&
                               preview->property("status").toInt() == 1;
            mismatch = mismatch || (ready && displayed == previous);
            currentFrame = ready && !displayed.isNull() && displayed != previous;
        });
        d.click(c.card(row, true));
        if (id == "UX-PRV-006") {
            require(c.editor.busy(), "Cancel fixture missed decode window");
            require(d.item("editor.cancelLoad")->isVisible(), "Decode Cancel is not visible");
            d.click("editor.cancelLoad");
            c.loaded();
            require(c.editor.sourcePath() != large &&
                        c.provider->requestImage({}, nullptr, {}) == previous,
                    "Cancel replaced the previous photo/preview");
            require(c.editor.error().isEmpty(), "User cancellation became an error");
            return true;
        }
        d.wait([&] { return mismatch || currentFrame; }, "matching asset and preview frame");
        require(!mismatch, "Visible preview of asset A was paired with metadata of asset B");
        require(currentFrame, "New asset never received its own preview");
        return true;
    }
    if (id == "UX-NAV-008" || id == "UX-NAV-009" || id == "UX-KEY-002") {
        c.openFirst();
        c.reveal(ids::exposure);
        const auto before = c.editor.currentSettings();
        require(!c.editor.canUndo() && !d.item(ids::undo)->isEnabled(),
                "Undo enabled on empty history");
        d.drag(ids::exposure, {.5, .5}, {.65, .5});
        const auto changed = c.editor.currentSettings();
        require(c.editor.canUndo() && d.item(ids::undo)->isEnabled() &&
                    !d.item(ids::redo)->isEnabled(),
                "History toolbar enabled states are stale");
        d.key(Qt::Key_Z, Qt::ControlModifier);
        require(c.editor.currentSettings() == before && c.editor.canRedo(),
                "Undo shortcut differs from toolbar");
        d.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        require(c.editor.currentSettings() == changed, "Redo shortcut differs from toolbar");
        d.click(ids::undo);
        require(c.editor.currentSettings() == before, "Toolbar Undo differs from shortcut");
        return true;
    }
    if (id == "UX-SLD-007" || id == "UX-SLD-008" || id == "UX-UND-005") {
        c.openFirst();
        c.reveal(ids::exposure);
        d.drag(ids::exposure, {.5, .5}, {.65, .5});
        const auto before = c.editor.currentSettings();
        c.reveal("develop.exposure.enabled");
        d.click("develop.exposure.enabled");
        const auto disabled = c.editor.currentSettings();
        require(!disabled.enabled(luma::Correction::exposure) &&
                    disabled.exposure == before.exposure,
                "Disable discarded the retained Exposure value");
        d.click(ids::undo);
        require(c.editor.currentSettings() == before, "Correction toggle Undo lost settings");
        d.click(ids::redo);
        require(c.editor.currentSettings() == disabled, "Correction toggle Redo lost settings");
        d.click("develop.exposure.enabled");
        require(c.editor.currentSettings() == before,
                "Re-enable failed to restore correction effect/settings");
        return true;
    }
    if (id == "UX-UND-004") {
        c.openFirst();
        c.reveal(ids::exposure);
        d.click(ids::exposure);
        const auto before = c.editor.currentSettings();
        d.drag(ids::exposure, {.5, .5}, {1, .5});
        require(c.editor.currentSettings().exposure == 5, "Drag did not reach slider maximum");
        d.key(Qt::Key_Right);
        d.click(ids::undo);
        require(c.editor.currentSettings() == before && !c.editor.canUndo(),
                "No-op key input polluted history");
        return true;
    }
    if (id == "UX-UND-001" || id == "UX-UND-002" || id == "UX-UND-003" || id == "UX-UND-010" ||
        id == "UX-SLD-019") {
        c.openFirst();
        std::vector<luma::EditDocument> documents{c.editor.currentSettings()};
        const QStringList names{"exposure", "contrast", "highlights", "shadows",
                                "whites",   "blacks",   "vibrance",   "saturation"};
        for (int i = 0; i < 50; ++i) {
            const auto &name = names[i % names.size()];
            const auto selector = "develop." + name + "Slider";
            c.reveal(selector);
            const auto before = c.editor.currentSettings();
            const double target = (i % 3 == 0 ? .35 : i % 3 == 1 ? .65 : .8);
            auto *slider = d.object(selector);
            const double position = slider->property("position").toDouble();
            d.drag(selector, {position, .5}, {target, .5});
            const auto after = c.editor.currentSettings();
            require(after != before, "Heterogeneous Undo fixture generated a no-op");
            documents.push_back(after);
        }
        const auto final = documents.back();
        if (id == "UX-UND-010" || id == "UX-SLD-019") {
            const auto selection = c.library.selectionIDs();
            c.reveal("develop.resetButton");
            d.click("develop.resetButton");
            require(c.editor.currentSettings() == luma::EditDocument{} &&
                        c.library.selectionIDs() == selection,
                    "Reset changed metadata/selection or did not reset edits");
            d.click(ids::undo);
            require(c.editor.currentSettings() == final, "Reset Undo lost complete prior document");
            return true;
        }
        for (int i = 49; i >= 0; --i) {
            d.click(ids::undo);
            require(c.editor.currentSettings() == documents[static_cast<std::size_t>(i)],
                    "Undo did not restore exact reverse document");
        }
        require(!c.editor.canUndo(), "No-op or gesture polluted history");
        for (int i = 1; i <= 50; ++i) {
            d.click(ids::redo);
            require(c.editor.currentSettings() == documents[static_cast<std::size_t>(i)],
                    "Redo did not restore exact document sequence");
        }
        require(c.editor.currentSettings() == final, "Full Redo changed final document hash");
        if (id == "UX-UND-003") {
            d.click(ids::undo);
            c.reveal(ids::exposure);
            d.drag(ids::exposure, {.5, .5}, {.6, .5});
            require(!c.editor.canRedo() && !d.item(ids::redo)->isEnabled(),
                    "New edit did not invalidate Redo branch");
        }
        return true;
    }
    return false;
}
} // namespace ux
