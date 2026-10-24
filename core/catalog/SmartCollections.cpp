#include "core/catalog/Catalog.h"
#include "core/catalog/Identity.h"
#include "core/catalog/SqliteStatement.h"
namespace luma {
void Catalog::migrateSmartCollections() {
    execute("CREATE TABLE smart_collections(collection_id TEXT PRIMARY KEY REFERENCES "
            "collections(id) ON DELETE CASCADE,"
            "revision INTEGER NOT NULL DEFAULT 0 CHECK(revision>=0),document TEXT NOT NULL "
            "CHECK(length(CAST(document AS BLOB)) BETWEEN 1 AND 65536));"
            "CREATE TRIGGER smart_collection_rename AFTER UPDATE OF name ON collections BEGIN "
            "UPDATE smart_collections SET revision=revision+1 WHERE collection_id=NEW.id; "
            "END;INSERT INTO schema_migrations(version) VALUES(20);PRAGMA user_version=20;");
}
std::optional<SmartCollectionRecord> Catalog::smartCollection(const std::string &id) const {
    detail::Statement query(
        db_, "SELECT s.document,s.revision FROM smart_collections s WHERE s.collection_id=?1");
    query.text(1, id);
    if (!query.row())
        return std::nullopt;
    return SmartCollectionRecord{decodeSmartRule(query.string(0)), query.integer(1)};
}
std::string Catalog::saveSmartCollection(const std::string &id, const std::string &name,
                                         std::int64_t revision, const SmartRule &rule) {
    const auto document = encodeSmartRule(rule);
    if (name.empty() || name.size() > 512 ||
        name.find_first_not_of(" \t\r\n") == std::string::npos || revision < 0)
        throw std::runtime_error("Invalid smart collection name or revision.");
    execute("BEGIN IMMEDIATE;");
    try {
        auto result = id;
        if (result.empty()) {
            detail::Statement count(db_, "SELECT COUNT(*) FROM smart_collections");
            if (!count.row() || count.integer(0) >= 512)
                throw std::runtime_error("Smart collection limit reached.");
            result = createCollection(name);
            detail::Statement insert(
                db_, "INSERT INTO smart_collections(collection_id,document) VALUES(?1,?2)");
            insert.text(1, result);
            insert.text(2, document);
            insert.row();
        } else {
            detail::Statement update(db_,
                                     "UPDATE smart_collections SET document=?1,revision=revision+1 "
                                     "WHERE collection_id=?2 AND revision=?3");
            update.text(1, document);
            update.text(2, result);
            update.integer(3, revision);
            update.row();
            if (sqlite3_changes(db_) != 1)
                throw std::runtime_error("Smart collection changed. Reopen it before saving.");
            renameCollection(result, name);
        }
        execute("COMMIT;");
        return result;
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
} // namespace luma
