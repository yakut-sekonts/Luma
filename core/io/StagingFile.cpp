#include "core/io/StagingFile.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
namespace luma {
namespace {
constexpr std::string_view magic = "LUMA-STAGING-V1\n";
void validPrefix(std::string_view prefix) {
    if (prefix != ".luma-xmp-" && prefix != ".luma-backup-" && prefix != ".luma-derived-")
        throw std::invalid_argument("Unknown staging file kind.");
}
bool sameInode(int descriptor, const std::filesystem::path &path) {
    struct stat opened{}, named{};
    return fstat(descriptor, &opened) == 0 && lstat(path.c_str(), &named) == 0 &&
           S_ISREG(named.st_mode) && opened.st_dev == named.st_dev && opened.st_ino == named.st_ino;
}
void removeCompanion(const std::filesystem::path &path, const char *suffix) {
    const auto name = path.string() + suffix;
    struct stat info{};
    if (lstat(name.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == getuid())
        (void)unlink(name.c_str());
}
struct Descriptor {
    int value;
    ~Descriptor() {
        if (value >= 0)
            close(value);
    }
    int release() {
        const int result = value;
        value = -1;
        return result;
    }
};
bool integer(std::string_view text, std::uint64_t &value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
bool identity(int lock, const std::filesystem::path &stage, std::uint64_t &device,
              std::uint64_t &inode) {
    struct stat info{};
    if (fstat(lock, &info) != 0 || info.st_size <= 0 || info.st_size > 8192)
        return false;
    std::array<char, 8192> bytes{};
    std::size_t offset = 0;
    while (offset < static_cast<std::size_t>(info.st_size)) {
        const auto size =
            pread(lock, bytes.data() + offset, static_cast<std::size_t>(info.st_size) - offset,
                  static_cast<off_t>(offset));
        if (size < 0 && errno == EINTR)
            continue;
        if (size <= 0)
            return false;
        offset += static_cast<std::size_t>(size);
    }
    std::string_view marker(bytes.data(), offset);
    const auto prefix = std::string(magic) + stage.string() + '\n';
    if (!marker.starts_with(prefix))
        return false;
    marker.remove_prefix(prefix.size());
    const auto separator = marker.find(':');
    return separator != marker.npos && integer(marker.substr(0, separator), device) &&
           integer(marker.substr(separator + 1), inode);
}
} // namespace
std::size_t StagingFile::cleanup(const std::filesystem::path &folder, std::string_view prefix) {
    validPrefix(prefix);
    std::size_t removed = 0;
    std::error_code error;
    auto entries = std::filesystem::directory_iterator(folder, error);
    if (error)
        return 0;
    for (const auto end = std::filesystem::directory_iterator{}; entries != end;
         entries.increment(error)) {
        if (error)
            break;
        const auto &entry = *entries;
        const auto name = entry.path().filename().string();
        if (!name.starts_with(prefix) || name.size() != prefix.size() + 6 + 5 ||
            !name.ends_with(".lock") ||
            !std::all_of(name.begin() + static_cast<std::ptrdiff_t>(prefix.size()), name.end() - 5,
                         [](unsigned char c) {
                             return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                    (c >= '0' && c <= '9');
                         }))
            continue;
        const auto lockPath = std::filesystem::absolute(entry.path()).lexically_normal();
        const auto stage = lockPath.parent_path() / name.substr(0, name.size() - 5);
        const Descriptor lock{
            ::open(lockPath.c_str(), O_RDWR | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)};
        struct stat info{};
        if (lock.value < 0 || fstat(lock.value, &info) != 0 || !S_ISREG(info.st_mode) ||
            info.st_uid != getuid() || flock(lock.value, LOCK_EX | LOCK_NB) != 0)
            continue;
        std::uint64_t device = 0, inode = 0;
        if (!identity(lock.value, stage, device, inode) || !sameInode(lock.value, lockPath))
            continue;
        struct stat staged{};
        if (lstat(stage.c_str(), &staged) == 0) {
            if (!S_ISREG(staged.st_mode) || staged.st_uid != getuid() ||
                static_cast<std::uint64_t>(staged.st_dev) != device || staged.st_ino != inode)
                continue;
            if (unlink(stage.c_str()) != 0)
                continue;
            ++removed;
        } else if (errno != ENOENT)
            continue;
        if (prefix == ".luma-backup-") {
            removeCompanion(stage, "-journal");
            removeCompanion(stage, "-wal");
            removeCompanion(stage, "-shm");
        }
        (void)unlink(lockPath.c_str());
    }
    return removed;
}
StagingFile::StagingFile(const std::filesystem::path &folder, std::string_view prefix) {
    validPrefix(prefix);
    (void)cleanup(folder, prefix);
    auto pattern = (std::filesystem::absolute(folder) / (std::string(prefix) + "XXXXXX"))
                       .lexically_normal()
                       .string();
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back(0);
    Descriptor file{mkstemp(name.data())};
    if (file.value < 0)
        throw std::runtime_error("Cannot create staging file.");
    bool lockCreated = false;
    try {
        path_ = name.data();
        lockPath_ = path_.string() + ".lock";
        Descriptor lock{
            ::open(lockPath_.c_str(), O_CREAT | O_EXCL | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600)};
        if (lock.value < 0)
            throw std::runtime_error("Cannot create staging ownership record.");
        lockCreated = true;
        if (flock(lock.value, LOCK_EX | LOCK_NB) != 0)
            throw std::runtime_error("Cannot lock staging ownership record.");
        struct stat info{};
        if (fstat(file.value, &info) != 0)
            throw std::runtime_error("Cannot identify staging inode.");
        const auto marker = std::string(magic) + path_.string() + '\n' +
                            std::to_string(static_cast<std::uint64_t>(info.st_dev)) + ':' +
                            std::to_string(info.st_ino);
        std::size_t offset = 0;
        while (offset < marker.size()) {
            const auto size = ::write(lock.value, marker.data() + offset, marker.size() - offset);
            if (size < 0 && errno == EINTR)
                continue;
            if (size <= 0)
                throw std::runtime_error("Cannot write staging ownership record.");
            offset += static_cast<std::size_t>(size);
        }
        if (fsync(lock.value) != 0)
            throw std::runtime_error("Cannot sync staging ownership record.");
        (void)fcntl(file.value, F_SETFD, FD_CLOEXEC);
        descriptor_ = file.release();
        lockDescriptor_ = lock.release();
    } catch (...) {
        (void)unlink(name.data());
        if (lockCreated)
            (void)unlink(lockPath_.c_str());
        throw;
    }
}
StagingFile::~StagingFile() {
    if (descriptor_ < 0)
        return;
    if (sameInode(descriptor_, path_))
        (void)unlink(path_.c_str());
    close(descriptor_);
    if (sameInode(lockDescriptor_, lockPath_))
        (void)unlink(lockPath_.c_str());
    close(lockDescriptor_);
}
} // namespace luma
