#include "core/catalog/Catalog.h"
#include "core/catalog/SqliteStatement.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxTestIds.h"
#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QQmlProperty>
#include <QSet>
#include <QTest>
#include <array>
#include <cmath>
#include <memory>
namespace ux {
namespace {
void idle(Context &c) {
    c.driver->wait(
        [&] { return !c.library.working() && !c.library.loading() && !c.library.selecting(); },
        "library settled");
    require(c.library.error().isEmpty(), c.library.error());
}
void collection(Context &c, const QString &name) {
    auto &d = *c.driver;
    d.click("library.newCollection");
    d.wait([&] { return d.object("library.collectionDialog")->property("opened").toBool(); },
           "collection editor opened");
    d.type("library.collectionName", name);
    d.click("library.collectionConfirm");
    d.wait([&] { return !d.object("library.collectionDialog")->property("visible").toBool(); },
           "collection dialog closed");
    d.wait([&] { return !c.library.collections().empty(); }, "collection persisted");
    idle(c);
}
} // namespace
bool libraryScenario(Context &c, const QString &id) {
    auto &d = *c.driver;
    if (id == "UX-LIB-001" || id == "UX-LIB-010" || id == "UX-LIB-011") {
        c.import(1000);
        const auto first = c.card(0);
        d.click(first);
        const auto selected = c.library.selectionIDs();
        require(selected.size() == 1, "Grid fixture has no unique selection");
        const auto active = d.object(first)->property("assetID").toString();
        if (id == "UX-LIB-011") {
            d.doubleClick(first);
            c.loaded();
            d.click("main.libraryButton");
            d.click("library.clearSelection");
        }
        auto *grid = d.item("library.grid");
        auto checkVisible = [&] {
            int count = 0;
            QSet<QString> identities;
            bool ready = true;
            std::function<void(QQuickItem *)> visit = [&](QQuickItem *parent) {
                for (auto *item : parent->childItems()) {
                    if (item->objectName().startsWith("library.card.")) {
                        const auto box = item->mapRectToItem(grid, item->boundingRect());
                        if (!item->isVisible() || !box.intersects(grid->boundingRect()))
                            continue;
                        ++count;
                        const auto asset = item->property("assetID").toString();
                        const auto row = item->property("index").toInt();
                        const auto expected =
                            c.library.data(c.library.index(row), LibraryModel::AssetID).toString();
                        ready = ready && !asset.isEmpty() && asset == expected &&
                                !identities.contains(asset);
                        identities.insert(asset);
                        auto *image = item->findChild<QQuickItem *>("photoThumbnail");
                        ready = ready && image && image->property("status").toInt() == 1;
                        if (id == "UX-LIB-011") {
                            const bool isActive = asset == active;
                            require(item->property("activePhoto").toBool() == isActive,
                                    "Active border role crossed assets");
                            require(QQmlProperty(item, "border.color").read().value<QColor>() ==
                                        QColor(isActive ? "#859ec1" : "#2a2f38"),
                                    "Grid rendered active border on wrong asset");
                        }
                    } else {
                        visit(item);
                    }
                }
            };
            visit(grid);
            return ready && count > 0;
        };
        d.wait(checkVisible, "initial visible grid thumbnails ready");
        auto *bar = d.object("library.scrollBar");
        for (const double fraction : {.25, .5, .75, 1., .75, .5, .25, 0.}) {
            const double size = bar->property("size").toDouble();
            const double position = bar->property("position").toDouble();
            d.drag("library.scrollBar", {.5, position + size / 2},
                   {.5, fraction * (1 - size) + size / 2});
            d.wait([&] { return !grid->property("moving").toBool(); }, "grid scroll settled");
            d.frame();
            d.wait(checkVisible, "scrolled visible grid thumbnails ready");
            d.log({{"action", "grid_scroll_checkpoint"},
                   {"fraction", fraction},
                   {"content_y", grid->property("contentY").toDouble()},
                   {"content_height", grid->property("contentHeight").toDouble()}});
            if (id != "UX-LIB-011")
                require(c.library.selectionIDs() == selected, "Far scroll lost the selected asset");
        }
        require(grid->property("contentY").toDouble() <= 1, "Grid did not return to the top");
        require(d.object(first)->property(id == "UX-LIB-011" ? "activePhoto" : "selected").toBool(),
                "Returned first card lost its identity state");
        return true;
    }

    if (id == "UX-SEL-007" || id == "UX-KEY-003" || id == "UX-FLT-001" || id == "UX-FLT-003" ||
        id == "UX-FLT-006" || id == "UX-FLT-008" || id == "UX-FLT-009") {
        c.import(100);
        if (id == "UX-FLT-006") {
            d.type(ids::search, QString(1024, 'x'));
            d.wait([&] { return !c.library.loading() && c.library.count() == 0; },
                   "long search finishes");
            require(d.object(ids::search)->property("text").toString().size() == 1024,
                    "Search input truncated unexpectedly");
        } else if (id == "UX-FLT-008") {
            for (int i = 0; i < 200; ++i)
                d.type(ids::search, i % 2 ? "Photo-000" : "Photo-009");
            d.wait([&] { return !c.library.loading() && c.library.count() == 10; },
                   "latest rapid filter wins");
            require(d.object(ids::search)->property("activeFocus").toBool(),
                    "Filtering stole search focus");
        } else {
            d.type(ids::search, "Photo-000");
            d.wait([&] { return !c.library.loading() && c.library.count() == 10; },
                   "realtime search result");
            if (id == "UX-SEL-007" || id == "UX-KEY-003") {
                d.click(c.card(0));
                require(!d.object(ids::search)->property("activeFocus").toBool(),
                        "Clicking a photo left keyboard focus in the search field");
                d.key(Qt::Key_A, Qt::ControlModifier);
                d.wait([&] { return !c.library.selecting(); }, "filtered select-all");
                require(c.library.selectedCount() == 10, "Select all included hidden rows");
                if (id == "UX-KEY-003") {
                    d.click("library.clearSelection");
                    require(c.library.selectedCount() == 0, "Clear selection failed");
                    d.key(Qt::Key_A, Qt::ControlModifier);
                    d.wait([&] { return !c.library.selecting(); },
                           "select-all from empty selection");
                    require(c.library.selectedCount() == 10,
                            "Cmd+A from empty selection did not select the filtered result");
                }
            }
        }
        if (id == "UX-FLT-003") {
            d.click(ids::search);
            d.key(Qt::Key_End);
            for (int i = 0; i < 9; ++i)
                d.key(Qt::Key_Backspace);
        } else
            d.type(ids::search, "");
        d.wait([&] { return !c.library.loading() && c.library.count() == 100; },
               "clear search restores all rows");
        return true;
    }
    if (id == "UX-SEL-010" || id == "UX-SEL-011" || id == "UX-KEY-005") {
        c.import(10);
        d.click(c.card(0));
        const auto asset = c.library.selectionIDs().front();
        const auto before =
            luma::Catalog((c.root + "/catalog.sqlite").toStdString()).libraryPage({}, 0, 10);
        d.type(ids::search, "");
        for (const auto key : {Qt::Key_P, Qt::Key_X, Qt::Key_U, Qt::Key_1})
            d.key(key);
        require(d.object(ids::search)->property("text").toString().toLower() == "pxu1",
                "Typing metadata keys was intercepted outside text input");
        d.wait([&] { return !c.library.loading() && c.library.count() == 0; }, "search typing");
        const auto after =
            luma::Catalog((c.root + "/catalog.sqlite").toStdString()).libraryPage({}, 0, 10);
        require(before.size() == after.size(), "Typing changed catalog rows");
        for (std::size_t i = 0; i < before.size(); ++i)
            require(before[i].rating == after[i].rating && before[i].flag == after[i].flag,
                    "Text typing triggered metadata shortcut");
        require(!asset.empty(), "Invalid selection fixture");
        if (id == "UX-SEL-010") {
            d.type(ids::search, "");
            d.wait([&] { return !c.library.loading() && c.library.count() == 10; },
                   "search cleared");
            d.click(c.card(0));
            require(!d.object(ids::search)->property("activeFocus").toBool(),
                    "Clicking a photo left keyboard focus in the search field");
            for (const auto &[key, flag] :
                 {std::pair{Qt::Key_P, 1}, std::pair{Qt::Key_X, -1}, std::pair{Qt::Key_U, 0}}) {
                d.key(key);
                d.wait(
                    [&] {
                        return !c.library.working() && !c.library.loading() &&
                               c.library.data(c.library.index(0), LibraryModel::Flag).toInt() ==
                                   flag;
                    },
                    "flag shortcut outside editable field");
                require(c.library.selectedCount() == 1, "Flag shortcut lost selection");
            }
        }
        return true;
    }
    if (id == "UX-FLT-004" || id == "UX-FLT-005") {
        QDir().mkpath(c.root + "/unicode");
        for (const auto &name : {QString::fromUtf8("Снимок.png"),
                                 QString::fromUtf8("Снимок-🌅.png"), QString("Plain.png")})
            require(QFile::copy(c.fixture + "/Photo-0000.png", c.root + "/unicode/" + name),
                    "Cannot create Unicode fixture");
        c.fixture = c.root + "/unicode";
        c.import(3);
        d.type(ids::search,
               id == "UX-FLT-004" ? QString::fromUtf8("Снимок") : QString::fromUtf8("🌅"));
        d.wait(
            [&] {
                return !c.library.loading() && c.library.count() == (id == "UX-FLT-004" ? 2 : 1);
            },
            "Unicode search");
        return true;
    }
    if (id == "UX-SEL-008" || id == "UX-SEL-009" || id == "UX-SEL-012") {
        c.import(10);
        d.click(c.card(0));
        if (id == "UX-SEL-008") {
            d.click("library.clearSelection");
            require(c.library.selectedCount() == 0 && !c.editor.hasPhoto(),
                    "Clear selection changed active photo");
        } else if (id == "UX-SEL-009") {
            for (int rating = 0; rating <= 5; ++rating) {
                d.key(static_cast<Qt::Key>(Qt::Key_0 + rating));
                d.wait(
                    [&] {
                        return !c.library.working() && !c.library.loading() &&
                               c.library.data(c.library.index(0), LibraryModel::Rating).toInt() ==
                                   rating;
                    },
                    "Rating shortcut reflected by model");
                require(c.library.selectedCount() == 1, "Rating lost selection");
            }
        } else {
            d.choose("library.labelChoice", 4);
            d.wait(
                [&] {
                    return !c.library.working() && !c.library.loading() &&
                           c.library.data(c.library.index(0), LibraryModel::Label).toString() ==
                               "green";
                },
                "Label reflected by model");
            require(c.library.selectedCount() == 1 &&
                        c.library.data(c.library.index(0), LibraryModel::Label).toString() ==
                            "green",
                    "Label mutation lost selection");
        }
        return true;
    }
    if (id == "UX-FLT-010" || id == "UX-FLT-012" || id == "UX-FLT-013") {
        c.import(10);
        collection(c, "Collection 🌅");
        auto records = luma::Catalog((c.root + "/catalog.sqlite").toStdString()).collections();
        require(records.size() == 1 && records[0].name == "Collection 🌅",
                "Collection creation did not persist exact name");
        if (id == "UX-FLT-010") {
            d.click("library.collection." + QString::fromStdString(records[0].id));
            idle(c);
            d.click("library.renameCollection");
            d.wait(
                [&] { return d.object("library.collectionDialog")->property("opened").toBool(); },
                "rename dialog");
            d.type("library.collectionName", "Renamed 🌅");
            d.click("library.collectionConfirm");
            idle(c);
            records = luma::Catalog((c.root + "/catalog.sqlite").toStdString()).collections();
            require(records.size() == 1 && records[0].name == "Renamed 🌅",
                    "Collection rename not durable");
        } else {
            d.click("library.collection." + QString::fromStdString(records[0].id));
            idle(c);
            d.click("library.deleteCollection");
            d.wait(
                [&] {
                    return d.object("library.deleteCollectionDialog")->property("opened").toBool();
                },
                "delete confirmation");
            d.click(id == "UX-FLT-012" ? "library.deleteCancel" : "library.deleteConfirm");
            idle(c);
            luma::Catalog catalog((c.root + "/catalog.sqlite").toStdString());
            require(catalog.libraryCount() == 10 &&
                        catalog.collections().size() == (id == "UX-FLT-012" ? 1U : 0U),
                    "Delete/cancel damaged assets or collection state");
            for (const auto &asset : catalog.libraryIDs({}))
                require(catalog.assetByID(asset).has_value(), "Collection deletion lost edits");
        }
        return true;
    }
    return false;
}

bool semanticScenario(Context &c, const QString &id) {
    if (id != "UX-P07-SEM-001")
        return false;
    auto &d = *c.driver;
    c.import(3);
    const auto selection = c.library.selectedAssetIDs();
    require(!c.library.semanticEnabled(),
            "Owned semantic fixture unexpectedly has an index enabled");
    d.click("semantic.open");
    d.wait([&] { return d.object("semantic.dialog")->property("opened").toBool(); },
           "Actual description search dialog opened");
    d.click("semantic.build");
    d.wait([&] { return !c.library.semanticBusy(); }, "Actual local semantic indexing completed",
           60000);
    require(c.library.semanticEnabled() && c.library.semanticError().isEmpty(),
            c.library.semanticError());
    require(c.library.semanticStatus().endsWith("errors: 0"),
            "Pinned photograph failed actual semantic indexing: " + c.library.semanticStatus());
    const auto storedCount = [&] {
        sqlite3 *raw = nullptr;
        const int opened = sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &raw,
                                           SQLITE_OPEN_READONLY, nullptr);
        const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection(raw, sqlite3_close);
        require(opened == SQLITE_OK, "Cannot inspect actual persisted semantic index");
        luma::detail::Statement statement(raw, "SELECT count(*) FROM semantic_embeddings");
        require(statement.row(), "Cannot count actual semantic embeddings");
        return statement.integer(0);
    };
    require(storedCount() == 3, "Actual index did not persist all three photo embeddings");
    const std::array<std::pair<QString, QString>, 3> queries{
        {{"a portrait of a person", "Portrait.png"},
         {"river and trees in a landscape", "Landscape.png"},
         {"a city at night", "Night.png"}}};
    for (const auto &[query, expected] : queries) {
        d.type("semantic.query", query);
        d.click("semantic.search");
        d.wait([&] { return !c.library.semanticBusy(); }, "Actual semantic query completed", 60000);
        require(c.library.semanticError().isEmpty(), c.library.semanticError());
        const auto matches = c.library.semanticResults();
        QJsonArray recorded;
        double prior = 2;
        for (const auto &row : matches) {
            const auto match = row.toMap();
            const double score = match.value("similarity").toDouble();
            require(std::isfinite(score) && score >= .15 && score <= 1.000001 && score <= prior,
                    "Semantic scores are not finite bounded descending matches");
            prior = score;
            recorded.append(QJsonObject::fromVariantMap(match));
        }
        d.log({{"action", "actual_semantic_query"},
               {"query", query},
               {"expected_first", expected},
               {"matches", recorded}});
        require(!matches.empty() &&
                    matches.front().toMap().value("filename").toString() == expected,
                "Actual semantic neighborhood did not rank " + expected + " first for " + query);
        const auto asset = matches.front().toMap().value("id").toString();
        d.wait(
            [&] {
                auto *item = d.find("semantic.result." + asset);
                return item && qobject_cast<QQuickItem *>(item)->isVisible();
            },
            "Expected semantic result is actually visible");
        d.screenshot(c.root + "/semantic-" + expected + ".png");
    }
    require(c.library.count() == 3 && c.library.selectedAssetIDs() == selection,
            "Semantic queries changed library selection or membership");
    // Normal opt-out must remove the persisted local index, not only hide results.
    d.click("semantic.disable");
    d.wait([&] { return !c.library.semanticBusy(); }, "Local semantic index deletion completed");
    require(!c.library.semanticEnabled() && c.library.semanticResults().empty() &&
                c.library.semanticError().isEmpty(),
            "Semantic opt-out retained results or error");
    require(storedCount() == 0, "Semantic opt-out did not delete persisted embeddings");
    require(!d.item("semantic.search")->isEnabled(), "Disabled semantic index still allows search");
    return true;
}

} // namespace ux
