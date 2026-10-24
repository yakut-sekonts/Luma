#pragma once
#include <filesystem>
#include <string_view>
namespace luma {
// A temporary inode with a separate locked ownership record. The record retains
// its exact path/device/inode; recovery preserves unknown files and live workers.
class StagingFile final {
  public:
    StagingFile(const std::filesystem::path &folder, std::string_view prefix);
    ~StagingFile();
    StagingFile(const StagingFile &) = delete;
    StagingFile &operator=(const StagingFile &) = delete;
    const std::filesystem::path &path() const {
        return path_;
    }
    int descriptor() const {
        return descriptor_;
    }
    static std::size_t cleanup(const std::filesystem::path &folder, std::string_view prefix);

  private:
    std::filesystem::path path_, lockPath_;
    int descriptor_ = -1, lockDescriptor_ = -1;
};
} // namespace luma
