#pragma once
#include "core/catalog/Catalog.h"

namespace luma {
class MetadataService {
  public:
    explicit MetadataService(std::filesystem::path catalog) : catalog_(std::move(catalog)) {}
    MetadataSnapshot read(const std::string &id) const;
    void importFromOriginal(const std::string &id, std::int64_t expectedRevision,
                            CancellationToken stop = {}) const;
    void save(const std::string &id, std::int64_t expectedRevision,
              const DescriptiveMetadata &values) const;
    std::filesystem::path writeSidecar(const std::string &id, CancellationToken stop = {}) const;
    void writePendingSidecars(CancellationToken stop = {}) const;
    static std::filesystem::path sidecarPath(const std::filesystem::path &source);
    static void readSourceMetadata(const std::filesystem::path &source, PhotoMetadata &metadata,
                                   CancellationToken stop = {});

  private:
    std::filesystem::path catalog_;
};
} // namespace luma
