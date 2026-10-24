#include "platform/macos/CrashCapture.h"
#import <Foundation/Foundation.h>
#import <PLCrashReport.h>
#import <PLCrashReportTextFormatter.h>
#import <PLCrashReporter.h>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <vector>

namespace luma::macos {
namespace {
constexpr NSUInteger maximumReportBytes = 8 * 1024 * 1024;
PLCrashReporter *__strong activeReporter;
PLCrashReporter *reporter(const std::filesystem::path &session) {
    auto *base = [NSString stringWithUTF8String:session.c_str()];
    if (base.length == 0)
        return nil;
    auto *configuration = [[PLCrashReporterConfig alloc]
                     initWithSignalHandlerType:PLCrashReporterSignalHandlerTypeMach
                         symbolicationStrategy:PLCrashReporterSymbolicationStrategyNone
        shouldRegisterUncaughtExceptionHandler:YES
                                      basePath:base
                                maxReportBytes:maximumReportBytes];
    return [[PLCrashReporter alloc] initWithConfiguration:configuration];
}
bool debuggerAttached() {
    int query[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    kinfo_proc process{};
    size_t size = sizeof(process);
    return sysctl(query, 4, &process, &size, nullptr, 0) != 0 ||
           (process.kp_proc.p_flag & P_TRACED);
}
} // namespace
bool enableCrashCapture(const std::filesystem::path &session) noexcept {
    try {
        @autoreleasepool {
            @try {
                if (activeReporter || debuggerAttached())
                    return false;
                auto *candidate = reporter(session);
                if (![candidate enableCrashReporterAndReturnError:nil])
                    return false;
                activeReporter = candidate; // Reporter lives until process termination.
                return true;
            } @catch (NSException *) {
                return false;
            }
        }
    } catch (...) {
        return false;
    }
}
bool formatCrashReport(const std::filesystem::path &session) noexcept {
    try {
        @autoreleasepool {
            @try {
                auto *source = reporter(session);
                const char *path = source.crashReportPath.fileSystemRepresentation;
                struct stat info{};
                if (!path || lstat(path, &info) != 0 || !S_ISREG(info.st_mode) ||
                    info.st_size < 1 || info.st_size > static_cast<off_t>(maximumReportBytes))
                    return false;
                const auto destination = session / "crash.txt";
                struct stat existing{};
                if (lstat(destination.c_str(), &existing) == 0)
                    return S_ISREG(existing.st_mode) && existing.st_size > 0 &&
                           existing.st_size <= static_cast<off_t>(maximumReportBytes);
                if (errno != ENOENT)
                    return false;
                NSData *bytes = [source loadPendingCrashReportDataAndReturnError:nil];
                if (!bytes || bytes.length > maximumReportBytes)
                    return false;
                auto *report = [[PLCrashReport alloc] initWithData:bytes error:nil];
                if (!report)
                    return false; // Preserve the original even if parsing failed.
                auto *text = [PLCrashReportTextFormatter
                    stringValueForCrashReport:report
                               withTextFormat:PLCrashReportTextFormatiOS];
                NSData *encoded = [text dataUsingEncoding:NSUTF8StringEncoding];
                if (!encoded || encoded.length == 0 || encoded.length > maximumReportBytes)
                    return false;
                const auto pattern = (session / ".crash-XXXXXX").string();
                std::vector<char> temporary(pattern.begin(), pattern.end());
                temporary.push_back('\0');
                const int fd = mkstemp(temporary.data());
                if (fd < 0)
                    return false;
                struct TemporaryFile {
                    int fd;
                    const char *path;
                    ~TemporaryFile() {
                        close(fd);
                        unlink(path);
                    }
                } closeFile{fd, temporary.data()};
                if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0)
                    return false;
                std::size_t offset = 0;
                while (offset < encoded.length) {
                    const auto count = write(fd, static_cast<const char *>(encoded.bytes) + offset,
                                             encoded.length - offset);
                    if (count < 0 && errno == EINTR)
                        continue;
                    if (count <= 0)
                        return false;
                    offset += static_cast<std::size_t>(count);
                }
                if (fsync(fd) != 0)
                    return false;
                // Install only a complete file, without replacing an existing report.
                return link(temporary.data(), destination.c_str()) == 0;
            } @catch (NSException *) {
                return false;
            }
        }
    } catch (...) {
        return false;
    }
}
} // namespace luma::macos
