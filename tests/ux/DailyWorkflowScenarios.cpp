#include "apps/shared/PhotoDecoder.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/FileHasher.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxTestIds.h"
#include <QColorSpace>
#include <QDir>
#include <QImage>
#include <QSignalSpy>
#include <algorithm>
#include <map>
#include <set>

namespace ux {
namespace {
using Ids = std::set<std::string>;
void idle(Context &c) {
    c.driver->wait(
        [&] {
            return !c.library.working() && !c.library.loading() && !c.library.selecting() &&
                   !c.batch.working();
        },
        "Daily Library operation settled", 60000);
    require(c.library.error().isEmpty() && c.batch.error().isEmpty(),
            c.library.error() + c.batch.error());
}
Ids visibleIDs(Context &c) {
    Ids result;
    c.driver->wait(
        [&] {
            result.clear();
            for (int i = 0; i < c.library.count(); ++i) {
                const auto id =
                    c.library.data(c.library.index(i), LibraryModel::AssetID).toString();
                if (id.isEmpty())
                    return false;
                result.insert(id.toStdString());
            }
            return !c.library.loading();
        },
        "Daily Library identity page loaded");
    require(result.size() == static_cast<std::size_t>(c.library.count()),
            "Daily Library contains empty or repeated identities");
    require(!result.contains(""), "Daily Library has an empty identity");
    return result;
}
void selectAll(Context &c, const Ids &expected) {
    c.driver->click("library.selectAll");
    idle(c);
    const auto selected = c.library.selectionIDs();
    require(Ids(selected.begin(), selected.end()) == expected,
            "Daily selection differs from the exact expected identities");
}
void expectVisible(Context &c, const Ids &expected, const QString &checkpoint) {
    c.driver->wait(
        [&] {
            return !c.library.loading() && c.library.count() == static_cast<int>(expected.size()) &&
                   visibleIDs(c) == expected;
        },
        checkpoint);
}
void search(Context &c, const QString &text, const Ids &expected) {
    c.driver->type(ids::search, text);
    c.driver->wait(
        [&] {
            return !c.library.loading() && c.library.count() == static_cast<int>(expected.size()) &&
                   visibleIDs(c) == expected;
        },
        "Daily search result");
    require(visibleIDs(c) == expected, "Daily search returned the wrong photos");
}
void chooseFilter(Context &c, const QString &selector, int index) {
    QSignalSpy reloads(&c.library, &LibraryModel::reloadStarted);
    c.driver->choose(selector, index);
    // Different filters can return identical photos. Wait for the debounced
    // reload itself before selecting; matching identities alone are not enough.
    c.driver->wait([&] { return !reloads.empty(); }, "Daily filter reload started");
    require(!reloads.front().front().toBool(), "Daily filter must reset the view");
    idle(c);
}
} // namespace

bool dailyCullingScenario(Context &c) {
    auto &d = *c.driver;
    require(c.library.count() == 0, "Daily culling requires a fresh owned catalog");
    c.import(100);
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto originals = catalog.libraryPage({}, 0, 101);
    require(originals.size() == 100, "Daily culling did not import exactly 100 originals");
    Ids all, picked, rejected;
    std::map<std::string, luma::EditDocument> expected;
    for (const auto &row : originals) {
        all.insert(row.id);
        if (row.filename.starts_with("Photo-000"))
            picked.insert(row.id);
        if (row.filename.starts_with("Photo-001"))
            rejected.insert(row.id);
        const auto asset = catalog.assetByID(row.id).value();
        require(asset.edits == luma::EditDocument{} && row.rating == 0 && row.flag == 0 &&
                    luma::macos::sha256File(row.path) == row.checksum,
                "Daily culling fixture is not neutral or original hashes differ");
        expected.emplace(row.id, asset.edits);
    }
    require(picked.size() == 10 && rejected.size() == 10 && all.size() == 100,
            "Daily culling fixture groups are not distinct");
    search(c, "Photo-000", picked);
    d.click(c.card(0));
    selectAll(c, picked);
    d.key(Qt::Key_3);
    idle(c);
    d.key(Qt::Key_P);
    idle(c);
    search(c, "Photo-001", rejected);
    d.click(c.card(0));
    selectAll(c, rejected);
    d.key(Qt::Key_1);
    idle(c);
    d.key(Qt::Key_X);
    idle(c);
    search(c, "", all);
    auto checkMetadata = [&] {
        for (const auto &row : catalog.libraryPage({}, 0, 101)) {
            require(row.rating == (picked.contains(row.id)     ? 3
                                   : rejected.contains(row.id) ? 1
                                                               : 0) &&
                        row.flag == (picked.contains(row.id)     ? 1
                                     : rejected.contains(row.id) ? -1
                                                                 : 0),
                    "Daily culling rating/flag changed the wrong photos");
        }
    };
    checkMetadata();
    chooseFilter(c, "library.ratingFilter", 3);
    expectVisible(c, picked, "Daily rating filter returned expected identities");
    chooseFilter(c, "libraryFlagFilter", 3);
    expectVisible(c, picked, "Daily combined flag/rating filter returned expected identities");
    selectAll(c, picked);
    d.click("library.newCollection");
    d.wait([&] { return d.object("library.collectionDialog")->property("opened").toBool(); },
           "Daily collection dialog opened");
    d.type("library.collectionName", "Daily picks");
    d.click("library.collectionConfirm");
    d.wait([&] { return !d.object("library.collectionDialog")->property("visible").toBool(); },
           "Daily collection dialog closed");
    idle(c);
    const auto collections = catalog.collections();
    require(collections.size() == 1 && collections.front().name == "Daily picks",
            "Daily collection was not durably created");
    const auto collection = collections.front().id;
    const auto selected = c.library.selectionIDs();
    require(Ids(selected.begin(), selected.end()) == picked,
            "Daily collection creation changed the selected photos");
    d.wait([&] { return d.item("libraryAddToCollection")->isEnabled(); },
           "Daily collection membership control ready");
    d.click("libraryAddToCollection");
    idle(c);
    luma::LibraryFilter collectionFilter;
    collectionFilter.collectionID = collection;
    const auto members = catalog.libraryIDs(collectionFilter);
    require(Ids(members.begin(), members.end()) == picked,
            "Daily collection membership differs from the exact selected photos");
    d.choose("library.ratingFilter", 0);
    d.choose("libraryFlagFilter", 0);
    idle(c);
    expectVisible(c, all, "Cleared filters restored all 100 originals");
    d.click("library.collection." + QString::fromStdString(collection));
    idle(c);
    expectVisible(c, picked, "Daily collection view returned expected identities");
    d.doubleClick(c.card(0));
    c.loaded();
    const auto developed = c.editor.photoID().toStdString();
    require(picked.contains(developed), "Develop opened a photo outside the collection");
    c.reveal(ids::exposure);
    d.drag(ids::exposure, d.sliderThumb(ids::exposure), {.65, .5});
    c.saved();
    expected.at(developed) = c.editor.currentSettings();
    require(expected.at(developed).exposure > 0 &&
                catalog.assetSnapshot(developed)->asset.edits == expected.at(developed),
            "Daily Develop edit was not persisted");
    d.click(ids::library);
    idle(c);
    expectVisible(c, picked, "Develop return retained the active collection");
    selectAll(c, picked);
    d.choose("batchPresetChoice", 1);
    const auto preset = c.presets.snapshot(c.presets.builtinItems()[1].toMap()["id"].toString());
    for (const auto &id : picked) {
        auto &document = expected.at(id);
        const auto merged =
            luma::mergeSettings(document, preset.settings, preset.groups, preset.parameters);
        require(merged != document, "Daily batch preset is a no-op");
        document = merged;
    }
    d.click("batchApplyPreset");
    idle(c);
    d.wait([&] { return !d.object("batch.wait")->property("visible").toBool(); },
           "Daily batch modal closed");
    for (const auto &[id, document] : expected)
        require(catalog.assetByID(id)->edits == document,
                "Daily batch lost an expected edit or changed an unselected photo");
    selectAll(c, picked);
    const auto destination = c.root + "/daily-exports";
    require(QDir().mkpath(destination), "Cannot create owned daily export destination");
    d.click("exportButton");
    d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
           "Daily Export opened");
    d.type("export.destination", destination);
    d.type("exportNaming", "{filename}");
    d.choose("exportFormat", 1);
    d.click("export.selection");
    d.wait([&] { return !c.exporter.working(); }, "Daily selection export completed", 60000);
    require(c.exporter.error().isEmpty() && c.exporter.failures().isEmpty() &&
                c.exporter.succeeded() == 10 && c.exporter.completed() == 10 &&
                c.exporter.skipped() == 0 && c.exporter.total() == 10,
            "Daily export did not finish all ten selected photos");
    require(QDir(destination).entryList(QDir::Files | QDir::Hidden).size() == 10,
            "Daily export left unexpected output or temporary files");
    for (const auto &row : originals) {
        require(luma::macos::sha256File(row.path) == row.checksum &&
                    catalog.assetByID(row.id)->edits == expected.at(row.id),
                "Daily workflow changed an original or the final saved document");
        if (!picked.contains(row.id))
            continue;
        const auto decoded = luma::PhotoDecoder().decode(row.path);
        const auto reference = luma::renderCPU(
            decoded.image, expected.at(row.id), decoded.metadata.orientation, {}, {},
            decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
        const QImage output(destination + '/' + QString::fromStdString(row.filename));
        const auto pixels = output.convertToFormat(QImage::Format_RGBA8888);
        require(!pixels.isNull() && pixels.size() == QSize(reference.width, reference.height) &&
                    output.colorSpace() == QColorSpace(QColorSpace::SRgb),
                "Daily export lost full dimensions or sRGB");
        for (int y = 0; y < pixels.height(); ++y)
            require(std::equal(pixels.constScanLine(y),
                               pixels.constScanLine(y) + pixels.width() * 4,
                               reference.rgba.data() + y * reference.width * 4),
                    "Daily exported pixels differ from the independent saved-document CPU oracle");
    }
    checkMetadata();
    const auto finalMembers = catalog.libraryIDs(collectionFilter);
    require(Ids(finalMembers.begin(), finalMembers.end()) == picked &&
                catalog.libraryCount() == 100,
            "Daily workflow changed catalog membership");
    c.checkData();
    return true;
}
} // namespace ux
