#include "core/catalog/Catalog.h"
#include "core/catalog/Identity.h"
#include "core/catalog/SqliteStatement.h"
#include "core/edits/EditSnapshot.h"
#include <limits>

namespace luma {
namespace {
using detail::Statement;
constexpr std::int64_t maximumVersions = 100;
constexpr std::int64_t maximumStoredBytes = 256 * 1024 * 1024;
void validateName(const std::string &name) {
    if (name.empty() || name.size() > 512 || name.front() == ' ' || name.back() == ' ')
        throw std::runtime_error(
            "Version names must contain 1–512 UTF-8 bytes without surrounding spaces.");
    for (std::size_t i = 0; i < name.size();) {
        const auto first = static_cast<unsigned char>(name[i++]);
        if (first < 0x80) {
            if (first < 32 || first == 127)
                throw std::runtime_error("Version names cannot contain control characters.");
            continue;
        }
        const int remaining = first >= 0xC2 && first <= 0xDF   ? 1
                              : first >= 0xE0 && first <= 0xEF ? 2
                              : first >= 0xF0 && first <= 0xF4 ? 3
                                                               : -1;
        if (remaining < 0 || i + static_cast<std::size_t>(remaining) > name.size())
            throw std::runtime_error("Version names must contain valid UTF-8.");
        std::uint32_t code = first & (remaining == 1 ? 0x1F : remaining == 2 ? 0x0F : 0x07);
        for (int j = 0; j < remaining; ++j) {
            const auto next = static_cast<unsigned char>(name[i++]);
            if ((next & 0xC0) != 0x80)
                throw std::runtime_error("Version names must contain valid UTF-8.");
            code = (code << 6) | (next & 0x3F);
        }
        if (code < (remaining == 1   ? 0x80U
                    : remaining == 2 ? 0x800U
                                     : 0x10000U) ||
            (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF)
            throw std::runtime_error("Version names must contain valid UTF-8.");
        if (code >= 0x80 && code <= 0x9F)
            throw std::runtime_error("Version names cannot contain control characters.");
    }
}
void requireRevision(std::int64_t revision) {
    if (revision < 0 || revision == std::numeric_limits<std::int64_t>::max())
        throw std::runtime_error("Invalid processing version revision.");
}
void requireChanged(sqlite3 *db) {
    if (sqlite3_changes(db) != 1)
        throw std::runtime_error(
            "This version changed or was deleted. Refresh the list and try again.");
}
} // namespace
void Catalog::migrateEditVersions() {
    execute(R"sql(
        CREATE TABLE edit_versions(
            id TEXT PRIMARY KEY, asset_id TEXT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
            name TEXT NOT NULL CHECK(typeof(name)='text' AND length(CAST(name AS BLOB)) BETWEEN 1 AND 512),
            revision INTEGER NOT NULL DEFAULT 0 CHECK(typeof(revision)='integer' AND revision>=0),
            created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
            document TEXT NOT NULL CHECK(typeof(document)='text' AND length(CAST(document AS BLOB)) BETWEEN 1 AND 20971520),
            UNIQUE(asset_id,name));
        CREATE INDEX edit_versions_asset ON edit_versions(asset_id,created_at,id);
        INSERT INTO schema_migrations(version) VALUES(18);
        PRAGMA user_version=18;
    )sql");
}
std::vector<EditVersionInfo> Catalog::editVersions(const std::string &assetID) const {
    Statement query(db_, "SELECT id,name,revision,created_at FROM edit_versions WHERE asset_id=?1 "
                         "ORDER BY created_at DESC,id DESC LIMIT 101");
    query.text(1, assetID);
    std::vector<EditVersionInfo> result;
    while (query.row()) {
        if (result.size() >= static_cast<std::size_t>(maximumVersions))
            throw std::runtime_error("Invalid processing version list.");
        auto name = query.string(1);
        validateName(name);
        requireRevision(query.integer(2));
        result.push_back({query.string(0), std::move(name), query.string(3), query.integer(2)});
    }
    return result;
}
std::string Catalog::createEditVersion(const std::string &assetID, const std::string &name,
                                       const EditDocument &edits) {
    validateName(name);
    const auto document = encodeEditSnapshot(edits);
    const auto id = newID();
    execute("BEGIN IMMEDIATE;");
    try {
        Statement asset(db_, "SELECT 1 FROM assets WHERE id=?1");
        asset.text(1, assetID);
        if (!asset.row())
            throw std::runtime_error("The photo is no longer in the catalog.");
        Statement budget(db_, "SELECT COUNT(*),COALESCE(SUM(length(CAST(document AS BLOB))),0) "
                              "FROM edit_versions WHERE asset_id=?1");
        budget.text(1, assetID);
        if (!budget.row() || budget.integer(0) >= maximumVersions ||
            budget.integer(1) > maximumStoredBytes - static_cast<std::int64_t>(document.size()))
            throw std::runtime_error(
                "A photo supports at most 100 versions and 256 MiB of version data.");
        Statement duplicate(db_, "SELECT 1 FROM edit_versions WHERE asset_id=?1 AND name=?2");
        duplicate.text(1, assetID);
        duplicate.text(2, name);
        if (duplicate.row())
            throw std::runtime_error("A version with this name already exists.");
        Statement insert(
            db_, "INSERT INTO edit_versions(id,asset_id,name,document) VALUES(?1,?2,?3,?4)");
        insert.text(1, id);
        insert.text(2, assetID);
        insert.text(3, name);
        insert.text(4, document);
        insert.row();
        execute("COMMIT;");
        return id;
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
EditDocument Catalog::editVersion(const std::string &assetID, const std::string &id,
                                  std::int64_t revision) const {
    requireRevision(revision);
    Statement query(db_, "SELECT typeof(document),length(CAST(document AS BLOB)),document FROM "
                         "edit_versions WHERE asset_id=?1 AND id=?2 AND revision=?3");
    query.text(1, assetID);
    query.text(2, id);
    query.integer(3, revision);
    if (!query.row())
        throw std::runtime_error(
            "This version changed or was deleted. Refresh the list and try again.");
    if (query.string(0) != "text" || query.integer(1) < 1 ||
        query.integer(1) > static_cast<std::int64_t>(maximumEditSnapshotBytes))
        throw std::runtime_error("Invalid processing version document.");
    const auto document = query.string(2);
    if (document.size() != static_cast<std::size_t>(query.integer(1)))
        throw std::runtime_error("Invalid processing version document.");
    return decodeEditSnapshot(document);
}
void Catalog::renameEditVersion(const std::string &assetID, const std::string &id,
                                std::int64_t revision, const std::string &name) {
    validateName(name);
    requireRevision(revision);
    execute("BEGIN IMMEDIATE;");
    try {
        Statement duplicate(db_,
                            "SELECT 1 FROM edit_versions WHERE asset_id=?1 AND name=?2 AND id<>?3");
        duplicate.text(1, assetID);
        duplicate.text(2, name);
        duplicate.text(3, id);
        if (duplicate.row())
            throw std::runtime_error("A version with this name already exists.");
        Statement update(db_, "UPDATE edit_versions SET name=?1,revision=revision+1 WHERE "
                              "asset_id=?2 AND id=?3 AND revision=?4");
        update.text(1, name);
        update.text(2, assetID);
        update.text(3, id);
        update.integer(4, revision);
        update.row();
        requireChanged(db_);
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void Catalog::deleteEditVersion(const std::string &assetID, const std::string &id,
                                std::int64_t revision) {
    requireRevision(revision);
    Statement remove(db_, "DELETE FROM edit_versions WHERE asset_id=?1 AND id=?2 AND revision=?3");
    remove.text(1, assetID);
    remove.text(2, id);
    remove.integer(3, revision);
    remove.row();
    requireChanged(db_);
}
} // namespace luma
