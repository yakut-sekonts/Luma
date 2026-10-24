#pragma once
#include "core/ai/Embedding.h"
#include "core/edits/EditDocument.h"
#include "core/jobs/Cancellation.h"
#include "core/library/LibraryTypes.h"
#include "core/library/SmartCollection.h"
#include "core/metadata/DescriptiveMetadata.h"
#include "core/metadata/PhotoMetadata.h"
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>

struct sqlite3;
namespace luma {
struct AssetRecord {
    std::string id;
    std::filesystem::path path;
    std::string checksum;
    PhotoMetadata metadata;
    EditDocument edits;
    std::optional<MetadataOverlay> descriptive = std::nullopt;
};

struct AssetSnapshot {
    AssetRecord asset;
    std::int64_t revision = 0;
};

struct BatchEditResult {
    std::size_t changed = 0;
    std::optional<EditDocument> active;
};
struct DerivedImageRecord {
    std::filesystem::path path;
    std::string checksum;
    int width = 0, height = 0;
    std::int64_t ownerPID = 0;
};
struct FileRecord {
    std::string id;
    std::filesystem::path path;
    std::string checksum;
    std::int64_t size = 0;
};
struct RelinkRecord {
    FileRecord source;
    std::filesystem::path destination;
};
struct EditVersionInfo {
    std::string id, name, createdAt;
    std::int64_t revision = 0;
};
struct SmartCollectionRecord {
    SmartRule rule;
    std::int64_t revision = 0;
};
enum class CatalogOpenMode { createIfMissing, existingOnly };

/// Each instance owns a connection on the calling worker thread.
class Catalog {
  public:
    explicit Catalog(const std::filesystem::path &path,
                     CatalogOpenMode mode = CatalogOpenMode::createIfMissing);
    ~Catalog();
    Catalog(const Catalog &) = delete;
    Catalog &operator=(const Catalog &) = delete;
    AssetRecord addAndSelect(const std::filesystem::path &source, const std::string &checksum,
                             const PhotoMetadata &metadata);
    std::optional<AssetRecord> activeAsset() const;
    void saveEdits(const std::string &assetID, EditDocument edits);
    BatchEditResult applyBatchEdits(const std::vector<std::string> &ids, const EditDocument &source,
                                    std::uint32_t groups, CancellationToken stop = {},
                                    const std::function<void(double)> &progress = {},
                                    const std::string &activeID = {},
                                    std::span<const std::string> parameters = {});
    BatchEditResult restoreBatchEdits(bool redo, CancellationToken stop = {},
                                      const std::string &activeID = {});
    bool canUndoBatch() const {
        return batchState_ == BatchState::applied;
    }
    bool canRedoBatch() const {
        return batchState_ == BatchState::undone;
    }
    bool batchAffects(const std::string &id) const;
    void saveExposure(const std::string &assetID, double ev);
    std::optional<AssetRecord> assetByID(const std::string &id) const;
    // Owns a read transaction; use on a dedicated reader connection.
    std::optional<AssetSnapshot> assetSnapshot(const std::string &id);
    bool originalAtPath(const std::filesystem::path &canonicalPath,
                        CancellationToken stop = {}) const;
    void installExport(const std::filesystem::path &target, CancellationToken stop,
                       const std::function<void()> &install);
    std::optional<AssetRecord> duplicate(const std::string &checksum, std::int64_t size) const;
    std::int64_t libraryCount(const LibraryFilter &filter = {}) const;
    std::optional<std::int64_t> libraryIndex(const LibraryFilter &filter,
                                             const std::string &assetID) const;
    std::vector<LibraryItem> libraryPage(const LibraryFilter &filter, int offset, int limit) const;
    std::vector<std::string> libraryIDs(const LibraryFilter &filter, int offset = 0,
                                        int limit = 2147483647) const;
    void setRating(const std::vector<std::string> &ids, int rating);
    void setFlag(const std::vector<std::string> &ids, int flag);
    void setLabel(const std::vector<std::string> &ids, const std::string &label);
    std::optional<SmartCollectionRecord> smartCollection(const std::string &id) const;
    std::string saveSmartCollection(const std::string &id, const std::string &name,
                                    std::int64_t revision, const SmartRule &rule);
    std::vector<CollectionRecord> collections() const;
    std::string createCollection(const std::string &name);
    void renameCollection(const std::string &id, const std::string &name);
    void deleteCollection(const std::string &id);
    void collectionMembership(const std::string &collectionID, const std::vector<std::string> &ids,
                              bool add);
    std::string beginImport(ImportMode mode, const std::filesystem::path &destination,
                            const std::vector<std::filesystem::path> &sources,
                            std::int64_t ownerPID, bool skipDuplicates = true,
                            const std::map<std::filesystem::path, std::string> &sourceErrors = {},
                            const std::optional<ImportOptions> &options = std::nullopt);
    bool claimImport(const std::string &id, std::int64_t previousPID, std::int64_t newPID);
    void stageImport(const ImportJournalItem &item);
    void stageImportPlan(const ImportJournalItem &item);
    void finishImportMove(const ImportJournalItem &item, const std::string &state,
                          const std::string &error = {});
    bool referencesSource(const std::filesystem::path &path) const;
    bool commitImport(const ImportJournalItem &item, const PhotoMetadata &metadata,
                      bool skipDuplicates,
                      const std::optional<EditDocument> &develop = std::nullopt);
    void finishImportItem(const std::string &sessionID, int index, const std::string &state,
                          const std::string &error = "", const std::string &assetID = "");
    void finishImport(const std::string &sessionID, const std::string &state);
    std::vector<ImportJournal> unfinishedImports() const;
    ImportSummary importSummary(const std::string &sessionID) const;
    std::vector<std::string> importProblems(const std::string &sessionID) const;
    void verify() const;
    void backupTo(const std::filesystem::path &destination, CancellationToken stop = {},
                  const std::function<void(double)> &progress = {}) const;
    static void verifyBackup(const std::filesystem::path &source, CancellationToken stop = {});
    static void restoreBackupTo(const std::filesystem::path &source,
                                const std::filesystem::path &destination,
                                CancellationToken stop = {});
    std::vector<std::string> pendingSemanticImages(const std::string &provider,
                                                   const std::string &after, int limit = 64) const;
    void saveSemanticEmbedding(const std::string &id, const std::string &checksum,
                               const std::string &provider, std::span<const float> embedding);
    std::vector<ai::SemanticMatch> semanticSearch(const std::string &provider,
                                                  std::span<const float> embedding, int limit = 100,
                                                  CancellationToken stop = {}) const;
    void clearSemanticIndex();
    void stageDerived(const DerivedImageRecord &record);
    void finishDerived(const DerivedImageRecord &record);
    void discardDerived(const std::filesystem::path &path);
    std::vector<DerivedImageRecord> pendingDerived() const;
    MetadataSnapshot metadataSnapshot(const std::string &id) const;
    void saveMetadata(const std::string &id, std::int64_t revision,
                      const DescriptiveMetadata &values,
                      const std::optional<std::string> &adoptSidecarHash = {});
    void installSidecar(const MetadataSnapshot &snapshot, const std::string &hash,
                        const std::function<void()> &install);
    std::vector<std::string> pendingSidecars(const std::string &after = {}, int limit = 64) const;
    std::vector<FileRecord> fileRecords(const std::string &after = {}, int limit = 128) const;
    std::optional<FileRecord> fileRecord(const std::string &id) const;
    void relink(const std::vector<RelinkRecord> &records);
    std::vector<EditVersionInfo> editVersions(const std::string &assetID) const;
    std::string createEditVersion(const std::string &assetID, const std::string &name,
                                  const EditDocument &edits);
    EditDocument editVersion(const std::string &assetID, const std::string &id,
                             std::int64_t revision) const;
    void renameEditVersion(const std::string &assetID, const std::string &id, std::int64_t revision,
                           const std::string &name);
    void deleteEditVersion(const std::string &assetID, const std::string &id,
                           std::int64_t revision);

