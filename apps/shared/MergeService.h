#pragma once
#include "core/merge/HdrMerge.h"
#include "core/merge/PanoramaMerge.h"
#include <filesystem>
#include <string>
#include <vector>

namespace luma {
class MergeService {
  public:
    explicit MergeService(std::filesystem::path catalog) : catalog_(std::move(catalog)) {}
    std::filesystem::path hdr(const std::vector<std::string> &ids, HdrOptions options,
                              CancellationToken stop = {},
                              const std::function<void(double)> &progress = {}) const;
    std::filesystem::path panorama(const std::vector<std::string> &ids, PanoramaOptions options,
                                   CancellationToken stop = {},
                                   const std::function<void(double)> &progress = {}) const;

  private:
    std::filesystem::path catalog_;
};
} // namespace luma
