#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace luma {
struct DescriptiveMetadata {
    std::string title, caption, copyright, location;
    std::vector<std::string> creators, keywords;
    int rating = 0;
    int flag = 0;      // Library Reject (-1), Unflagged (0), Pick (1).
    std::string label; // Exact XMP text, not restricted to Library color tokens.
    std::string colorLabel() const;
    int xmpRating() const {
        return flag == -1 ? -1 : rating;
    }
    void validate() const;
    bool operator==(const DescriptiveMetadata &) const = default;
};
struct MetadataSnapshot {
    std::string id, checksum;
    std::filesystem::path path;
    DescriptiveMetadata values;
    std::int64_t revision = 0, writtenRevision = 0;
    std::string sidecarHash;
    bool initialized = false;
    std::string readError;
};
struct MetadataOverlay {
    DescriptiveMetadata values;
    bool includeText = false;
};
} // namespace luma
