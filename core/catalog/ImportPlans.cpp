#include "core/catalog/Catalog.h"
#include "core/catalog/SqliteStatement.h"
namespace luma {
void Catalog::migrateImportPlans() {
    execute("CREATE TABLE import_options(session_id TEXT PRIMARY KEY REFERENCES "
            "import_sessions(id) ON DELETE CASCADE,document TEXT NOT NULL "
            "CHECK(length(CAST(document AS BLOB)) BETWEEN 1 AND 21102592));"
            "CREATE TABLE import_item_plans(session_id TEXT NOT NULL,position INTEGER NOT "
            "NULL,backup_path TEXT NOT NULL,source_hold_path TEXT NOT NULL,metadata_document TEXT "
            "NOT NULL CHECK(length(CAST(metadata_document AS BLOB)) BETWEEN 1 AND 1048576),"
            "move_state TEXT NOT NULL CHECK(move_state IN ('pending','done','retained')),PRIMARY "
            "KEY(session_id,position),"
            "FOREIGN KEY(session_id,position) REFERENCES import_items(session_id,position) ON "
            "DELETE CASCADE);"
            "INSERT INTO schema_migrations(version) VALUES(21);PRAGMA user_version=21;");
}
void Catalog::stageImportPlan(const ImportJournalItem &item) {
    execute("BEGIN IMMEDIATE;");
    try {
        stageImport(item);
        detail::Statement plan(db_, "INSERT INTO "
                                    "import_item_plans(session_id,position,backup_path,source_hold_"
                                    "path,move_state,metadata_document) VALUES(?1,?2,?3,?4,?5,?6)");
        plan.text(1, item.sessionID);
        plan.integer(2, item.index);
        plan.text(3, item.backup.string());
        plan.text(4, item.sourceHold.string());
        plan.text(5, item.sourceHold.empty() ? "done" : "pending");
        plan.text(6, item.metadataDocument);
        plan.row();
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
bool Catalog::referencesSource(const std::filesystem::path &path) const {
    detail::Statement query(db_, "SELECT 1 FROM assets WHERE original_path=?1 LIMIT 1");
    query.text(1, path.lexically_normal().string());
    return query.row();
}
void Catalog::finishImportMove(const ImportJournalItem &item, const std::string &state,
                               const std::string &error) {
    if (state != "done" && state != "retained")
        throw std::runtime_error("Invalid Move result.");
    execute("BEGIN IMMEDIATE;");
    try {
        detail::Statement plan(
            db_, "UPDATE import_item_plans SET move_state=?1 WHERE session_id=?2 AND position=?3");
        plan.text(1, state);
        plan.text(2, item.sessionID);
        plan.integer(3, item.index);
        plan.row();
        detail::Statement note(db_, "UPDATE import_items SET error=?1 WHERE session_id=?2 AND "
                                    "position=?3 AND state='committed'");
        note.text(1, error.substr(0, 2048));
        note.text(2, item.sessionID);
        note.integer(3, item.index);
        note.row();
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
} // namespace luma
