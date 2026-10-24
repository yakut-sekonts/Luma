#pragma once
#include <cstdint>
#include <string>

namespace luma::macos {
struct SystemDiagnostics {
    std::string cpu, gpu;
    std::uint64_t physicalMemory = 0, residentMemory = 0;
    int logicalCores = 0;
    bool metalAvailable = false;
};
// No serial numbers, host names, user names or filesystem paths.
SystemDiagnostics systemDiagnostics();
} // namespace luma::macos
