#include "core/catalog/LocalMaskStorage.h"
#include "core/catalog/Catalog.h"
#include "core/catalog/SqliteStatement.h"
namespace luma {
namespace detail {
std::vector<LocalMask> readLocalMasks(sqlite3 *db, const std::string &assetID) {
    Statement query(db, "SELECT typeof(document),length(CAST(document AS BLOB)),document FROM "
                        "edit_local_masks WHERE asset_id=?1");
    query.text(1, assetID);
    if (!query.row() || query.string(0) != "text" || query.integer(1) < 1 ||
        query.integer(1) > 16 * 1048576)
        throw std::runtime_error("Missing or invalid local mask document.");
    const auto text = query.string(2);
    if (static_cast<std::int64_t>(text.size()) != query.integer(1))
        throw std::runtime_error("Invalid local mask text encoding.");
    return decodeLocalMasks(text);
}
} // namespace detail
void Catalog::migrateLocalMasks() {
    detail::Statement unexpected(
        db_, "SELECT 1 FROM edit_documents WHERE schema_version!=8 OR "
             "typeof(enabled)!='integer' OR enabled NOT BETWEEN 0 AND 131071 LIMIT 1");
    if (unexpected.row())
        throw std::runtime_error("Unsupported edit document version during local mask migration.");
    execute(R"sql(
        CREATE TABLE edit_local_masks(
            asset_id TEXT PRIMARY KEY REFERENCES edit_documents(asset_id) ON DELETE CASCADE,
            document TEXT NOT NULL CHECK(typeof(document)='text' AND length(CAST(document AS BLOB)) BETWEEN 1 AND 1048576));
        INSERT INTO edit_local_masks SELECT asset_id,'LUMA_LOCAL_MASKS 1 0'||char(10) FROM edit_documents;
        CREATE TRIGGER initialize_local_masks AFTER INSERT ON edit_documents BEGIN
            INSERT INTO edit_local_masks VALUES(NEW.asset_id,'LUMA_LOCAL_MASKS 1 0'||char(10));
        END;
        INSERT INTO schema_migrations(version) VALUES(10);
        PRAGMA user_version=10;
    )sql");
}
void Catalog::writeLocalMasks(const std::string &assetID, const std::vector<LocalMask> &masks) {
    // UPDATE deliberately refuses a missing mandatory child rather than repairing corruption.
    detail::Statement query(db_, "UPDATE edit_local_masks SET document=?1 WHERE asset_id=?2");
    query.text(1, encodeLocalMasks(masks));
    query.text(2, assetID);
    query.row();
    if (sqlite3_changes(db_) != 1)
        throw std::runtime_error("Cannot save missing local mask document.");
}
} // namespace luma
