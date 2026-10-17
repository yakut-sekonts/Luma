#include "tests/ux/UxContext.h"
#include <QSignalSpy>
#include <chrono>
#include <future>

namespace ux {
namespace {
void settled(Context &c) {
    c.driver->wait(
        [&] { return !c.library.loading() && !c.library.working() && !c.library.selecting(); },
        "filter/metadata work settled");
    require(c.library.error().isEmpty(), c.library.error());
    c.driver->frame();
}
void create(Context &c, const QString &name, bool success = true) {
    auto &d = *c.driver;
    d.click("library.newCollection");
    d.wait([&] { return d.object("library.collectionDialog")->property("opened").toBool(); },
           "collection validation dialog opened");
    d.type("library.collectionName", name);
    d.click("library.collectionConfirm");
    d.wait(
        [&] {
            return !d.object("library.collectionDialog")->property("visible").toBool() &&
                   !c.library.loading() && !c.library.working();
        },
        "collection validation completed");
    if (success)
        require(c.library.error().isEmpty(), c.library.error());
    else
        require(!c.library.error().isEmpty() && d.item("library.error")->isVisible() &&
                    !d.object("library.error")->property("text").toString().isEmpty(),
                "Invalid collection name has no visible error");
}
void apply(Context &c, const std::function<void()> &input, int count) {
    QSignalSpy resets(&c.library, &QAbstractItemModel::modelReset);
    input();
    c.driver->wait(
        [&] {
            return !resets.isEmpty() && !c.library.loading() && !c.library.working() &&
                   c.library.count() == count;
        },
        "new filter applied, including unchanged-count queries");
    settled(c);
}
} // namespace
bool filterExtraScenario(Context &c, const QString &id) {
    if (id != "UX-FLT-007" && id != "UX-FLT-011" && id != "UX-FLT-014" && id != "UX-FLT-015")
        return false;
    auto &d = *c.driver;
    c.import(10);
    if (id == "UX-FLT-011") {
        create(c, "Validation fixture");
        for (const auto &name : {QString(), QString("   "), QString("Validation fixture")}) {
            QString previous;
            for (int repeat = 0; repeat < 2; ++repeat) {
                create(c, name, false);
                const auto records = luma::Catalog(c.catalogFile.toStdString()).collections();
                require(records.size() == 1 && records.front().name == "Validation fixture",
                        "Invalid/duplicate name changed the collection set");
                if (repeat)
                    require(c.library.error() == previous,
                            "Repeated invalid name changed its error");
                previous = c.library.error();
            }
        }
        // Collection names use the catalog's case-sensitive UNIQUE constraint.
        create(c, "validation FIXTURE");
        create(c, "Valid retry");
        require(c.library.collections().size() == 3 && c.library.count() == 10,
                "Valid retry failed after collection validation errors");
        return true;
    }
    if (id == "UX-FLT-015") {
        d.resize({1000, 640});
        const QString name(128, QChar(0x0416));
        create(c, name);
        const auto records = luma::Catalog(c.catalogFile.toStdString()).collections();
        require(records.size() == 1 && QString::fromStdString(records.front().name) == name,
                "Long collection name was changed in storage");
        const auto selector = "library.collection." + QString::fromStdString(records.front().id);
        auto *button = d.item(selector);
        auto *scroll = d.item("library.collectionsScroll");
        const auto bounds = button->mapRectToItem(scroll, button->boundingRect());
        require(bounds.left() >= 0 && bounds.right() <= scroll->width() + 1,
                "Long collection button escaped its sidebar");
        bool elided = false;
        std::function<void(QQuickItem *)> visit = [&](QQuickItem *parent) {
            for (auto *child : parent->childItems()) {
                elided = elided || child->property("truncated").toBool();
                visit(child);
            }
        };
        visit(button);
        require(elided, "Long collection text has no rendered ellipsis");
        apply(c, [&] { d.click(selector); }, 0);
        d.click("library.renameCollection");
        d.wait([&] { return d.object("library.collectionDialog")->property("opened").toBool(); },
               "long collection remains renameable");
        require(d.object("library.collectionName")->property("text").toString() == name,
                "Elided collection lost its full editable name");
        d.click("library.collectionCancel");
        return true;
    }
    if (id == "UX-FLT-014") {
        auto work = std::async(std::launch::async, [path = c.catalogFile.toStdString()] {
            luma::Catalog database(path);
            for (int index = 0; index < 500; ++index)
                database.createCollection(
                    QString("Collection-%1").arg(index, 3, 10, QChar('0')).toStdString());
        });
        d.wait([&] { return work.wait_for(std::chrono::seconds(0)) == std::future_status::ready; },
               "500-collection fixture worker completed");
        work.get();
        c.library.refresh(); // Fixture refresh only; scrolling/selection below uses real input.
        d.wait([&] { return !c.library.loading() && c.library.collections().size() == 500; },
               "500 collections appeared in sidebar");
        settled(c);
        const auto records = luma::Catalog(c.catalogFile.toStdString()).collections();
        auto *view = d.item("library.collectionsScroll");
        auto *flick =
            qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(view->property("contentItem")));
        require(flick && flick->property("contentHeight").toDouble() > view->height(),
                "Large collection fixture has no scrollable content");
        c.metrics.reset();
        for (int repeat = 0; repeat < 3; ++repeat) {
            for (const bool bottom : {true, false}) {
                d.wheel("library.collectionsScroll", bottom ? -200000 : 200000);
                auto *button =
                    d.item("library.collection." +
                           QString::fromStdString(bottom ? records.back().id : records.front().id));
                d.wait(
                    [&] {
                        const auto bounds = button->mapRectToItem(view, button->boundingRect());
                        return !flick->property("moving").toBool() && bounds.top() >= -1 &&
                               bounds.bottom() <= view->height() + 1;
                    },
                    "collection scroll reached visible end control");
                QSignalSpy reset(&c.library, &QAbstractItemModel::modelReset);
                d.click(button->objectName());
                d.wait(
                    [&] {
                        return !reset.isEmpty() && !c.library.loading() && c.library.count() == 0;
                    },
                    "scrolled collection responds to selection");
            }
        }
        require(c.library.collections().size() == 500, "Collection scrolling lost rows");
        d.log({{"action", "collection_scroll_measurement"},
               {"collections", 500},
               {"metrics", c.metrics.json()},
               {"performance_mode", "MEASURE_ONLY"}});
        return true;
    }
    create(c, "Combined fixture");
    const auto collection = luma::Catalog(c.catalogFile.toStdString()).collections().front().id;
    d.click(c.card(0));
    d.click(c.card(1), Qt::ControlModifier);
    d.click(c.card(2), Qt::ControlModifier);
    const auto expected = c.library.selectionIDs();
    require(expected.size() == 3, "Combined filter fixture needs three selected photos");
    d.key(Qt::Key_5);
    settled(c);
    d.key(Qt::Key_P);
    settled(c);
    d.choose("library.labelChoice", 2);
    settled(c);
    d.click("libraryAddToCollection");
    settled(c);
    apply(c, [&] { d.click("library.collection." + QString::fromStdString(collection)); }, 3);
    apply(c, [&] { d.type("library.search", "Photo-000"); }, 3);
    apply(c, [&] { d.choose("library.ratingFilter", 4); }, 3);
    apply(c, [&] { d.choose("libraryFlagFilter", 3); }, 3);
    apply(c, [&] { d.choose("libraryLabelFilter", 2); }, 3);
    d.click("library.selectAll");
    settled(c);
    require(c.library.selectionIDs() == expected,
            "Combined filters returned different asset identities");
    // Each metadata field can remove a row from this combined result.
    d.click(c.card(0));
    apply(c, [&] { d.key(Qt::Key_0); }, 2);
    d.click(c.card(0));
    apply(c, [&] { d.key(Qt::Key_U); }, 1);
    d.click(c.card(0));
    apply(c, [&] { d.choose("library.labelChoice", 5); }, 0);
    return true;
}
} // namespace ux
