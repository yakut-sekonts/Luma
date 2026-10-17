#include "tests/ux/UxContext.h"
#include "tests/ux/UxTestIds.h"
#include <memory>

namespace ux {
bool recoveryScenario(Context &c, const QString &id) {
    const bool failure = id == "UX-REC-003" || id == "UX-REC-004" || id == "UX-REC-005" ||
                         id == "UX-REC-006" || id == "UX-REC-012";
    if (!failure && id != "UX-REC-001" && id != "UX-REC-002")
        return false;
    auto &d = *c.driver;
    c.openFirst();
    c.saved();
    const auto before = c.editor.currentSettings();
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> lock(nullptr, sqlite3_close);
    if (id == "UX-REC-002") {
        sqlite3 *database = nullptr;
        require(sqlite3_open((c.root + "/catalog.sqlite").toUtf8().constData(), &database) ==
                    SQLITE_OK,
                "Cannot open busy-lock fixture");
        lock.reset(database);
        require(sqlite3_exec(database, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == SQLITE_OK,
                "Cannot acquire write-lock fixture");
    }
    c.storageFault.diskFull(failure);
    struct ResetFault {
        SqliteFault &fault;
        ~ResetFault() {
            fault.diskFull(false);
        }
    } reset{c.storageFault};
    c.reveal(ids::exposure);
    d.drag(ids::exposure, {.5, .5}, {.65, .5});
    const auto changed = c.editor.currentSettings();
    require(changed != before && c.editor.saving(), "Edit never entered pending save state");
    if (failure || lock) {
        d.log({{"action", "storage_fault"}, {"kind", failure ? "SQLITE_FULL" : "SQLITE_BUSY"}});
        d.wait([&] { return !c.editor.error().isEmpty(); }, "visible save failure");
        d.frame();
        require(d.item("editor.retrySave")->isVisible() && d.item("editor.retrySave")->isEnabled(),
                "Save failure has no actionable Retry");
        const auto durable =
            luma::Catalog((c.root + "/catalog.sqlite").toStdString()).activeAsset();
        require(durable && durable->edits == before, "Failed save partially persisted edits");
        require(c.editor.saving() && c.editor.currentSettings() == changed,
                "Save failure discarded the pending document");
        if (failure)
            require(c.storageFault.rejectedWrites() > 0,
                    "SQLITE_FULL was never injected into xWrite");
        c.checkData();
        d.screenshot(c.root + "/storage-failure.png");
        if (lock)
            require(sqlite3_exec(lock.get(), "ROLLBACK", nullptr, nullptr, nullptr) == SQLITE_OK,
                    "Cannot release busy fixture");
        c.storageFault.diskFull(false);
        d.click("editor.retrySave");
        if (id == "UX-REC-005") {
            // Two real clicks while Retry is still visible; controller must coalesce writes.
            if (d.item("editor.retrySave")->isVisible())
                d.click("editor.retrySave");
        }
    }
    c.saved();
    const auto durable = luma::Catalog((c.root + "/catalog.sqlite").toStdString()).activeAsset();
    require(durable && durable->edits == changed,
            "Retry did not persist the exact pending document");
    require(c.editor.error().isEmpty() && !c.editor.saving(),
            "Successful save retained a stale error");
    d.click(ids::undo);
    require(c.editor.currentSettings() == before && !c.editor.canUndo(),
            "Retry duplicated edit history");
    d.click(ids::redo);
    require(c.editor.currentSettings() == changed, "Retry damaged Redo");
    require(c.metrics.json()["main_loop_stall_max_ms"].toDouble() < 2000,
            "Storage failure blocked UI for >2 seconds");
    return true;
}
} // namespace ux
