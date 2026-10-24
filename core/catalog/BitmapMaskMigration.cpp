#include "core/catalog/Catalog.h"
#include "core/catalog/LocalMaskStorage.h"
#include "core/catalog/SqliteStatement.h"

namespace luma {
void Catalog::migrateBitmapMasks() {
    execute(R"sql(
        DROP TRIGGER initialize_local_masks;
        CREATE TABLE edit_local_masks_v3(
            asset_id TEXT PRIMARY KEY REFERENCES edit_documents(asset_id) ON DELETE CASCADE,
            document TEXT NOT NULL CHECK(typeof(document)='text' AND
                length(CAST(document AS BLOB)) BETWEEN 1 AND 16777216));
    )sql");
    {
        detail::Statement parents(db_, "SELECT asset_id FROM edit_documents ORDER BY asset_id");
        while (parents.row()) {
            const auto id = parents.string(0);
            const auto masks = detail::readLocalMasks(db_, id);
            detail::Statement insert(db_, "INSERT INTO edit_local_masks_v3 VALUES(?1,?2)");
            insert.text(1, id);
            insert.text(2, encodeLocalMasks(masks));
            insert.row();
        }
    }
    execute(R"sql(
        DROP TABLE edit_local_masks;
        ALTER TABLE edit_local_masks_v3 RENAME TO edit_local_masks;
        CREATE TRIGGER initialize_local_masks AFTER INSERT ON edit_documents BEGIN
            INSERT INTO edit_local_masks VALUES(NEW.asset_id,'LUMA_LOCAL_MASKS 3 0'||char(10));
        END;
        INSERT INTO schema_migrations(version) VALUES(12);
        PRAGMA user_version=12;
    )sql");
}
} // namespace luma
