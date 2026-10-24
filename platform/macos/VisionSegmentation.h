#pragma once
#include "core/ai/Providers.h"
#include <memory>

namespace luma::macos {
std::shared_ptr<ai::ISegmentationProvider> makeVisionSegmentation();
} // namespace luma::macos
