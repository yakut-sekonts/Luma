#pragma once
#include "core/metadata/DescriptiveMetadata.h"
#include "core/metadata/PhotoMetadata.h"
struct sqlite3;
namespace luma::detail {
MetadataOverlay readMetadataOverlay(sqlite3 *db, const std::string &id);
void initializeMetadata(sqlite3 *db, const std::string &id, const std::filesystem::path &source,
                        const PhotoMetadata &metadata);
} // namespace luma::detail
