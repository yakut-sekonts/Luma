#include "apps/shared/LibraryService.h"
#include "platform/macos/FileHasher.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <typename F> void rejects(F operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Invalid import option was accepted.");
}
struct Temporary {
    std::filesystem::path root;
    Temporary() {
        auto pattern =
            (std::filesystem::temp_directory_path() / "luma-advanced-import-XXXXXX").string();
        require(mkdtemp(pattern.data()) != nullptr, "Cannot create import fixture.");
        root = pattern;
    }
    ~Temporary() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};
void optionsAndNaming() {
    luma::ImportOptions options;
    options.mode = luma::ImportMode::move;
    options.destination = "/tmp/Photos";
    options.backupDestination = "/tmp/Backup";
    options.dateFolders = true;
    options.startedAt = 1609459200;
    options.naming = "{date}-{seq}-{name}";
    options.metadata.copyright = "Author";
    options.metadata.keywords = {"Travel"};
    options.develop.emplace();
    options.develop->setParameter("presence.texture", 45);
    const auto encoded = luma::encodeImportOptions(options);
    const auto decoded = luma::decodeImportOptions(encoded);
    require(luma::encodeImportOptions(decoded) == encoded && decoded.develop == options.develop,
            "Import snapshot did not retain its options and creative settings.");
    require(luma::importRelativePath("{date}.JPG", {}, 1, options) ==
                "2021/01/01/2021-01-01-000002-{date}.JPG",
            "Date fallback, sequence, or literal source-name braces changed.");
    require(luma::importRelativePath("photo.jpg", 0, 0, options) ==
                "1970/01/01/1970-01-01-000001-photo.jpg",
            "Capture date did not override the session date.");
    rejects([&] { luma::decodeImportOptions(encoded + "extra"); });
    rejects([&] { luma::importRelativePath("photo.jpg", {}, -1, options); });
    for (const auto &name : {"../{name}", "{unknown}", ".hidden", "{name}:bad"}) {
        options.naming = name;
        rejects([&] { options.validate(); });
    }
}
void addExistingLongNames(const std::filesystem::path &fixture) {
    Temporary temporary;
    std::string unicodeName;
    for (int index = 0; index < 124; ++index)
        unicodeName += "ф";
    const std::vector<std::string> names{std::string(250, 'a') + ".webp", unicodeName + ".webp"};
    const auto checksum = luma::macos::sha256File(fixture);
    const auto catalogPath = temporary.root / "catalog.sqlite";
    luma::LibraryService service(catalogPath);
    luma::ImportOptions options;
    options.skipDuplicates = false;
    for (const auto &name : names) {
        const auto source = temporary.root / name;
        std::filesystem::copy_file(fixture, source);
        const auto added = service.import({source}, options);
        require(added.added == 1 && added.failed == 0,
                "Add rejected a legal existing filename above the generated-name limit.");
        require(luma::macos::sha256File(source) == checksum,
                "Add changed the existing original bytes.");
    }
    {
        const luma::Catalog catalog(catalogPath);
        require(catalog.libraryCount() == static_cast<std::int64_t>(names.size()),
                "Add lost a long-name source.");
        for (const auto &id : catalog.libraryIDs({})) {
            const auto asset = catalog.assetByID(id).value();
            require(asset.path.parent_path() == std::filesystem::canonical(temporary.root) &&
                        std::find(names.begin(), names.end(), asset.path.filename().string()) !=
                            names.end() &&
                        asset.checksum == checksum,
                    "Reopened catalog changed a long source name, location or checksum.");
        }
    }
    options.destination = temporary.root / "copies";
    std::filesystem::create_directory(options.destination);
    options.backupDestination = temporary.root / "backup";
    std::filesystem::create_directory(options.backupDestination);
    const auto source = temporary.root / names.front();
    for (const auto mode :
         {luma::ImportMode::add, luma::ImportMode::copy, luma::ImportMode::move}) {
        options.mode = mode;
        const auto rejected = service.import({source}, options);
        require(rejected.added == 0 && rejected.failed == 1 &&
                    std::filesystem::is_empty(options.destination) &&
                    std::filesystem::is_empty(options.backupDestination) &&
                    luma::macos::sha256File(source) == checksum,
                "Generated Copy/Move/backup names bypassed validation or changed the original.");
    }
    require(service.recover().empty(), "Long-name imports left an unfinished journal.");
}
void copyMoveAndBackup(const std::filesystem::path &fixture) {
    Temporary temporary;
    const auto source = temporary.root / "source.webp";
    std::filesystem::copy_file(fixture, source);
    const auto checksum = luma::macos::sha256File(source);
    const auto catalogPath = temporary.root / "catalog.sqlite";
    luma::LibraryService service(catalogPath);
    luma::ImportOptions options;
    options.mode = luma::ImportMode::copy;
    options.destination = temporary.root / "photos";
    options.backupDestination = temporary.root / "backup";
    std::filesystem::create_directory(options.destination);
    std::filesystem::create_directory(options.backupDestination);
    options.metadata.title = "Imported title";
    options.metadata.keywords = {"Imported keyword"};
    options.develop.emplace();
    options.develop->setParameter("presence.clarity", 23);
    const auto copied = service.import({source}, options);
    require(copied.added == 1 && copied.failed == 0 && std::filesystem::exists(source),
            "Copy failed or removed its original.");
    const auto target = options.destination / "source.webp";
    const auto backup = options.backupDestination / "source.webp";
    require(luma::macos::sha256File(target) == checksum &&
                luma::macos::sha256File(backup) == checksum,
            "Published original and secondary backup differ from the source.");
    {
        luma::Catalog catalog(catalogPath);
        const auto asset = catalog.assetByID(catalog.libraryIDs({}).at(0)).value();
        require(asset.edits == *options.develop,
                "Import did not apply the frozen develop settings.");
        const auto metadata = catalog.metadataSnapshot(asset.id).values;
        require(metadata.title == options.metadata.title &&
                    metadata.keywords == options.metadata.keywords,
                "Import did not apply the metadata preset atomically.");
    }
    const auto duplicate = service.import({source}, options);
    require(duplicate.duplicates == 1 && duplicate.added == 0,
            "Duplicate import published another catalog asset.");
    const auto moveSource = temporary.root / "move.webp";
    std::filesystem::copy_file(fixture, moveSource);
    options.mode = luma::ImportMode::move;
    options.skipDuplicates = false;
    const auto moved = service.import({moveSource}, options);
    require(moved.added == 1 && moved.failed == 0 && !std::filesystem::exists(moveSource),
            "Move did not complete after publication and backup.");
    require(luma::macos::sha256File(options.destination / "move.webp") == checksum &&
                luma::macos::sha256File(options.backupDestination / "move.webp") == checksum,
            "Move removed its source without two verified copies.");
    const auto retainedSource = temporary.root / "retain.webp";
    std::filesystem::copy_file(fixture, retainedSource);
    const auto blocked = temporary.root / "not-a-directory";
    std::ofstream(blocked) << "sentinel";
    options.backupDestination = blocked;
    rejects([&] { service.import({retainedSource}, options); });
    require(std::filesystem::exists(retainedSource),
            "Invalid backup path removed the Move source.");
    require(luma::macos::sha256File(retainedSource) == checksum,
            "Failed Move changed its original bytes.");
    luma::Catalog catalog(catalogPath);
    require(catalog.libraryCount() == 2, "Failed or duplicate imports changed catalog membership.");
    require(service.recover().empty(), "Completed imports unexpectedly require recovery.");
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 2, "Expected a raster fixture path.");
        optionsAndNaming();
        addExistingLongNames(argv[1]);
        copyMoveAndBackup(argv[1]);
        std::cout << "Import options, Copy/Move, backups and metadata/develop snapshots passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
