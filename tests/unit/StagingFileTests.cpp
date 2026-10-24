#include "core/io/StagingFile.h"
#include <array>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string read(const std::filesystem::path &path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), {}};
}
void one(const std::filesystem::path &folder, const std::string &prefix) {
    std::filesystem::create_directory(folder);
    const auto foreign = folder / (prefix + "FOREIG"), link = folder / (prefix + "SYMLNK");
    std::ofstream(foreign) << "Preserve unmarked user bytes";
    std::filesystem::create_symlink(foreign, link);
    {
        luma::StagingFile active(folder, prefix);
        std::ofstream(active.path()) << "Live worker";
        require(luma::StagingFile::cleanup(folder, prefix) == 0 &&
                    std::filesystem::exists(active.path()),
                "Cleanup removed a live locked stage");
    }
    int pipeFD[2];
    require(pipe(pipeFD) == 0, "Cannot create child checkpoint pipe");
    const auto child = fork();
    require(child >= 0, "Cannot create owned crash child");
    if (child == 0) {
        close(pipeFD[0]);
        try {
            luma::StagingFile stage(folder, prefix);
            std::ofstream(stage.path()) << "Complete staged bytes";
            std::filesystem::create_hard_link(stage.path(), folder / "installed-output");
            if (prefix == ".luma-backup-")
                std::ofstream(stage.path().string() + "-journal") << "SQLite stage journal";
            const auto path = stage.path().string();
            if (write(pipeFD[1], path.data(), path.size()) != static_cast<ssize_t>(path.size()))
                _exit(3);
            close(pipeFD[1]);
            for (;;)
                pause();
        } catch (...) {
            _exit(2);
        }
    }
    struct Child {
        pid_t pid;
        ~Child() {
            if (pid > 0) {
                (void)kill(pid, SIGKILL);
                (void)waitpid(pid, nullptr, 0);
            }
        }
    } guard{child};
    close(pipeFD[1]);
    struct pollfd pollFD{pipeFD[0], POLLIN, 0};
    const int ready = poll(&pollFD, 1, 5000);
    std::array<char, 4096> buffer{};
    const auto length = ready > 0 ? ::read(pipeFD[0], buffer.data(), buffer.size()) : -1;
    close(pipeFD[0]);
    require(length > 0 && length < static_cast<ssize_t>(buffer.size()),
            "Crash child did not reach checkpoint");
    const auto path =
        std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(length)));
    require(luma::StagingFile::cleanup(folder, prefix) == 0 && std::filesystem::exists(path),
            "Cleanup deleted another process's active stage");
    require(kill(child, SIGKILL) == 0, "Cannot terminate owned staging child");
    int status = 0;
    require(waitpid(child, &status, 0) == child, "Cannot reap owned staging child");
    guard.pid = -1;
    require(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
            "Child was not killed at checkpoint");
    const auto retained = folder / "retained-stage";
    std::filesystem::rename(path, retained);
    std::ofstream(path) << "Replacement inode must survive";
    require(luma::StagingFile::cleanup(folder, prefix) == 0 &&
                read(path) == "Replacement inode must survive",
            "Cleanup deleted a replacement inode");
    std::filesystem::remove(path);
    std::filesystem::create_symlink(foreign, path);
    require(luma::StagingFile::cleanup(folder, prefix) == 0 && std::filesystem::is_symlink(path),
            "Cleanup followed a replacement staging symlink");
    std::filesystem::remove(path);
    std::filesystem::rename(retained, path);
    const auto record = std::filesystem::path(path.string() + ".lock");
    const auto moved = folder / (prefix + "RENAME");
    std::filesystem::rename(path, moved);
    std::filesystem::rename(record, moved.string() + ".lock");
    require(luma::StagingFile::cleanup(folder, prefix) == 0 &&
                read(moved) == "Complete staged bytes",
            "Cleanup accepted an ownership record for a different pathname");
    std::filesystem::rename(moved, path);
    std::filesystem::rename(moved.string() + ".lock", record);
    require(luma::StagingFile::cleanup(folder, prefix) == 1 && !std::filesystem::exists(path),
            "Crash stage was not cleaned after its kernel lock was released");
    require(read(folder / "installed-output") == "Complete staged bytes" &&
                read(foreign) == "Preserve unmarked user bytes" &&
                std::filesystem::is_symlink(link),
            "Cleanup changed installed, unmarked or symlinked files");
    if (prefix == ".luma-backup-")
        require(!std::filesystem::exists(path.string() + "-journal"),
                "Owned SQLite companion survived cleanup");
    require(luma::StagingFile::cleanup(folder, prefix) == 0, "Cleanup is not idempotent");
}
} // namespace
int main() {
    std::array<char, 32> pattern{};
    const std::string name = "/tmp/luma-staging-XXXXXX";
    std::copy(name.begin(), name.end(), pattern.begin());
    const auto created = mkdtemp(pattern.data());
    if (!created)
        return 2;
    const std::filesystem::path root(created);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{root};
    try {
        one(root / "xmp", ".luma-xmp-");
        one(root / "backup", ".luma-backup-");
        one(root / "derived", ".luma-derived-");
        std::cout << "PASS: live locks, real SIGKILL, exact markers, foreign files, links and "
                     "installed bytes\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
