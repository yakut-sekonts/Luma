#include "core/catalog/Catalog.h"
#include "core/catalog/MetadataStorage.h"
#include "core/catalog/SqliteStatement.h"

namespace luma {
using detail::Statement;
namespace {
void readLists(sqlite3 *db, const std::string &id, DescriptiveMetadata &values) {
    Statement creators(
        db, "SELECT position,name FROM asset_creators WHERE asset_id=?1 ORDER BY position");
    creators.text(1, id);
    while (creators.row()) {
        if (creators.integer(0) != static_cast<std::int64_t>(values.creators.size()) ||
            values.creators.size() >= 256)
            throw std::runtime_error("Invalid stored creator sequence.");
        values.creators.push_back(creators.string(1));
    }
    Statement keywords(db,
                       "SELECT keyword FROM asset_keywords WHERE asset_id=?1 ORDER BY position");
    keywords.text(1, id);
    while (keywords.row()) {
        if (values.keywords.size() >= 256)
            throw std::runtime_error("Invalid stored keyword list.");
        values.keywords.push_back(keywords.string(0));
    }
}
void writeLists(sqlite3 *db, const std::string &id, const DescriptiveMetadata &values) {
    Statement clearCreators(db, "DELETE FROM asset_creators WHERE asset_id=?1");
    clearCreators.text(1, id);
    clearCreators.row();
    Statement clearKeywords(db, "DELETE FROM asset_keywords WHERE asset_id=?1");
    clearKeywords.text(1, id);
    clearKeywords.row();
    for (std::size_t i = 0; i < values.creators.size(); ++i) {
        Statement creator(db,
                          "INSERT INTO asset_creators(asset_id,position,name) VALUES(?1,?2,?3)");
        creator.text(1, id);
        creator.integer(2, static_cast<std::int64_t>(i));
        creator.text(3, values.creators[i]);
        creator.row();
    }
    for (std::size_t i = 0; i < values.keywords.size(); ++i) {
        Statement keyword(db,
                          "INSERT INTO asset_keywords(asset_id,position,keyword) VALUES(?1,?2,?3)");
        keyword.text(1, id);
        keyword.integer(2, static_cast<std::int64_t>(i));
        keyword.text(3, values.keywords[i]);
        keyword.row();
    }
}
void writeOrganization(sqlite3 *db, const std::string &id, const DescriptiveMetadata &values) {
    Statement organization(db, "UPDATE assets SET rating=?1,flag=?2,color_label=?3 WHERE id=?4");
    organization.integer(1, values.rating);
    organization.integer(2, values.flag);
    organization.text(3, values.colorLabel());
    organization.text(4, id);
    organization.row();
}
} // namespace
MetadataOverlay detail::readMetadataOverlay(sqlite3 *db, const std::string &id) {
    Statement query(db, "SELECT m.title,m.caption,m.copyright,m.location,a.rating,a.flag,"
                        "m.xmp_label,m.initialized FROM assets a JOIN asset_metadata m ON "
                        "m.asset_id=a.id WHERE a.id=?1");
    query.text(1, id);
    if (!query.row())
        throw std::runtime_error("Photo metadata is missing from the catalog.");
    MetadataOverlay result;
    result.values.title = query.string(0);
    result.values.caption = query.string(1);
    result.values.copyright = query.string(2);
    result.values.location = query.string(3);
    result.values.rating = static_cast<int>(query.integer(4));
    result.values.flag = static_cast<int>(query.integer(5));
    result.values.label = query.string(6);
    result.includeText = query.integer(7) != 0;
    readLists(db, id, result.values);
    result.values.validate();
    return result;
}
void Catalog::migrateMetadata() {
    execute(R"sql(
        CREATE TABLE asset_metadata(
            asset_id TEXT PRIMARY KEY REFERENCES assets(id) ON DELETE CASCADE,
            title TEXT NOT NULL DEFAULT '', caption TEXT NOT NULL DEFAULT '', copyright TEXT NOT NULL DEFAULT '',
            creator TEXT NOT NULL DEFAULT '', location TEXT NOT NULL DEFAULT '',
            revision INTEGER NOT NULL DEFAULT 0, written_revision INTEGER NOT NULL DEFAULT 0,
            sidecar_hash TEXT NOT NULL DEFAULT '', initialized INTEGER NOT NULL DEFAULT 0 CHECK(initialized IN (0,1)),
            read_error TEXT NOT NULL DEFAULT '');
        INSERT INTO asset_metadata(asset_id) SELECT id FROM assets;
        CREATE TABLE asset_keywords(
            asset_id TEXT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
            position INTEGER NOT NULL CHECK(position BETWEEN 0 AND 255), keyword TEXT NOT NULL,
            PRIMARY KEY(asset_id,position), UNIQUE(asset_id,keyword));
        CREATE INDEX keyword_lookup ON asset_keywords(keyword COLLATE NOCASE,asset_id);
        CREATE TRIGGER initialize_asset_metadata AFTER INSERT ON assets BEGIN
            INSERT INTO asset_metadata(asset_id) VALUES(NEW.id);
        END;
        CREATE TRIGGER update_organization_metadata AFTER UPDATE OF rating,color_label ON assets
        WHEN OLD.rating<>NEW.rating OR COALESCE(OLD.color_label,'')<>COALESCE(NEW.color_label,'') BEGIN
            UPDATE asset_metadata SET revision=revision+1 WHERE asset_id=NEW.id;
        END;
        INSERT INTO schema_migrations(version) VALUES(16);
        PRAGMA user_version=16;
    )sql");
}
void Catalog::migrateMetadataInterop() {
    execute(R"sql(
        CREATE TABLE asset_creators(
            asset_id TEXT NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
            position INTEGER NOT NULL CHECK(position BETWEEN 0 AND 255),
            name TEXT NOT NULL CHECK(length(name)>0), PRIMARY KEY(asset_id,position));
        INSERT INTO asset_creators(asset_id,position,name)
            SELECT asset_id,0,creator FROM asset_metadata WHERE creator<>'';
        ALTER TABLE asset_metadata DROP COLUMN creator;
        ALTER TABLE asset_metadata ADD COLUMN xmp_label TEXT NOT NULL DEFAULT '';
        UPDATE asset_metadata SET xmp_label=(SELECT COALESCE(color_label,'') FROM assets WHERE id=asset_id);
        DROP TRIGGER update_organization_metadata;
        CREATE TRIGGER update_organization_metadata AFTER UPDATE OF rating,flag ON assets
        WHEN OLD.rating<>NEW.rating OR OLD.flag<>NEW.flag BEGIN
            UPDATE asset_metadata SET revision=revision+1 WHERE asset_id=NEW.id;
        END;
        CREATE TRIGGER update_color_label_metadata AFTER UPDATE OF color_label ON assets BEGIN
            UPDATE asset_metadata SET xmp_label=CASE COALESCE(NEW.color_label,'')
                WHEN 'red' THEN 'Red' WHEN 'yellow' THEN 'Yellow' WHEN 'green' THEN 'Green'
                WHEN 'blue' THEN 'Blue' WHEN 'purple' THEN 'Purple' ELSE '' END,
                revision=revision+1
            WHERE asset_id=NEW.id AND xmp_label<>CASE COALESCE(NEW.color_label,'')
                WHEN 'red' THEN 'Red' WHEN 'yellow' THEN 'Yellow' WHEN 'green' THEN 'Green'
                WHEN 'blue' THEN 'Blue' WHEN 'purple' THEN 'Purple' ELSE '' END;
        END;
        -- Previous schemas did not dirty XMP when a photo was rejected.
        UPDATE asset_metadata SET revision=revision+1 WHERE asset_id IN (SELECT id FROM assets WHERE flag=-1);
        INSERT INTO schema_migrations(version) VALUES(17);
        PRAGMA user_version=17;
    )sql");
}
MetadataSnapshot Catalog::metadataSnapshot(const std::string &id) const {
    execute("BEGIN;");
    try {
        Statement query(
            db_, "SELECT a.original_path,a.checksum,m.title,m.caption,m.copyright,m.location,"
                 "a.rating,a.flag,m.xmp_label,m.revision,m.written_revision,m.sidecar_hash,m."
                 "initialized,m.read_error "
                 "FROM assets a JOIN asset_metadata m ON m.asset_id=a.id WHERE a.id=?1");
        query.text(1, id);
        if (!query.row())
            throw std::runtime_error("Photo metadata is missing from the catalog.");
        MetadataSnapshot result;
        result.id = id;
        result.path = query.string(0);
        result.checksum = query.string(1);
        result.values.title = query.string(2);
        result.values.caption = query.string(3);
        result.values.copyright = query.string(4);
        result.values.location = query.string(5);
        result.values.rating = static_cast<int>(query.integer(6));
        result.values.flag = static_cast<int>(query.integer(7));
        result.values.label = query.string(8);
        result.revision = query.integer(9);
        result.writtenRevision = query.integer(10);
        result.sidecarHash = query.string(11);
        result.initialized = query.integer(12) != 0;
        result.readError = query.string(13);
        readLists(db_, id, result.values);
        result.values.validate();
        execute("COMMIT;");
        return result;
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void Catalog::saveMetadata(const std::string &id, std::int64_t revision,
                           const DescriptiveMetadata &values,
                           const std::optional<std::string> &adoptSidecarHash) {
    values.validate();
    if (revision < 0 ||
        (adoptSidecarHash && !adoptSidecarHash->empty() && adoptSidecarHash->size() != 64))
        throw std::runtime_error("Invalid metadata revision or sidecar identity.");
    execute("BEGIN IMMEDIATE;");
    try {
        Statement current(db_, "SELECT revision FROM asset_metadata WHERE asset_id=?1");
        current.text(1, id);
        if (!current.row() || current.integer(0) != revision)
            throw std::runtime_error("Metadata changed elsewhere. Reload before saving.");
        // Library triggers update revisions and canonical color names. Restore
        // the exact imported/custom label after them in the same transaction.
        writeOrganization(db_, id, values);
        Statement write(
            db_,
            "UPDATE asset_metadata SET title=?1,caption=?2,copyright=?3,xmp_label=?4,location=?5,"
            "revision=revision+1,initialized=1,read_error='',sidecar_hash=CASE WHEN ?6 THEN ?7 "
            "ELSE sidecar_hash END "
            "WHERE asset_id=?8");
        write.text(1, values.title);
        write.text(2, values.caption);
        write.text(3, values.copyright);
        write.text(4, values.label);
        write.text(5, values.location);
        write.integer(6, adoptSidecarHash.has_value());
        write.text(7, adoptSidecarHash.value_or(""));
        write.text(8, id);
        write.row();
        if (sqlite3_changes(db_) != 1)
            throw std::runtime_error("Metadata changed elsewhere. Reload before saving.");
        writeLists(db_, id, values);
        if (adoptSidecarHash) {
            Statement synced(
                db_, "UPDATE asset_metadata SET written_revision=revision WHERE asset_id=?1");
            synced.text(1, id);
            synced.row();
        }
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void Catalog::installSidecar(const MetadataSnapshot &snapshot, const std::string &hash,
                             const std::function<void()> &install) {
    execute("BEGIN IMMEDIATE;");
    try {
        Statement write(db_,
                        "UPDATE asset_metadata SET written_revision=revision,sidecar_hash=?1 "
                        "WHERE asset_id=?2 AND revision=?3 AND sidecar_hash=?4 AND EXISTS"
                        "(SELECT 1 FROM assets WHERE id=?2 AND original_path=?5 AND checksum=?6)");
        write.text(1, hash);
        write.text(2, snapshot.id);
        write.integer(3, snapshot.revision);
        write.text(4, snapshot.sidecarHash);
        write.text(5, snapshot.path.string());
        write.text(6, snapshot.checksum);
        write.row();
        if (sqlite3_changes(db_) != 1)
            throw std::runtime_error("Metadata changed before writing the sidecar.");
        install();
        execute("COMMIT;");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
std::vector<std::string> Catalog::pendingSidecars(const std::string &after, int limit) const {
    if (limit < 1 || limit > 256)
        throw std::runtime_error("Invalid sidecar page size.");
    Statement query(db_, "SELECT asset_id FROM asset_metadata WHERE asset_id>?1 AND "
                         "revision>written_revision ORDER BY asset_id LIMIT ?2");
    query.text(1, after);
    query.integer(2, limit);
    std::vector<std::string> ids;
    while (query.row())
        ids.push_back(query.string(0));
    return ids;
}
void detail::initializeMetadata(sqlite3 *db, const std::string &id,
                                const std::filesystem::path &source,
                                const PhotoMetadata &metadata) {
    if (!metadata.descriptive) {
        Statement warning(db, "UPDATE asset_metadata SET read_error=?1 WHERE asset_id=?2");
        warning.text(1, metadata.descriptiveError);
        warning.text(2, id);
        warning.row();
        return;
    }
    const auto &values = *metadata.descriptive;
    values.validate();
    writeOrganization(db, id, values);
    Statement write(
        db, "UPDATE asset_metadata SET title=?1,caption=?2,copyright=?3,xmp_label=?4,location=?5,"
            "sidecar_hash=?6,initialized=1,revision=0,written_revision=0,read_error='' WHERE "
            "asset_id=?7");
    write.text(1, values.title);
    write.text(2, values.caption);
    write.text(3, values.copyright);
    write.text(4, values.label);
    write.text(5, values.location);
    write.text(6, source == metadata.descriptiveSource ? metadata.sidecarHash : "");
    write.text(7, id);
    write.row();
    writeLists(db, id, values);
}
} // namespace luma
