#include "tests/ux/UxContext.h"
#include <QColor>
#include <QQmlProperty>
#include <QSet>
#include <QSignalSpy>
#include <algorithm>
#include <cmath>

namespace ux {
namespace {
void idle(Context &c) {
    c.driver->wait(
        [&] { return !c.library.working() && !c.library.loading() && !c.library.selecting(); },
        "selection metadata/model settled");
    require(c.library.error().isEmpty(), c.library.error());
    c.driver->frame();
}
void scroll(Context &c, double fraction) {
    auto &d = *c.driver;
    auto *bar = d.item("library.scrollBar");
    auto *thumb =
        qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(bar->property("contentItem")));
    require(thumb, "Selection fixture scrollbar has no thumb");
    const auto center = thumb->mapToItem(bar, QPointF(thumb->width() / 2, thumb->height() / 2));
    const double travel = bar->property("availableHeight").toDouble() - thumb->height();
    require(travel > 0, "Selection fixture does not scroll");
    const double target =
        bar->property("topPadding").toDouble() + travel * fraction + thumb->height() / 2;
    d.drag("library.scrollBar", {center.x() / bar->width(), center.y() / bar->height()},
           {.5, target / bar->height()});
    auto *grid = d.item("library.grid");
    d.wait([&] { return !grid->property("moving").toBool(); }, "selection fixture scroll settled");
    d.frame();
    const double maximum = grid->property("contentHeight").toDouble() - grid->height();
    require(maximum > 0 && std::abs(grid->property("contentY").toDouble() / maximum - fraction) <=
                               2 / travel + 1e-4,
            "Selection fixture did not reach its requested scroll region");
}
QString collection(Context &c) {
    auto &d = *c.driver;
    d.click("library.newCollection");
    d.wait([&] { return d.object("library.collectionDialog")->property("opened").toBool(); },
           "membership collection dialog opened");
    d.type("library.collectionName", "Membership fixture");
    d.click("library.collectionConfirm");
    d.wait(
        [&] {
            return !d.object("library.collectionDialog")->property("visible").toBool() &&
                   !c.library.collections().isEmpty();
        },
        "membership collection created");
    idle(c);
    const auto records = luma::Catalog(c.catalogFile.toStdString()).collections();
    require(records.size() == 1 && records.front().name == "Membership fixture",
            "Membership collection was not persisted");
    return QString::fromStdString(records.front().id);
}
} // namespace
bool selectionScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-SEL-006", "UX-SEL-013", "UX-SEL-014",
                                         "UX-SEL-015", "UX-SEL-016", "UX-SEL-017",
                                         "UX-SEL-018", "UX-SEL-019", "UX-KEY-004"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    if (id == "UX-SEL-017" || id == "UX-SEL-018") {
        c.openFirst();
        const auto source = c.editor.sourcePath();
        const auto document = c.editor.currentSettings();
        QSignalSpy activation(&c.library, &LibraryModel::openRequested);
        d.click(c.card(1, true), id == "UX-SEL-017" ? Qt::NoModifier : Qt::ControlModifier);
        if (id == "UX-SEL-017") {
            d.wait([&] { return c.editor.sourcePath() != source && !c.editor.busy(); },
                   "single filmstrip activation loaded its own photo");
            c.loaded();
            require(activation.count() == 1, "Single filmstrip click activated more than once");
        } else {
            require(activation.count() == 0 && c.editor.sourcePath() == source &&
                        c.editor.currentSettings() == document && c.library.selectedCount() == 2,
                    "Modified filmstrip click activated or changed the current photo");
            d.click(c.card(3, true), Qt::ShiftModifier);
            require(activation.count() == 0 && c.editor.sourcePath() == source &&
                        c.editor.currentSettings() == document,
                    "Shift filmstrip selection activated another photo");
        }
        return true;
    }
    if (id == "UX-SEL-015") {
        c.openFirst();
        c.reveal("develop.exposureSlider");
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
        c.saved();
        d.click("main.libraryButton");
    } else
        c.import(id == "UX-SEL-013" || id == "UX-SEL-019" ? 1000 : 100);
    if (id == "UX-SEL-006") {
        d.click(c.card(3));
        d.type("library.search", "Photo-009");
        d.wait([&] { return !c.library.loading() && c.library.count() == 10; },
               "anchor filter changed the visible set");
        d.resize({1320, 1000});
        const auto target = c.card(6);
        const auto identity = d.object(target)->property("assetID").toString().toStdString();
        d.click(target, Qt::ShiftModifier);
        idle(c);
        const auto selected = c.library.selectionIDs();
        require(selected.size() == 1 && selected.front() == identity,
                "Shift selection reused the previous filter's numeric anchor");
        return true;
    }
    if (id == "UX-SEL-016") {
        const QStringList cards{c.card(0), c.card(1), c.card(2), c.card(3)};
        QSignalSpy activation(&c.library, &LibraryModel::openRequested);
        for (int index = 0; index < 100; ++index)
            d.click(cards[index % cards.size()]);
        const auto expected =
            d.object(cards[99 % cards.size()])->property("assetID").toString().toStdString();
        const auto selected = c.library.selectionIDs();
        require(selected.size() == 1 && selected.front() == expected && activation.isEmpty() &&
                    !c.editor.hasPhoto(),
                "Rapid single clicks lost final selection or caused unintended activation");
        return true;
    }
    if (id == "UX-SEL-013") {
        scroll(c, .6);
        auto *grid = d.item("library.grid");
        const int columns =
            static_cast<int>(std::lround(grid->width() / grid->property("cellWidth").toDouble()));
        const int firstFullRow =
            static_cast<int>(std::floor(grid->property("contentY").toDouble() /
                                        grid->property("cellHeight").toDouble())) +
            1;
        const int row = firstFullRow * columns;
        d.click(c.card(row));
        const auto selected = c.library.selectionIDs();
        const double before = grid->property("contentY").toDouble();
        for (const auto key : {Qt::Key_5, Qt::Key_P, Qt::Key_X, Qt::Key_U}) {
            d.key(key);
            idle(c);
            d.log({{"action", "metadata_scroll_result"},
                   {"key", static_cast<int>(key)},
                   {"before_content_y", before},
                   {"after_content_y", grid->property("contentY").toDouble()},
                   {"same_selection", c.library.selectionIDs() == selected}});
            require(std::abs(grid->property("contentY").toDouble() - before) <= 1 &&
                        c.library.selectionIDs() == selected,
                    "Rating/flag update reset Library scroll or selection");
        }
        return true;
    }
    if (id == "UX-SEL-019") {
        d.doubleClick(c.card(0));
        c.loaded();
        const auto active = c.editor.photoID();
        d.click("main.libraryButton");
        d.click(c.card(1), Qt::ControlModifier);
        const auto selected = c.library.selectionIDs();
        require(selected.size() == 2, "Recycled border fixture did not select two assets");
        scroll(c, .8);
        scroll(c, 0);
        std::function<void(QQuickItem *)> visit = [&](QQuickItem *parent) {
            for (auto *item : parent->childItems()) {
                if (item->objectName().startsWith("library.card.")) {
                    const auto asset = item->property("assetID").toString();
                    if (asset.isEmpty())
                        continue;
                    const bool chosen = std::find(selected.begin(), selected.end(),
                                                  asset.toStdString()) != selected.end();
                    const bool isActive = asset == active;
                    require(item->property("selected").toBool() == chosen &&
                                item->property("activePhoto").toBool() == isActive,
                            "A recycled card retained another asset's selection/active role");
                    const QColor expected(chosen ? "#dfae77" : isActive ? "#859ec1" : "#2a2f38");
                    require(QQmlProperty(item, "border.color").read().value<QColor>() == expected,
                            "A recycled card retained another asset's border color");
                }
                visit(item);
            }
        };
        d.frame();
        visit(d.item("library.grid"));
        return true;
    }
    if (id == "UX-KEY-004") {
        d.click(c.card(0));
        const auto chosen = c.library.selectionIDs();
        for (int rating = 0; rating <= 5; ++rating) {
            d.key(static_cast<Qt::Key>(Qt::Key_0 + rating));
            d.wait(
                [&] {
                    return !c.library.working() && !c.library.loading() &&
                           c.library.data(c.library.index(0), LibraryModel::Rating).toInt() ==
                               rating;
                },
                "rating shortcut acknowledged");
        }
        for (const auto &[key, flag] :
             {std::pair{Qt::Key_P, 1}, std::pair{Qt::Key_X, -1}, std::pair{Qt::Key_U, 0}}) {
            d.key(key);
            d.wait(
                [&] {
                    return !c.library.working() && !c.library.loading() &&
                           c.library.data(c.library.index(0), LibraryModel::Flag).toInt() == flag;
                },
                "flag shortcut acknowledged");
        }
        require(c.library.selectionIDs() == chosen && !c.editor.hasPhoto(),
                "Metadata shortcuts changed selection/activation");
        return true;
    }
    require(id == "UX-SEL-014" || id == "UX-SEL-015", "Unhandled selection scenario");
    d.click(c.card(0));
    d.click(c.card(1), Qt::ControlModifier);
    const auto selected = c.library.selectionIDs();
    require(selected.size() == 2, "Membership fixture selection is not exact");
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto first = catalog.assetByID(selected[0]);
    const auto second = catalog.assetByID(selected[1]);
    require(first && second, "Membership fixture assets missing");
    const auto total = catalog.libraryCount();
    const auto name = collection(c);
    d.click("libraryAddToCollection");
    idle(c);
    luma::LibraryFilter filter;
    filter.collectionID = name.toStdString();
    auto members = catalog.libraryIDs(filter);
    auto ordered = selected;
    std::sort(members.begin(), members.end());
    std::sort(ordered.begin(), ordered.end());
    require(members == ordered && catalog.libraryCount() == total,
            "Add to collection changed unselected assets");
    if (id == "UX-SEL-015") {
        d.click("library.collection." + name);
        d.wait([&] { return !c.library.loading() && c.library.count() == 2; },
               "new collection filter loaded");
        d.click(c.card(0));
        d.click("libraryRemoveFromCollection");
        d.wait(
            [&] { return !c.library.working() && !c.library.loading() && c.library.count() == 1; },
            "remove membership completed");
        const auto afterFirst = catalog.assetByID(first->id),
                   afterSecond = catalog.assetByID(second->id);
        require(catalog.libraryCount() == total && afterFirst && afterSecond &&
                    afterFirst->edits == first->edits && afterSecond->edits == second->edits,
                "Remove membership deleted an asset or changed its edit document");
    }
    return true;
}
} // namespace ux
