#pragma once
#include <filesystem>

namespace luma::macos {
// Installs at most one process-wide reporter. Never called from acceptance runs.
bool enableCrashCapture(const std::filesystem::path &session) noexcept;
// Called only for an inactive session held under the desktop session lock.
bool formatCrashReport(const std::filesystem::path &session) noexcept;
} // namespace luma::macos