  private:
    enum class BatchState { none, applied, undone };
    BatchState batchState_ = BatchState::none;
    bool batchStorageConfigured_ = false;
    sqlite3 *db_ = nullptr;
    std::optional<EditDocument> batchActiveEdits(const std::string &id) const;
    void migrateSharpening();
    void migrateNoiseReduction();
    void migrateCrop();
    void migrateLens();
    void migrateTransform();
    void migrateLocalMasks();
    void migrateMaskComponents();
    void migrateBitmapMasks();
    void migrateRetouch();
    void migrateSemanticIndex();
    void migrateDerivedImages();
    void migrateMetadata();
    void migrateMetadataInterop();
    void migrateEditVersions();
    void migrateCreative();
    void migrateSmartCollections();
    void migrateImportPlans();
    void writeCreative(const std::string &assetID, const CreativeSettings &settings);
    void writeRetouch(const std::string &assetID, const std::vector<RetouchStroke> &strokes);
    void writeLocalMasks(const std::string &assetID, const std::vector<LocalMask> &masks);
    void writeEdits(const std::string &assetID, const EditDocument &edits);
    void execute(const char *sql) const;
    AssetRecord addAssetRecord(const std::filesystem::path &source, const std::string &checksum,
                               const PhotoMetadata &metadata, bool selectActive);
    void updateMetadata(const std::vector<std::string> &ids, const char *sql,
                        std::optional<int> number, const std::string &text = "");
};
} // namespace luma
