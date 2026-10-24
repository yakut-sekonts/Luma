#pragma once
#include "core/ai/Providers.h"
#include <filesystem>
#include <memory>
namespace luma::macos {
std::shared_ptr<ai::ISegmentationProvider> makeCoreMLSky(const std::filesystem::path &path);
}
