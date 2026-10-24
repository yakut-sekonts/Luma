#pragma once
#include "core/catalog/Catalog.h"

namespace luma {
struct RelinkSummary {
    std::size_t relinked = 0, unresolved = 0;
};
class RelinkService {
  public:
    explicit RelinkService(std::filesystem::path catalog) : catalog_(std::move(catalog)) {}
    void locate(const std::string &id, const std::filesystem::path &file,
                CancellationToken stop = {}) const;
    // oldRoot->newRoot preserves relative paths. Nearby mode additionally tries
    // the basename directly under newRoot; it never traverses unrelated folders.
    RelinkSummary locateFolder(const std::filesystem::path &oldRoot,
                               const std::filesystem::path &newRoot, bool nearby,
                               CancellationToken stop = {}) const;

  private:
    std::filesystem::path catalog_;
};
} // namespace luma
