#include "core/catalog/Catalog.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
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
    require(rejected, "Invalid smart collection operation was accepted.");
}
luma::SmartRule condition(std::string field, std::string operation, luma::SmartValue value) {
    return {"condition", std::move(field), std::move(operation), std::move(value), {}};
}
struct Temporary {
    std::filesystem::path root;
    Temporary() {
        auto pattern = (std::filesystem::temp_directory_path() / "luma-smart-XXXXXX").string();
        require(mkdtemp(pattern.data()) != nullptr, "Cannot create smart collection fixture.");
        root = pattern;
    }
    ~Temporary() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};
void codecAndBounds() {
    auto rule = condition("filename", "contains", std::string("50%_' OR 1=1 --"));
    const auto encoded = luma::encodeSmartRule(rule);
    require(luma::encodeSmartRule(luma::decodeSmartRule(encoded)) == encoded,
            "Smart rule codec did not round-trip.");
    const auto sql = luma::compileSmartRule(rule);
    require(sql.expression.find("OR 1=1") == std::string::npos,
            "User text was interpolated into SQL.");
    require(std::get<std::string>(sql.values.at(0)) == "%50\\%\\_' OR 1=1 --%",
            "LIKE wildcards were not escaped as literals.");
    rejects([&] { luma::decodeSmartRule(encoded + "garbage"); });
    rejects(
        [&] { luma::compileSmartRule(condition("filename); DROP TABLE assets;--", "eq", 1.0)); });
    rejects([&] { luma::compileSmartRule(condition("rating", "contains", 2.0)); });
    rejects([&] { luma::compileSmartRule(condition("rating", "eq", std::string("2"))); });
    rejects([&] {
        luma::compileSmartRule(condition("iso", "gt", std::numeric_limits<double>::infinity()));
    });
    luma::SmartRule group;
    rejects([&] { group.validate(); });
    group.children.assign(17, condition("rating", "ge", 1.0));
    rejects([&] { group.validate(); });
    auto nested = condition("rating", "ge", 1.0);
    for (int i = 0; i < 9; ++i) {
        luma::SmartRule parent;
        parent.children = {nested};
        nested = std::move(parent);
    }
    rejects([&] { nested.validate(); });
}
void catalogQueriesAndBackup() {
    Temporary temporary;
    const auto path = temporary.root / "catalog.sqlite";
    luma::Catalog catalog(path);
    std::vector<std::string> ids;
    for (const auto &name : {"photo_50%.jpg", "photo_500.jpg", "other.jpg"}) {
        const auto source = temporary.root / name;
        std::ofstream(source) << name;
        luma::PhotoMetadata metadata;
        metadata.width = 100;
        metadata.height = 80;
        const auto asset =
            catalog.addAndSelect(source, std::string(64, 'a' + ids.size()), metadata);
        ids.push_back(asset.id);
    }
    catalog.setRating({ids[0]}, 5);
    catalog.setRating({ids[1]}, 3);
    auto metadata = catalog.metadataSnapshot(ids[0]);
    metadata.values.keywords = {"travel", "portrait"};
    catalog.saveMetadata(ids[0], metadata.revision, metadata.values);
    auto rule = condition("rating", "ge", 4.0);
    const auto collection = catalog.saveSmartCollection({}, "Best", 0, rule);
    luma::LibraryFilter filter;
    filter.collectionID = collection;
    require(catalog.libraryIDs(filter) == std::vector<std::string>{ids[0]},
            "Smart collection selected the wrong rating.");
    catalog.setRating({ids[1]}, 4);
    require(catalog.libraryCount(filter) == 2, "Smart collection did not track a metadata change.");
    require(catalog.libraryPage(filter, 1, 1).size() == 1,
            "Smart collection parameters collided with pagination parameters.");
    rejects([&] { catalog.collectionMembership(collection, {ids[2]}, true); });
    auto current = catalog.smartCollection(collection).value();
    catalog.saveSmartCollection(collection, "Literal percent", current.revision,
                                condition("filename", "contains", std::string("50%")));
    require(catalog.libraryIDs(filter) == std::vector<std::string>{ids[0]},
            "Literal percent matched wildcard filenames.");
    rejects([&] { catalog.saveSmartCollection(collection, "Stale", current.revision, rule); });
    current = catalog.smartCollection(collection).value();
    catalog.renameCollection(collection, "Renamed elsewhere");
    rejects(
        [&] { catalog.saveSmartCollection(collection, "Stale rename", current.revision, rule); });
    current = catalog.smartCollection(collection).value();
    catalog.saveSmartCollection(collection, "No travel", current.revision,
                                condition("keyword", "ne", std::string("travel")));
    const auto withoutTravel = catalog.libraryIDs(filter);
    require(withoutTravel.size() == 2 && std::find(withoutTravel.begin(), withoutTravel.end(),
                                                   ids[0]) == withoutTravel.end(),
            "Keyword negation selected an asset that has both matching and other keywords.");
    auto edits = catalog.assetByID(ids[0])->edits;
    edits.setParameter("presence.texture", 37);
    edits.setEnabled(luma::Correction::grain, false);
    catalog.saveEdits(ids[0], edits);
    const auto backup = temporary.root / "backup.sqlite";
    catalog.backupTo(backup);
    luma::Catalog::verifyBackup(backup);
    const auto restored = temporary.root / "restored.sqlite";
    luma::Catalog::restoreBackupTo(backup, restored);
    luma::Catalog reopened(restored, luma::CatalogOpenMode::existingOnly);
    require(reopened.assetByID(ids[0])->edits == edits, "Backup restore lost creative settings.");
    require(reopened.libraryIDs(filter) == withoutTravel, "Backup restore lost smart rules.");
    rejects([&] { luma::Catalog::restoreBackupTo(backup, restored); });
}
} // namespace
int main() {
    try {
        codecAndBounds();
        catalogQueriesAndBackup();
        std::cout << "Smart collection rules, queries, revisions and backup restore passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
