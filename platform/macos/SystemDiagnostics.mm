#include "platform/macos/SystemDiagnostics.h"
#import <Metal/Metal.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <vector>

namespace luma::macos {
namespace {
template <class T> T number(const char *name) {
    T result{};
    std::size_t size = sizeof(result);
    if (sysctlbyname(name, &result, &size, nullptr, 0) != 0 || size != sizeof(result))
        return {};
    return result;
}
std::string text(const char *name) {
    std::size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0 || size > 4096)
        return {};
    std::vector<char> buffer(size + 1, 0);
    if (sysctlbyname(name, buffer.data(), &size, nullptr, 0) != 0)
        return {};
    return buffer.data();
}
} // namespace
SystemDiagnostics systemDiagnostics() {
    @autoreleasepool {
        SystemDiagnostics result;
        result.cpu = text("machdep.cpu.brand_string");
        result.physicalMemory = number<std::uint64_t>("hw.memsize");
        result.logicalCores = number<int>("hw.logicalcpu");
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        result.metalAvailable = device != nil;
        const char *gpuName = device.name.UTF8String;
        if (gpuName)
            result.gpu = gpuName;
        mach_task_basic_info_data_t info{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                      &count) == KERN_SUCCESS)
            result.residentMemory = info.resident_size;
        return result;
    }
}
} // namespace luma::macos
