#include "apps/shared/MetadataService.h"
#include "core/image/Bayer.h"
#include "core/io/StagingFile.h"
#include "platform/macos/FileHasher.h"
#include "platform/macos/PhotoMetadataIO.h"
#ifdef LUMA_UX_PERSISTENCE_PROBE
#include "tests/ux/PersistenceProbe.h"
#endif
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <fcntl.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace luma {
namespace {
bool rawExtension(const std::filesystem::path &path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    static const std::set<std::string> extensions{".nef", ".dng", ".cr2", ".cr3",
                                                  ".arw", ".rw2", ".orf", ".raf"};
    return extensions.contains(extension);
}
struct Descriptor {
    int value;
    ~Descriptor() {
        if (value >= 0)
            close(value);
    }
};
std::vector<std::uint8_t> readBytes(const std::filesystem::path &path) {
    const Descriptor file{::open(path.c_str(), O_RDONLY | O_NOFOLLOW)};
    if (file.value < 0) {
        if (errno == ENOENT)
            return {};
        throw std::runtime_error("Cannot read XMP sidecar; symbolic links are not supported.");
    }
    struct stat info{};
    if (fstat(file.value, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 1 ||
        info.st_size > 8 * 1024 * 1024)
        throw std::runtime_error("XMP sidecar must be a regular file of at most 8 MiB.");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(info.st_size));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::read(file.value, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            throw std::runtime_error("XMP sidecar changed during reading.");
        offset += static_cast<std::size_t>(count);
    }
    return bytes;
}
struct Temporary {
    StagingFile file;
    const std::filesystem::path &path;
    Temporary(const std::filesystem::path &folder, std::span<const std::uint8_t> bytes,
              CancellationToken stop)
        : file(folder, ".luma-xmp-"), path(file.path()) {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            checkCancellation(stop);
            const auto count =
                ::write(file.descriptor(), bytes.data() + offset, bytes.size() - offset);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                throw std::runtime_error("Cannot write XMP sidecar.");
            offset += static_cast<std::size_t>(count);
        }
        if (fsync(file.descriptor()) != 0)
            throw std::runtime_error("Cannot sync XMP sidecar.");
    }
};

void syncFolder(const std::filesystem::path &path) {
    const Descriptor folder{::open(path.c_str(), O_RDONLY)};
    if (folder.value < 0 || fsync(folder.value) != 0)
        throw std::runtime_error("Cannot sync XMP folder.");
}
} // namespace
std::filesystem::path MetadataService::sidecarPath(const std::filesystem::path &source) {
    auto target = source;
    if (rawExtension(source)) {
        // Two RAWs with one basename cannot safely share a standard sidecar.
        for (const auto &entry : std::filesystem::directory_iterator(source.parent_path()))
            if (entry.path() != source && rawExtension(entry.path()) &&
                entry.path().stem() == source.stem())
                throw std::runtime_error(
                    "Multiple RAWs share this basename; XMP sidecar ownership is ambiguous.");
        target.replace_extension(".xmp");
    } else
        target += ".xmp";
    auto upper = target;
    upper.replace_extension(".XMP");
    if (std::filesystem::exists(upper) && !std::filesystem::exists(target))
        return upper;
    if (std::filesystem::exists(upper) && std::filesystem::exists(target) &&
        !std::filesystem::equivalent(upper, target))
        throw std::runtime_error("Both .xmp and .XMP sidecars exist. Resolve the ambiguity first.");
    return target;
}
MetadataSnapshot MetadataService::read(const std::string &id) const {
    auto snapshot = Catalog(catalog_).metadataSnapshot(id);
    // Populate the first editing draft from the original. Saving is explicit;
    // opening this panel alone does not change catalog metadata or a sidecar.
    if (!snapshot.initialized && std::filesystem::is_regular_file(snapshot.path)) {
        const auto rating = snapshot.values.rating;
        const auto flag = snapshot.values.flag;
        const auto label = snapshot.values.label;
        try {
            const auto bytes = readBytes(sidecarPath(snapshot.path));
            snapshot.values = macos::readDescriptiveMetadata(snapshot.path, bytes);
            snapshot.values.rating = rating;
            snapshot.values.flag = flag;
            snapshot.values.label = label;
            snapshot.readError.clear();
        } catch (const std::exception &error) {
            snapshot.readError = error.what();
        }
    }
    return snapshot;
}
void MetadataService::readSourceMetadata(const std::filesystem::path &source,
                                         PhotoMetadata &metadata, CancellationToken stop) {
    try {
        checkCancellation(stop);
        const auto path = sidecarPath(source);
        const auto bytes = readBytes(path);
        const auto hash = bytes.empty() ? "" : macos::sha256File(path, stop);
        const auto values = macos::readDescriptiveMetadata(source, bytes);
        if (readBytes(path) != bytes)
            throw std::runtime_error("XMP changed during metadata reading.");
        metadata.descriptive = values;
        metadata.descriptiveSource = std::filesystem::canonical(source);
        metadata.sidecarHash = hash;
        metadata.descriptiveError.clear();
    } catch (const std::exception &error) {
        checkCancellation(stop);
        // An unsupported IPTC/XMP property must not prevent pixel import. Keep
        // the problem visible; never overwrite the external packet on this path.
        metadata.descriptive.reset();
        metadata.descriptiveError = error.what();
    }
}
void MetadataService::writePendingSidecars(CancellationToken stop) const {
    Catalog database(catalog_);
    std::string after, firstError;
    for (;;) {
        checkCancellation(stop);
        const auto ids = database.pendingSidecars(after);
        if (ids.empty())
            break;
        for (const auto &id : ids) {
            try {
                writeSidecar(id, stop);
            } catch (const std::exception &error) {
                checkCancellation(stop);
                if (firstError.empty())
                    firstError = error.what();
            }
        }
        after = ids.back();
    }
    if (!firstError.empty())
        throw std::runtime_error("Some XMP sidecars could not be written: " + firstError);
}
void MetadataService::save(const std::string &id, std::int64_t expectedRevision,
                           const DescriptiveMetadata &values) const {
    Catalog(catalog_).saveMetadata(id, expectedRevision, values);
}
void MetadataService::importFromOriginal(const std::string &id, std::int64_t expectedRevision,
                                         CancellationToken stop) const {
    Catalog database(catalog_);
    const auto snapshot = database.metadataSnapshot(id);
    if (snapshot.revision != expectedRevision)
        throw std::runtime_error("Metadata changed elsewhere. Reload before importing.");
    if (macos::sha256File(snapshot.path, stop) != snapshot.checksum)
        throw std::runtime_error("Original changed; metadata import refused.");
    const auto path = sidecarPath(snapshot.path);
    const auto bytes = readBytes(path);
    const auto hash = bytes.empty() ? "" : macos::sha256File(path, stop);
    const auto values = macos::readDescriptiveMetadata(snapshot.path, bytes);
    if (readBytes(path) != bytes || macos::sha256File(snapshot.path, stop) != snapshot.checksum)
        throw std::runtime_error("Original or XMP changed during metadata import.");
    checkCancellation(stop);
    database.saveMetadata(id, expectedRevision, values, hash);
}
std::filesystem::path MetadataService::writeSidecar(const std::string &id,
                                                    CancellationToken stop) const {
    Catalog database(catalog_);
    const auto snapshot = database.metadataSnapshot(id);
    if (macos::sha256File(snapshot.path, stop) != snapshot.checksum)
        throw std::runtime_error("Original changed; XMP write refused.");
    const auto path = sidecarPath(snapshot.path);
    const auto bytes = readBytes(path);
    const auto oldHash = bytes.empty() ? "" : macos::sha256File(path, stop);
    auto values = snapshot.values;
    if (!snapshot.initialized) {
        // Rating/label can change before the Metadata panel has been used.
        // Preserve descriptive fields from the source rather than blanking them.
        values = macos::readDescriptiveMetadata(snapshot.path, bytes);
        values.rating = snapshot.values.rating;
        values.flag = snapshot.values.flag;
        values.label = snapshot.values.label;
    }
    const auto output = macos::writeDescriptiveXMP(values, bytes);
    // A previous file install can survive a crash before its DB commit. An
    // identical desired packet may be acknowledged without rewriting the file.
    if (oldHash != snapshot.sidecarHash && bytes != output)
        throw std::runtime_error("XMP changed outside Luma. Import its metadata before writing; "
                                 "catalog edits are preserved.");
    Temporary staged(path.parent_path(), output, stop);
    const auto hash = macos::sha256File(staged.path, stop);
    if (!bytes.empty()) {
        const auto backups =
            catalog_.parent_path() / (catalog_.filename().string() + ".luma-xmp-backups");
        std::filesystem::create_directories(backups);
        const auto backup = backups / (oldHash + ".xmp");
        if (!std::filesystem::exists(backup)) {
            Temporary saved(backups, bytes, stop);
            if (link(saved.path.c_str(), backup.c_str()) != 0 && errno != EEXIST)
                throw std::runtime_error("Cannot preserve the existing XMP packet.");
            syncFolder(backups);
        }
        if (macos::sha256File(backup, stop) != oldHash)
            throw std::runtime_error("Existing XMP backup has changed.");
    }
    database.installSidecar(snapshot, hash, [&] {
        checkCancellation(stop);
        if (readBytes(path) != bytes)
            throw std::runtime_error("XMP changed while a write was being prepared.");
        if (bytes == output)
            return;
#ifdef LUMA_UX_PERSISTENCE_PROBE
        ux::observePersistenceCheckpoint(catalog_, "xmp.before-install", path, staged.path);
#endif
        const int installed = bytes.empty() ? link(staged.path.c_str(), path.c_str())
                                            : rename(staged.path.c_str(), path.c_str());
        if (installed != 0)
            throw std::runtime_error("Cannot install XMP sidecar.");
        syncFolder(path.parent_path());
#ifdef LUMA_UX_PERSISTENCE_PROBE
        ux::observePersistenceCheckpoint(catalog_, "xmp.after-install", path, staged.path);
#endif
    });
    return path;
}
} // namespace luma
