#include "core/catalog/Catalog.h"
#include "core/catalog/SqliteStatement.h"

namespace luma {
std::optional<FileRecord> Catalog::fileRecord(const std::string &id) const {
    detail::Statement query(db_,
                            "SELECT id,original_path,checksum,filesize FROM assets WHERE id=?1");
    query.text(1, id);
    if (!query.row())
        return {};
    return FileRecord{query.string(0), query.string(1), query.string(2), query.integer(3)};
}
std::vector<FileRecord> Catalog::fileRecords(const std::string &after, int limit) const {
    if (limit < 1 || limit > 1024)
        throw std::runtime_error("Invalid file metadata page size.");
    detail::Statement query(
        db_,
        "SELECT id,original_path,checksum,filesize FROM assets WHERE id>?1 ORDER BY id LIMIT ?2");
    query.text(1, after);
    query.integer(2, limit);
    std::vector<FileRecord> result;
    while (query.row())
        result.push_back({query.string(0), query.string(1), query.string(2), query.integer(3)});
    return result;
}
void Catalog::relink(const std::vector<RelinkRecord> &records) {
    execute("BEGIN IMMEDIATE;");
    try {
        for (const auto &record : records) {
            if (!record.destination.is_absolute())
                throw std::runtime_error("Relink requires an absolute path.");
            detail::Statement update(
                db_, "UPDATE assets SET original_path=?1,filename=?2,extension=?3 "
                     "WHERE id=?4 AND original_path=?5 AND checksum=?6 AND filesize=?7");
            update.text(1, record.destination.string());
            update.text(2, record.destination.filename().string());
            update.text(3, record.destination.extension().string());
            update.text(4, record.source.id);
            update.text(5, record.source.path.string());
            update.text(6, record.source.checksum);
            update.integer(7, record.source.size);
            update.row();
            if (sqlite3_changes(db_) != 1)
                throw std::runtime_error("Catalog source changed before relinking.");
            detail::Statement metadata(
                db_,
                "UPDATE asset_metadata SET sidecar_hash='',revision=revision+1 WHERE asset_id=?1");
            metadata.text(1, record.source.id);
            metadata.row();
        }
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
} // namespace luma
