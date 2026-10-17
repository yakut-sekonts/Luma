#include "tests/ux/UxContext.h"
#include "tests/ux/UxTestIds.h"
#include <QSet>
#include <map>

namespace ux {
namespace {
using Documents = std::map<std::string, luma::EditDocument>;
Documents documents(Context &c) {
    luma::Catalog catalog(c.catalogFile.toStdString());
    Documents result;
    for (int offset = 0;; offset += 512) {
        const auto page = catalog.libraryPage({}, offset, 512);
        for (const auto &item : page) {
            const auto snapshot = catalog.assetSnapshot(item.id);
            require(snapshot.has_value(), "Batch fixture asset missing");
            result.emplace(item.id, snapshot->asset.edits);
        }
        if (page.size() < 512)
            return result;
    }
}
void idle(Context &c) {
    c.driver->wait(
        [&] { return !c.batch.working() && !c.library.working() && !c.library.loading(); },
        "batch settled", 60000);
    c.driver->wait([&] { return !c.driver->object("batch.wait")->property("visible").toBool(); },
                   "batch modal closed");
    c.checkData();
}
void sql(Context &c, const char *query) {
    sqlite3 *db = nullptr;
    require(sqlite3_open(c.catalogFile.toUtf8().constData(), &db) == SQLITE_OK,
            "Cannot open batch fault fixture");
    char *message = nullptr;
    const int result = sqlite3_exec(db, query, nullptr, nullptr, &message);
    const QString error = message ? QString::fromUtf8(message) : QString{};
    sqlite3_free(message);
    sqlite3_close(db);
    require(result == SQLITE_OK, "Batch fault setup failed: " + error);
}
} // namespace
bool batchScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-BAT-001", "UX-BAT-002", "UX-BAT-003", "UX-BAT-005",
                                         "UX-BAT-006", "UX-BAT-008", "UX-BAT-009", "UX-BAT-010",
                                         "UX-BAT-012", "UX-BAT-015", "UX-APP-010"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    if (id == "UX-BAT-009") {
        c.openFirst();
        c.saved();
        d.click(ids::library);
    } else
        c.import(id == "UX-BAT-003" || id == "UX-BAT-012" || id == "UX-APP-010" ? 1000 : 100);
    auto *grid = d.object(ids::grid);
    if (id == "UX-BAT-010") {
        d.wheel(ids::grid, -480);
        d.wait(
            [&] {
                return grid->property("contentY").toDouble() > 0 &&
                       !grid->property("moving").toBool();
            },
            "Library scroll settled");
    }
    const double scroll = grid->property("contentY").toDouble();
    d.click("library.selectAll");
    d.wait([&] { return !c.library.selecting() && c.library.selectedCount() == c.library.count(); },
           "batch selection ready");
    const auto selected = c.library.selectionIDs();
    const auto before = documents(c);
    d.choose("batchPresetChoice", 1);
    const auto presetID = c.presets.builtinItems()[1].toMap()["id"].toString();
    const auto preset = c.presets.snapshot(presetID);
    Documents expected;
    for (const auto &[asset, document] : before)
        expected.emplace(asset, luma::mergeSettings(document, preset.settings, preset.groups,
                                                    preset.parameters));
    require(expected != before, "Batch fixture preset is a no-op");
    if (id == "UX-BAT-002") {
        char *query = sqlite3_mprintf(
            "CREATE TRIGGER ux_batch_fail BEFORE UPDATE ON edit_documents WHEN NEW.asset_id=%Q "
            "BEGIN SELECT RAISE(ABORT,'UX mid-batch failure'); END;",
            selected[50].c_str());
        require(query, "Cannot allocate batch fault SQL");
        const QByteArray copy(query);
        sqlite3_free(query);
        sql(c, copy.constData());
    }
    d.click("batchApplyPreset");
    require(c.batch.working(), "Batch did not start: " + c.batch.error());
    if (id == "UX-BAT-003") {
        d.wait([&] { return d.object("batch.wait")->property("opened").toBool(); },
               "batch cancellation modal");
        d.click("batch.cancel");
        idle(c);
        require(documents(c) == before && !c.batch.canUndo(),
                "Cancelled batch partially committed assets");
        return true;
    }
    if (id == "UX-BAT-012" || id == "UX-APP-010") {
        d.wait([&] { return d.object("batch.wait")->property("opened").toBool(); },
               "batch modal ready for quit");
        d.log({{"action", "quit_batch_state"},
               {"working", c.batch.working()},
               {"modal_open", d.object("batch.wait")->property("opened").toBool()},
               {"active_window", c.window->isActive()}});
        d.quitShortcut();
        d.wait(
            [&] { return !c.window->isVisible() && !c.batch.working() && !c.library.importing(); },
            "quit during batch", 10000);
        const auto after = documents(c);
        require(after == before || after == expected, "Quit partially committed batch");
        c.checkData();
        return true;
    }
    idle(c);
    if (id == "UX-BAT-002") {
        require(c.batch.error().contains("UX mid-batch failure") && documents(c) == before &&
                    !c.batch.canUndo(),
                "Injected mid-batch failure did not roll back every asset");
        sql(c, "DROP TRIGGER ux_batch_fail");
        d.click("batchApplyPreset");
        idle(c);
    }
    require(c.batch.error().isEmpty() && documents(c) == expected && c.batch.canUndo(),
            "Batch preset did not atomically update the complete selection");
    if (id == "UX-BAT-009")
        require(c.editor.currentSettings() == expected.at(c.editor.photoID().toStdString()),
                "Open affected asset retained stale settings");
    if (id == "UX-BAT-010")
        require(c.library.selectionIDs() == selected &&
                    std::abs(grid->property("contentY").toDouble() - scroll) < 1,
                "Batch changed selection or Library scroll position");
    if (id == "UX-BAT-008") {
        d.doubleClick(c.card(0));
        c.loaded();
        c.reveal(ids::exposure);
        d.drag(ids::exposure, {.5, .5}, {.7, .5});
        c.saved();
        d.click(ids::library);
        const auto conflict = documents(c);
        d.click("batchUndo");
        idle(c);
        require(!c.batch.error().isEmpty() && documents(c) == conflict,
                "Conflicting batch Undo overwrote later edits");
        return true;
    }
    if (id == "UX-BAT-015") {
        d.click("batchApplyPreset");
        idle(c);
        require(documents(c) == expected, "No-op batch changed settings");
    }
    d.click("batchUndo");
    idle(c);
    require(documents(c) == before && !c.batch.canUndo() && c.batch.canRedo(),
            "Batch Undo did not restore all documents exactly once");
    d.click("batchRedo");
    idle(c);
    require(documents(c) == expected && c.batch.canUndo(),
            "Batch Redo did not restore complete applied documents");
    return true;
}
} // namespace ux
