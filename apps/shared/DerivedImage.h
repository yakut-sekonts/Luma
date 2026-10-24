#pragma once
#include "core/export/LinearOutput.h"
#include "core/jobs/Cancellation.h"
#include <filesystem>

namespace luma {
// Creates a uniquely named managed float DNG. No original/export destination can
// be overwritten. The returned file is durable before it is opened/imported.
std::filesystem::path createDerivedImage(const std::filesystem::path &catalog,
                                         const LinearOutput &image, std::string_view provenance,
                                         CancellationToken stop = {},
                                         const std::function<void(double)> &progress = {});
} // namespace luma
