#pragma once
#include "core/ai/Providers.h"
#include <filesystem>
#include <memory>

namespace luma::macos {
// Loads only a local .mlmodelc with the explicit Luma linear RGB contract.
// Call on a worker: model loading can take time. No automatic network download.
std::shared_ptr<ai::IDenoiseProvider> makeCoreMLDenoise(const std::filesystem::path &model);
} // namespace luma::macos
