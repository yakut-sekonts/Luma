#pragma once
#include "core/render/DevelopAccelerator.h"
#include <memory>

namespace luma::macos {
// Lazy initialization and all command execution happen on the calling worker.
std::shared_ptr<DevelopAccelerator> makeMetalDevelopAccelerator();
} // namespace luma::macos
