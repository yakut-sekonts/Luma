#include "core/catalog/RetouchStorage.h"
#include "core/catalog/Catalog.h"
#include "core/catalog/SqliteStatement.h"

namespace luma {
namespace detail {
std::vector<RetouchStroke> readRetouch(sqlite3 *db, const std::string &assetID) {
    Statement query(db, "SELECT typeof(document),length(CAST(document AS BLOB)),document "
                        "FROM edit_retouch WHERE asset_id=?1");
    query.text(1, assetID);
    if (!query.row() || query.string(0) != "text" || query.integer(1) < 1 ||
        query.integer(1) > 2 * 1048576)
        throw std::runtime_error("Missing or invalid retouch document.");
    const auto text = query.string(2);
    if (static_cast<std::int64_t>(text.size()) != query.integer(1))
        throw std::runtime_error("Invalid retouch text encoding.");
    return decodeRetouch(text);
}
} // namespace detail
void Catalog::migrateRetouch() {
    execute(R"sql(
        CREATE TABLE edit_retouch(
            asset_id TEXT PRIMARY KEY REFERENCES edit_documents(asset_id) ON DELETE CASCADE,
            document TEXT NOT NULL CHECK(typeof(document)='text' AND
                length(CAST(document AS BLOB)) BETWEEN 1 AND 2097152));
        INSERT INTO edit_retouch SELECT asset_id,'LUMA_RETOUCH 1 0'||char(10) FROM edit_documents;
        CREATE TRIGGER initialize_retouch AFTER INSERT ON edit_documents BEGIN
            INSERT INTO edit_retouch VALUES(NEW.asset_id,'LUMA_RETOUCH 1 0'||char(10));
        END;
        INSERT INTO schema_migrations(version) VALUES(13);
        PRAGMA user_version=13;
    )sql");
}
void Catalog::writeRetouch(const std::string &assetID, const std::vector<RetouchStroke> &strokes) {
    detail::Statement query(db_, "UPDATE edit_retouch SET document=?1 WHERE asset_id=?2");
    query.text(1, encodeRetouch(strokes));
    query.text(2, assetID);
    query.row();
    if (sqlite3_changes(db_) != 1)
        throw std::runtime_error("Cannot save missing retouch document.");
}
} // namespace luma
