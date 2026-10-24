#include "core/catalog/CreativeStorage.h"
#include "core/catalog/Catalog.h"
#include "core/catalog/SqliteStatement.h"
namespace luma {
namespace detail {
CreativeSettings readCreative(sqlite3 *db, const std::string &assetID) {
    Statement query(db, "SELECT typeof(document),length(CAST(document AS BLOB)),document FROM "
                        "edit_creative WHERE asset_id=?1");
    query.text(1, assetID);
    if (!query.row() || query.string(0) != "text" || query.integer(1) < 1 ||
        query.integer(1) > 8192)
        throw std::runtime_error("Missing or invalid creative adjustment document.");
    return decodeCreative(query.string(2));
}
} // namespace detail
void Catalog::migrateCreative() {
    const auto initial = encodeCreative({}); // Fixed numeric codec; no user text in DDL.
    execute(("CREATE TABLE edit_creative(asset_id TEXT PRIMARY KEY REFERENCES "
             "edit_documents(asset_id) ON DELETE CASCADE,"
             "document TEXT NOT NULL CHECK(typeof(document)='text' AND length(CAST(document AS "
             "BLOB)) BETWEEN 1 AND 8192));"
             "INSERT INTO edit_creative SELECT asset_id,'" +
             initial +
             "' FROM edit_documents;"
             "CREATE TRIGGER initialize_creative AFTER INSERT ON edit_documents BEGIN INSERT INTO "
             "edit_creative VALUES(NEW.asset_id,'" +
             initial +
             "'); END; INSERT INTO schema_migrations(version) VALUES(19); PRAGMA user_version=19;")
                .c_str());
}
void Catalog::writeCreative(const std::string &assetID, const CreativeSettings &settings) {
    detail::Statement query(db_, "UPDATE edit_creative SET document=?1 WHERE asset_id=?2");
    query.text(1, encodeCreative(settings));
    query.text(2, assetID);
    query.row();
    if (sqlite3_changes(db_) != 1)
        throw std::runtime_error("Cannot save missing creative adjustment document.");
}
} // namespace luma
