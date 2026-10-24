#pragma once
#include "core/edits/EditDocument.h"
#include "core/metadata/DescriptiveMetadata.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace luma {
struct LibraryFilter {
    int minimumRating = 0;
    int flag = -2; // -2: any, -1: Reject, 0: Unflagged, 1: Pick.
    std::optional<std::string> label;
    std::string collectionID;
    std::string search;
    void validate() const;
};
struct LibraryItem {
    std::string id;
    std::filesystem::path path;
    std::string filename;
    std::string checksum;
    int width = 0, height = 0, orientation = 0, rating = 0, flag = 0;
    std::string label, camera;
    bool missing = false;
};
struct CollectionRecord {
    std::string id, name;
    std::int64_t count = 0;
    bool smart = false;
};
enum class ImportMode : std::uint8_t { add, copy, move };
struct ImportOptions {
    ImportMode mode = ImportMode::add;
    std::filesystem::path destination;
    bool recursive = true;
    bool skipDuplicates = true;
    // Capture date in UTC; missing dates use the timestamp frozen at session start.
    bool dateFolders = false;
    std::string naming = "{name}";
    std::filesystem::path backupDestination{};
    DescriptiveMetadata metadata{};
    std::optional<EditDocument> develop = std::nullopt;
    std::int64_t startedAt = 0;
    void validate() const;
};
struct ImportSummary {
    std::string sessionID;
    std::int64_t added = 0, duplicates = 0, failed = 0, cancelled = 0;
};
struct ImportJournalItem {
    std::string sessionID;
    int index = 0;
    std::filesystem::path source, target, temporary;
    std::string checksum, state, error;
    std::int64_t size = 0;
    std::filesystem::path backup{}, sourceHold{};
    std::string moveState{};
    std::string metadataDocument{};
};
struct ImportJournal {
    std::string id, state;
    ImportMode mode = ImportMode::add;
    std::int64_t ownerPID = 0;
    std::filesystem::path destination;
    std::vector<ImportJournalItem> items;
    bool skipDuplicates = true;
    std::optional<ImportOptions> options = std::nullopt;
};
std::string encodeImportOptions(const ImportOptions &options);
ImportOptions decodeImportOptions(const std::string &document);
std::filesystem::path importRelativePath(const std::filesystem::path &source,
                                         std::optional<std::int64_t> capturedAt, int index,
                                         const ImportOptions &options);
DescriptiveMetadata importMetadata(DescriptiveMetadata original, const DescriptiveMetadata &preset);
void validateLabel(const std::string &label);
} // namespace luma
