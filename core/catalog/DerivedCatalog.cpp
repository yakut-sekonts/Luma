#include "core/catalog/Catalog.h"
#include "core/catalog/SqliteStatement.h"

namespace luma {
void Catalog::migrateDerivedImages() {
    execute(R"sql(
        CREATE TABLE derived_images(
            path TEXT PRIMARY KEY, checksum TEXT NOT NULL, width INTEGER NOT NULL CHECK(width>0),
            height INTEGER NOT NULL CHECK(height>0), owner_pid INTEGER NOT NULL,
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
        INSERT INTO schema_migrations(version) VALUES(15);
        PRAGMA user_version=15;
    )sql");
}
void Catalog::stageDerived(const DerivedImageRecord &record) {
    if (!record.path.is_absolute() || record.checksum.size() != 64 || record.width < 1 ||
        record.height < 1)
        throw std::runtime_error("Invalid derived image journal entry.");
    detail::Statement insert(
        db_,
        "INSERT INTO derived_images(path,checksum,width,height,owner_pid) VALUES(?1,?2,?3,?4,?5)");
    insert.text(1, record.path.string());
    insert.text(2, record.checksum);
    insert.integer(3, record.width);
    insert.integer(4, record.height);
    insert.integer(5, record.ownerPID);
    insert.row();
}
void Catalog::finishDerived(const DerivedImageRecord &record) {
    execute("BEGIN IMMEDIATE;");
    try {
        detail::Statement entry(db_,
                                "SELECT checksum,width,height FROM derived_images WHERE path=?1");
        entry.text(1, record.path.string());
        if (!entry.row() || entry.string(0) != record.checksum ||
            entry.integer(1) != record.width || entry.integer(2) != record.height)
            throw std::runtime_error("Derived image journal changed before registration.");
        PhotoMetadata metadata;
        metadata.width = record.width;
        metadata.height = record.height;
        metadata.camera = "Luma Linear RGB Float";
        addAssetRecord(record.path, record.checksum, metadata, false);
        discardDerived(record.path);
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void Catalog::discardDerived(const std::filesystem::path &path) {
    detail::Statement remove(db_, "DELETE FROM derived_images WHERE path=?1");
    remove.text(1, path.string());
    remove.row();
}
std::vector<DerivedImageRecord> Catalog::pendingDerived() const {
    detail::Statement query(
        db_, "SELECT path,checksum,width,height,owner_pid FROM derived_images ORDER BY path");
    std::vector<DerivedImageRecord> result;
    while (query.row())
        result.push_back({query.string(0), query.string(1), static_cast<int>(query.integer(2)),
                          static_cast<int>(query.integer(3)), query.integer(4)});
    return result;
}
} // namespace luma
