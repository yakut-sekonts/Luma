#include "core/catalog/Catalog.h"
#include "core/catalog/Identity.h"
#include "core/catalog/SqliteStatement.h"
#include <algorithm>
#include <limits>
#include <set>

namespace luma {
namespace {
using detail::Statement;
constexpr auto where =
    " WHERE a.rating>=?1 AND (?2=-2 OR a.flag=?2)"
    " AND (?3='' OR a.filename LIKE ?3 ESCAPE '\\' OR a.original_path LIKE ?3 ESCAPE '\\'"
    " OR a.camera LIKE ?3 ESCAPE '\\' OR a.lens LIKE ?3 ESCAPE '\\')"
    " AND (?4='' OR EXISTS(SELECT 1 FROM collection_assets ca WHERE ca.asset_id=a.id AND "
    "ca.collection_id=?4))"
    " AND (?5=0 OR COALESCE(a.color_label,'')=?6)";
constexpr auto order = " ORDER BY a.filename COLLATE NOCASE,a.id";
void bindFilter(Statement &query, const LibraryFilter &filter) {
    filter.validate();
    query.integer(1, filter.minimumRating);
    query.integer(2, filter.flag);
    std::string search;
    for (char c : filter.search) {
        if (c == '%' || c == '_' || c == '\\')
            search += '\\';
        search += c;
    }
    query.text(3, search.empty() ? "" : "%" + search + "%");
    query.text(4, filter.collectionID);
    query.integer(5, filter.label.has_value() ? 1 : 0);
    query.text(6, filter.label.value_or(""));
}
void validateName(const std::string &name) {
    if (name.empty() || name.size() > 512 || name.find_first_not_of(" \t\r\n") == std::string::npos)
        throw std::runtime_error("Collection name must contain text and fit 128 characters.");
}
void requireChanged(sqlite3 *db) {
    if (sqlite3_changes(db) != 1)
        throw std::runtime_error("Library item is missing; transaction was not committed.");
}
} // namespace
std::int64_t Catalog::libraryCount(const LibraryFilter &filter) const {
    Statement query(db_, ("SELECT COUNT(*) FROM assets a" + std::string(where)).c_str());
    bindFilter(query, filter);
    if (!query.row())
        throw std::runtime_error("Cannot count library.");
    return query.integer(0);
}
std::optional<std::int64_t> Catalog::libraryIndex(const LibraryFilter &filter,
                                                  const std::string &assetID) const {
    filter.validate();
    if (assetID.empty())
        return std::nullopt;
    Statement query(db_,
                    ("SELECT position FROM (SELECT a.id,ROW_NUMBER() OVER (" + std::string(order) +
                     ")-1 AS position FROM assets a" + where + ") WHERE id=?7")
                        .c_str());
    bindFilter(query, filter);
    query.text(7, assetID);
    return query.row() ? std::optional<std::int64_t>(query.integer(0)) : std::nullopt;
}
std::vector<LibraryItem> Catalog::libraryPage(const LibraryFilter &filter, int offset,
                                              int limit) const {
    if (offset < 0 || limit < 1 || limit > 1024)
        throw std::runtime_error("Invalid metadata page bounds.");
    Statement query(
        db_, ("SELECT a.id,a.original_path,a.filename,a.checksum,a.width,a.height,a.orientation,"
              "a.rating,a.flag,COALESCE(a.color_label,''),a.camera FROM assets a" +
              std::string(where) + order + " LIMIT ?7 OFFSET ?8")
                 .c_str());
    bindFilter(query, filter);
    query.integer(7, limit);
    query.integer(8, offset);
    std::vector<LibraryItem> rows;
    while (query.row())
        rows.push_back({query.string(0), query.string(1), query.string(2), query.string(3),
                        static_cast<int>(query.integer(4)), static_cast<int>(query.integer(5)),
                        static_cast<int>(query.integer(6)), static_cast<int>(query.integer(7)),
                        static_cast<int>(query.integer(8)), query.string(9), query.string(10)});
    return rows;
}
std::vector<std::string> Catalog::libraryIDs(const LibraryFilter &filter, int offset,
                                             int limit) const {
    if (offset < 0 || limit < 0)
        throw std::runtime_error("Invalid selection bounds.");
    Statement query(db_, ("SELECT a.id FROM assets a" + std::string(where) + std::string(order) +
                          " LIMIT ?7 OFFSET ?8")
                             .c_str());
    bindFilter(query, filter);
    query.integer(7, limit);
    query.integer(8, offset);
    std::vector<std::string> ids;
    while (query.row())
        ids.push_back(query.string(0));
    return ids;
}
void Catalog::updateMetadata(const std::vector<std::string> &ids, const char *sql,
                             std::optional<int> number, const std::string &text) {
    execute("BEGIN IMMEDIATE;");
    try {
        for (const auto &id : ids) {
            Statement query(db_, sql);
            if (number)
                query.integer(1, *number);
            else
                query.text(1, text);
            query.text(2, id);
            query.row();
            requireChanged(db_);
        }
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void Catalog::setRating(const std::vector<std::string> &ids, int rating) {
    if (rating < 0 || rating > 5)
        throw std::runtime_error("Rating must be 0–5.");
    updateMetadata(ids, "UPDATE assets SET rating=?1 WHERE id=?2", rating);
}
void Catalog::setFlag(const std::vector<std::string> &ids, int flag) {
    if (flag < -1 || flag > 1)
        throw std::runtime_error("Flag must be Reject, Unflagged or Pick.");
    updateMetadata(ids, "UPDATE assets SET flag=?1 WHERE id=?2", flag);
}
void Catalog::setLabel(const std::vector<std::string> &ids, const std::string &label) {
    validateLabel(label);
    updateMetadata(ids, "UPDATE assets SET color_label=?1 WHERE id=?2", std::nullopt, label);
}
std::vector<CollectionRecord> Catalog::collections() const {
    Statement query(
        db_,
        "SELECT c.id,c.name,COUNT(ca.asset_id) FROM collections c LEFT JOIN collection_assets ca"
        " ON ca.collection_id=c.id GROUP BY c.id ORDER BY c.name COLLATE NOCASE,c.id");
    std::vector<CollectionRecord> rows;
    while (query.row())
        rows.push_back({query.string(0), query.string(1), query.integer(2)});
    return rows;
}
std::string Catalog::createCollection(const std::string &name) {
    validateName(name);
    const auto id = newID();
    Statement query(db_, "INSERT INTO collections(id,name) VALUES(?1,?2)");
    query.text(1, id);
    query.text(2, name);
    query.row();
    return id;
}
void Catalog::renameCollection(const std::string &id, const std::string &name) {
    validateName(name);
    Statement query(db_, "UPDATE collections SET name=?1 WHERE id=?2");
    query.text(1, name);
    query.text(2, id);
    query.row();
    requireChanged(db_);
}
void Catalog::deleteCollection(const std::string &id) {
    Statement query(db_, "DELETE FROM collections WHERE id=?1");
    query.text(1, id);
    query.row();
    requireChanged(db_);
}
void Catalog::collectionMembership(const std::string &collectionID,
                                   const std::vector<std::string> &ids, bool add) {
    execute("BEGIN IMMEDIATE;");
    try {
        Statement exists(db_, "SELECT 1 FROM collections WHERE id=?1");
        exists.text(1, collectionID);
        if (!exists.row())
            throw std::runtime_error("Collection does not exist.");
        for (const auto &id : ids) {
            Statement asset(db_, "SELECT 1 FROM assets WHERE id=?1");
            asset.text(1, id);
            if (!asset.row())
                throw std::runtime_error("Photo does not exist.");
            Statement query(
                db_, add ? "INSERT OR IGNORE INTO collection_assets(collection_id,asset_id) "
                           "VALUES(?1,?2)"
                         : "DELETE FROM collection_assets WHERE collection_id=?1 AND asset_id=?2");
            query.text(1, collectionID);
            query.text(2, id);
            query.row();
        }
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
std::string Catalog::beginImport(ImportMode mode, const std::filesystem::path &destination,
                                 const std::vector<std::filesystem::path> &sources,
                                 std::int64_t ownerPID, bool skipDuplicates,
                                 const std::map<std::filesystem::path, std::string> &sourceErrors) {
    if (sources.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        ownerPID <= 0 || (mode != ImportMode::add && mode != ImportMode::copy))
        throw std::runtime_error("Invalid import session.");
    if (!sourceErrors.empty()) {
        const std::set<std::filesystem::path> knownSources(sources.begin(), sources.end());
        for (const auto &[source, error] : sourceErrors) {
            if (error.empty() || !knownSources.contains(source))
                throw std::runtime_error("Invalid initial import error.");
        }
    }
    const auto id = newID();
    execute("BEGIN IMMEDIATE;");
    try {
        Statement session(
            db_, "INSERT INTO import_sessions(id,mode,destination,state,owner_pid,skip_duplicates) "
                 "VALUES(?1,?2,?3,'running',?4,?5)");
        session.text(1, id);
        session.text(2, mode == ImportMode::add ? "add" : "copy");
        session.text(3, destination.string());
        session.integer(4, ownerPID);
        session.integer(5, skipDuplicates ? 1 : 0);
        session.row();
        for (std::size_t i = 0; i < sources.size(); ++i) {
            const auto failure = sourceErrors.find(sources[i]);
            Statement item(db_,
                           "INSERT INTO import_items(session_id,position,source_path,state,error) "
                           "VALUES(?1,?2,?3,?4,?5)");
            item.text(1, id);
            item.integer(2, static_cast<std::int64_t>(i));
            item.text(3, sources[i].string());
            item.text(4, failure == sourceErrors.end() ? "queued" : "failed");
            item.text(5, failure == sourceErrors.end() ? "" : failure->second);
            item.row();
        }
        execute("COMMIT;");
        return id;
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void Catalog::stageImport(const ImportJournalItem &item) {
    Statement query(db_, "UPDATE import_items SET "
                         "target_path=?1,temporary_path=?2,checksum=?3,filesize=?4,state='staged'"
                         " WHERE session_id=?5 AND position=?6 AND state IN ('queued','staged')");
    query.text(1, item.target.string());
    query.text(2, item.temporary.string());
    query.text(3, item.checksum);
    query.integer(4, item.size);
    query.text(5, item.sessionID);
    query.integer(6, item.index);
    query.row();
    requireChanged(db_);
}
bool Catalog::commitImport(const ImportJournalItem &item, const PhotoMetadata &metadata,
                           bool skipDuplicates) {
    execute("BEGIN IMMEDIATE;");
    try {
        auto existing =
            skipDuplicates ? duplicate(item.checksum, item.size) : std::optional<AssetRecord>{};
        bool inserted = !existing.has_value();
        if (!existing) {
            Statement path(db_, "SELECT id FROM assets WHERE original_path=?1");
            path.text(1, std::filesystem::canonical(item.target).string());
            inserted = !path.row();
            existing = addAssetRecord(item.target, item.checksum, metadata, false);
        }
        Statement complete(db_, "UPDATE import_items SET state=?1,asset_id=?2,error='' WHERE "
                                "session_id=?3 AND position=?4"
                                " AND state IN ('queued','staged')");
        complete.text(1, inserted ? "committed" : "duplicate");
        complete.text(2, existing->id);
        complete.text(3, item.sessionID);
        complete.integer(4, item.index);
        complete.row();
        requireChanged(db_);
        execute("COMMIT;");
        return inserted;
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void Catalog::finishImportItem(const std::string &sessionID, int index, const std::string &state,
                               const std::string &error, const std::string &assetID) {
    if (state != "duplicate" && state != "failed" && state != "cancelled")
        throw std::runtime_error("Invalid import result.");
    Statement query(db_, "UPDATE import_items SET state=?1,error=?2,asset_id=NULLIF(?3,'') WHERE "
                         "session_id=?4 AND position=?5"
                         " AND state IN ('queued','staged')");
    query.text(1, state);
    query.text(2, error.substr(0, 2048));
    query.text(3, assetID);
    query.text(4, sessionID);
    query.integer(5, index);
    query.row();
    requireChanged(db_);
}
void Catalog::finishImport(const std::string &sessionID, const std::string &state) {
    if (state != "completed" && state != "cancelled" && state != "interrupted")
        throw std::runtime_error("Invalid session result.");
    execute("BEGIN IMMEDIATE;");
    try {
        Statement cancel(
            db_,
            "UPDATE import_items SET state='cancelled' WHERE session_id=?1 AND state='queued'");
        cancel.text(1, sessionID);
        cancel.row();
        Statement query(db_, "UPDATE import_sessions SET state=?1,finished_at=CURRENT_TIMESTAMP "
                             "WHERE id=?2 AND state='running'");
        query.text(1, state);
        query.text(2, sessionID);
        query.row();
        requireChanged(db_);
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
std::vector<ImportJournal> Catalog::unfinishedImports() const {
    Statement sessions(db_, "SELECT id,state,mode,owner_pid,destination,skip_duplicates FROM "
                            "import_sessions WHERE state='running' ORDER BY created_at,id");
    std::vector<ImportJournal> result;
    while (sessions.row()) {
        ImportJournal journal{sessions.string(0),
                              sessions.string(1),
                              sessions.string(2) == "copy" ? ImportMode::copy : ImportMode::add,
                              sessions.integer(3),
                              sessions.string(4),
                              {},
                              sessions.integer(5) != 0};
        Statement items(
            db_,
            "SELECT position,source_path,target_path,temporary_path,checksum,state,error,filesize "
            "FROM import_items"
            " WHERE session_id=?1 AND state IN ('queued','staged') ORDER BY position");
        items.text(1, journal.id);
        while (items.row())
            journal.items.push_back({journal.id, static_cast<int>(items.integer(0)),
                                     items.string(1), items.string(2), items.string(3),
                                     items.string(4), items.string(5), items.string(6),
                                     items.integer(7)});
        result.push_back(std::move(journal));
    }
    return result;
}
ImportSummary Catalog::importSummary(const std::string &sessionID) const {
    Statement query(db_,
                    "SELECT state,COUNT(*) FROM import_items WHERE session_id=?1 GROUP BY state");
    query.text(1, sessionID);
    ImportSummary result;
    result.sessionID = sessionID;
    while (query.row()) {
        const auto state = query.string(0);
        const auto count = query.integer(1);
        if (state == "committed")
            result.added = count;
        else if (state == "duplicate")
            result.duplicates = count;
        else if (state == "failed")
            result.failed = count;
        else if (state == "cancelled")
            result.cancelled = count;
    }
    return result;
}
std::vector<std::string> Catalog::importProblems(const std::string &sessionID) const {
    Statement query(db_, "SELECT source_path,error FROM import_items WHERE session_id=?1"
                         " AND state='failed' ORDER BY position LIMIT 100");
    query.text(1, sessionID);
    std::vector<std::string> results;
    while (query.row())
        results.push_back(query.string(0) + "\n" + query.string(1));
    return results;
}
} // namespace luma

namespace luma {
bool Catalog::claimImport(const std::string &id, std::int64_t previousPID, std::int64_t newPID) {
    if (newPID <= 0)
        throw std::runtime_error("Invalid recovery owner.");
    detail::Statement query(
        db_,
        "UPDATE import_sessions SET owner_pid=?1 WHERE id=?2 AND owner_pid=?3 AND state='running'");
    query.integer(1, newPID);
    query.text(2, id);
    query.integer(3, previousPID);
    query.row();
    return sqlite3_changes(db_) == 1;
}
} // namespace luma
