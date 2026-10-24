#include "tests/ux/UxContext.h"
#include <QJsonArray>
#include <QSignalSpy>
#include <algorithm>
#include <cmath>

namespace ux {
namespace {
struct VisibleCard {
    QString selector, asset;
    int row;
    QRectF box;
};
std::vector<VisibleCard> visibleCards(Context &c, bool filmstrip = false) {
    auto *grid = c.driver->item(filmstrip ? "develop.filmstrip" : "library.grid");
    std::vector<VisibleCard> cards;
    std::function<void(QQuickItem *)> visit = [&](QQuickItem *parent) {
        for (auto *item : parent->childItems()) {
            if (item->objectName().startsWith(filmstrip ? "filmstrip.card." : "library.card.")) {
                const auto box = item->mapRectToItem(grid, item->boundingRect());
                const auto asset = item->property("assetID").toString();
                if (item->isVisible() && box.intersects(grid->boundingRect()))
                    cards.push_back(
                        {item->objectName(), asset, item->property("index").toInt(), box});
            } else
                visit(item);
        }
    };
    visit(grid);
    std::sort(cards.begin(), cards.end(),
              [](const auto &a, const auto &b) { return a.row < b.row; });
    return cards;
}
void settled(Context &c, bool filmstrip = false) {
    c.driver->wait(
        [&] {
            return !c.library.loading() && !c.library.working() && !c.library.selecting() &&
                   !c.driver->object(filmstrip ? "develop.filmstrip" : "library.grid")
                        ->property("moving")
                        .toBool();
        },
        "Library selection and scroll settled");
    require(c.library.error().isEmpty(), c.library.error());
    c.driver->frame();
}
void scrollTo(Context &c, double fraction) {
    auto &d = *c.driver;
    auto *bar = d.item("library.scrollBar");
    auto *thumb =
        qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(bar->property("contentItem")));
    require(thumb && thumb->height() > 0, "Library scroll has no usable scrollbar thumb");
    const auto center = thumb->mapToItem(bar, QPointF(thumb->width() / 2, thumb->height() / 2));
    const double travel = bar->property("availableHeight").toDouble() - thumb->height();
    require(travel > 0 && bar->width() > 0 && bar->height() > 0, "Library cannot scroll");
    const double target =
        bar->property("topPadding").toDouble() + travel * fraction + thumb->height() / 2;
    d.drag("library.scrollBar", {center.x() / bar->width(), center.y() / bar->height()},
           {.5, target / bar->height()});
    settled(c);
    auto *grid = d.item("library.grid");
    const double maximum = grid->property("contentHeight").toDouble() - grid->height();
    require(maximum > 0 && std::abs(grid->property("contentY").toDouble() / maximum - fraction) <=
                               2 / travel + 1e-4,
            "Actual scrollbar input did not reach the requested deep viewport");
    d.wait(
        [&] {
            const auto cards = visibleCards(c);
            return !cards.empty() && std::all_of(cards.begin(), cards.end(), [&](const auto &card) {
                return !card.asset.isEmpty() &&
                       c.library.data(c.library.index(card.row), LibraryModel::AssetID)
                               .toString() == card.asset;
            });
        },
        "Deep Library metadata pages ready");
}
QJsonObject snapshot(Context &c) {
    const auto cards = visibleCards(c);
    require(!cards.empty() && !cards.front().asset.isEmpty(),
            "Library scroll oracle has no populated anchor");
    const auto first = cards.front();
    auto *grid = c.driver->item("library.grid");
    return {{"anchor", first.asset},
            {"anchor_row", first.row},
            {"anchor_offset", first.box.y()},
            {"content_y", grid->property("contentY").toDouble()},
            {"origin_y", grid->property("originY").toDouble()},
            {"height", grid->height()},
            {"count", c.library.count()}};
}
VisibleCard clickTarget(Context &c, bool last) {
    auto cards = visibleCards(c);
    auto *grid = c.driver->item("library.grid");
    std::erase_if(cards,
                  [&](const auto &card) { return !grid->boundingRect().contains(card.box); });
    require(cards.size() >= 2, "Library regression needs two fully visible click targets");
    return last ? cards.back() : cards.front();
}
void verify(Context &c, const QJsonObject &before, const QString &action, QJsonArray &checks) {
    settled(c);
    const auto after = snapshot(c);
    QJsonObject entry{{"action", action}, {"before", before}, {"after", after}};
    checks.append(entry);
    c.driver->log(entry);
    require(before["anchor"] == after["anchor"] && before["count"] == after["count"] &&
                std::abs(before["anchor_offset"].toDouble() - after["anchor_offset"].toDouble()) <=
                    1 &&
                std::abs(before["content_y"].toDouble() - after["content_y"].toDouble()) <= 1 &&
                std::abs(before["height"].toDouble() - after["height"].toDouble()) <= 1,
            "Photo selection/open/return changed Library's visible asset anchor or offset");
}
QJsonObject filmstripSnapshot(Context &c) {
    const auto cards = visibleCards(c, true);
    require(!cards.empty() && !cards.front().asset.isEmpty(), "Filmstrip has no visible anchor");
    auto *view = c.driver->item("develop.filmstrip");
    return {{"anchor", cards.front().asset},   {"row", cards.front().row},
            {"offset", cards.front().box.x()}, {"content_x", view->property("contentX").toDouble()},
            {"width", view->width()},          {"count", c.library.count()}};
}
void filmstripScroll(Context &c, QJsonArray &checks) {
    auto &d = *c.driver;
    d.doubleClick(c.card(0));
    c.loaded();
    auto *view = d.item("develop.filmstrip");
    for (const double fraction : {.3, .88}) {
        auto *bar = d.item("develop.filmstripBar");
        auto *thumb =
            qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(bar->property("contentItem")));
        require(thumb && thumb->width() > 0, "Filmstrip has no scrollbar thumb");
        const auto center = thumb->mapToItem(bar, {thumb->width() / 2, thumb->height() / 2});
        const double travel = bar->property("availableWidth").toDouble() - thumb->width();
        require(travel > 0 && bar->width() > 0 && bar->height() > 0, "Filmstrip cannot scroll");
        const double target =
            bar->property("leftPadding").toDouble() + travel * fraction + thumb->width() / 2;
        d.drag("develop.filmstripBar", {center.x() / bar->width(), center.y() / bar->height()},
               {target / bar->width(), .5});
        settled(c, true);
        const double maximum = view->property("contentWidth").toDouble() - view->width();
        require(maximum > 0 && std::abs(view->property("contentX").toDouble() / maximum -
                                        fraction) <= 2 / travel + 1e-4,
                "Filmstrip drag did not reach its requested deep viewport");
        d.wait(
            [&] {
                const auto cards = visibleCards(c, true);
                return !cards.empty() &&
                       std::all_of(cards.begin(), cards.end(), [&](const auto &card) {
                           return !card.asset.isEmpty() &&
                                  c.library.data(c.library.index(card.row), LibraryModel::AssetID)
                                          .toString() == card.asset;
                       });
            },
            "Scrolled filmstrip metadata ready");
        const auto before = filmstripSnapshot(c);
        require(before["content_x"].toDouble() > view->width(),
                "Filmstrip fixture is not deep enough");
        if (fraction > .8)
            require(before["row"].toInt() >= LibraryModel::pageSize,
                    "Filmstrip did not cross metadata page");
        for (bool last : {false, true}) {
            auto cards = visibleCards(c, true);
            std::erase_if(cards, [&](const auto &card) {
                return last ? !view->boundingRect().contains(card.box)
                            : !view->boundingRect().contains(card.box.center());
            });
            require(cards.size() >= 2, "Filmstrip requires two real pointer targets");
            const auto chosen = last ? cards.back() : cards.front();
            auto *content =
                qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(view->property("contentItem")));
            require(content, "Filmstrip has no observable content item");
            const double initialX = content->x();
            QJsonArray positions{initialX};
            QJsonArray framePositions;
            bool overflow = false;
            QObject observer;
            QObject::connect(content, &QQuickItem::xChanged, &observer, [&] {
                if (positions.size() < 64)
                    positions.append(content->x());
                else
                    overflow = true;
            });
            // currentIndex can move and restore content inside one click handler.
            // Keep that trace, but judge visible movement at scene synchronization.
            // Qt blocks the GUI thread during this direct callback, so reading
            // the content item and recording these samples cannot race GUI work.
            QObject::connect(
                c.window, &QQuickWindow::beforeSynchronizing, &observer,
                [&] {
                    if (framePositions.size() < 256)
                        framePositions.append(content->x());
                    else
                        overflow = true;
                },
                Qt::DirectConnection);
            QSignalSpy resets(&c.library, &QAbstractItemModel::modelReset);
            QSignalSpy activation(&c.library, &LibraryModel::openRequested);
            QSignalSpy loaded(&c.editor, &EditorController::photoLoaded);
            d.click(chosen.selector);
            c.loaded();
            settled(c, true);
            const auto after = filmstripSnapshot(c);
            checks.append(QJsonObject{{"action", "filmstrip-single-click"},
                                      {"chosen", chosen.asset},
                                      {"before", before},
                                      {"after", after},
                                      {"content_item_x", positions},
                                      {"frame_content_item_x", framePositions},
                                      {"overflow", overflow},
                                      {"model_resets", resets.count()},
                                      {"activations", activation.count()},
                                      {"loads", loaded.count()}});
            require(c.editor.photoID() == chosen.asset && activation.count() == 1 &&
                        loaded.count() == 1 &&
                        c.library.selectionIDs() ==
                            std::vector<std::string>{chosen.asset.toStdString()},
                    "Filmstrip click did not select and open exactly its visible photo once");
            require(resets.isEmpty() && before["anchor"] == after["anchor"] &&
                        before["count"] == after["count"] &&
                        std::abs(before["offset"].toDouble() - after["offset"].toDouble()) <= 1 &&
                        std::abs(before["content_x"].toDouble() - after["content_x"].toDouble()) <=
                            1,
                    "Filmstrip click/open moved its visible anchor or reset the model");
            require(!overflow && !framePositions.isEmpty() &&
                        std::all_of(framePositions.begin(), framePositions.end(),
                                    [&](const auto &value) {
                                        return std::isfinite(value.toDouble()) &&
                                               std::abs(value.toDouble() - initialX) <= 1;
                                    }),
                    "Filmstrip click moved a frame's viewport by more than one logical pixel");
        }
        const int previousRow = view->property("currentIndex").toInt();
        const double previousX = view->property("contentX").toDouble();
        QSignalSpy keyboardActivations(&c.library, &LibraryModel::openRequested);
        d.key(Qt::Key_Right);
        try {
            d.wait(
                [&] {
                    const auto cards = visibleCards(c, true);
                    return view->property("contentX").toDouble() > previousX + 1 &&
                           std::any_of(cards.begin(), cards.end(), [&](const auto &card) {
                               return card.row == previousRow + 1 &&
                                      view->boundingRect().contains(card.box);
                           });
                },
                "Filmstrip Right-key current-item reveal completed", 5000);
        } catch (...) {
            QJsonArray geometry;
            for (const auto &card : visibleCards(c, true))
                geometry.append(QJsonObject{{"row", card.row},
                                            {"x", card.box.x()},
                                            {"y", card.box.y()},
                                            {"width", card.box.width()},
                                            {"height", card.box.height()}});
            checks.append(QJsonObject{{"action", "filmstrip-keyboard-failure"},
                                      {"previous_row", previousRow},
                                      {"current_row", view->property("currentIndex").toInt()},
                                      {"state", filmstripSnapshot(c)},
                                      {"cards", geometry},
                                      {"height", view->height()}});
            throw;
        }
        settled(c, true);
        require(view->property("currentIndex").toInt() == previousRow + 1,
                "Filmstrip mouse retention broke Right-key navigation");
        checks.append(QJsonObject{{"action", "filmstrip-keyboard-right"},
                                  {"before_row", previousRow},
                                  {"after_row", view->property("currentIndex").toInt()},
                                  {"before_x", previousX},
                                  {"after", filmstripSnapshot(c)}});
        const double rightX = view->property("contentX").toDouble();
        constexpr int leftSteps = 12; // Cross this bounded fixture's visible row.
        for (int step = 0; step < leftSteps; ++step)
            d.key(Qt::Key_Left);
        d.wait(
            [&] {
                const auto cards = visibleCards(c, true);
                return view->property("contentX").toDouble() < rightX - 1 &&
                       std::any_of(
                           cards.begin(), cards.end(),
                           [&](const auto &card) {
                               return card.row == previousRow + 1 - leftSteps &&
                                      view->boundingRect().contains(card.box);
                           });
            },
            "Filmstrip Left-key current-item reveal completed", 5000);
        require(view->property("currentIndex").toInt() == previousRow + 1 - leftSteps &&
                    keyboardActivations.isEmpty(),
                "Filmstrip arrow navigation opened a photo or changed the wrong current item");
        checks.append(QJsonObject{{"action", "filmstrip-keyboard-left"},
                                  {"key_presses", leftSteps},
                                  {"after", filmstripSnapshot(c)}});
    }
}
} // namespace

bool libraryScrollCandidate(Context &c, const QString &id) {
    const bool filmstrip = id == "CANDIDATE-FILMSTRIP-SELECTION-SCROLL";
    if (id != "CANDIDATE-LIBRARY-SELECTION-SCROLL" && !filmstrip)
        return false;
    constexpr int count = 176;
    require(count > LibraryModel::pageSize, "Fixture must cross a production metadata page");
    c.import(count);
    auto &d = *c.driver;
    QJsonArray checks;
    try {
        if (filmstrip)
            filmstripScroll(c, checks);
        for (int phase = 0; phase < (filmstrip ? 0 : 3); ++phase) {
            const QString search = phase == 1 ? "Photo-01" : "";
            if (phase > 0) {
                d.type("library.search", search);
                d.wait(
                    [&] {
                        return !c.library.loading() &&
                               c.library.count() == (phase == 1 ? 76 : count);
                    },
                    "Actual Library filter completed");
            }
            scrollTo(c, phase == 0 ? .88 : .6);
            const auto before = snapshot(c);
            require(before["content_y"].toDouble() > before["height"].toDouble(),
                    "Selection fixture is not beyond the first viewport");
            if (phase == 0)
                require(before["anchor_row"].toInt() >= LibraryModel::pageSize,
                        "Selection fixture did not reach the second metadata page");
            QSignalSpy resets(&c.library, &QAbstractItemModel::modelReset);
            QSignalSpy activation(&c.library, &LibraryModel::openRequested);
            QString chosen;
            for (bool last : {false, true}) {
                const auto target = clickTarget(c, last);
                chosen = target.asset;
                auto *content = qobject_cast<QQuickItem *>(
                    qvariant_cast<QObject *>(d.object("library.grid")->property("contentItem")));
                require(content, "Library scroll observer has no content item");
                const double initialY = content->y();
                QJsonArray positions{initialY};
                QJsonArray framePositions;
                bool overflow = false;
                QObject observer;
                QObject::connect(content, &QQuickItem::yChanged, &observer, [&] {
                    if (positions.size() < 64)
                        positions.append(content->y());
                    else
                        overflow = true;
                });
                // As for the filmstrip, Qt can reveal/restore currentIndex inside
                // one event handler. Only synchronized frames reach the user.
                // Retain the raw motion trace as evidence, with the same 1 px limit
                // applied to every frame and the final visible anchor below.
                QObject::connect(
                    c.window, &QQuickWindow::beforeSynchronizing, &observer,
                    [&] {
                        if (framePositions.size() < 256)
                            framePositions.append(content->y());
                        else
                            overflow = true;
                    },
                    Qt::DirectConnection);
                d.click(target.selector);
                verify(c, before, "single-click", checks);
                checks.append(QJsonObject{{"action", "single-click-motion"},
                                          {"content_item_y", positions},
                                          {"frame_content_item_y", framePositions},
                                          {"overflow", overflow}});
                const auto selected = c.library.selectionIDs();
                require(selected.size() == 1 && selected.front() == chosen.toStdString() &&
                            activation.isEmpty(),
                        "Single Library click selected the wrong asset or opened Develop");
                require(!overflow && !framePositions.isEmpty() &&
                            std::all_of(framePositions.begin(), framePositions.end(),
                                        [&](const auto &value) {
                                            return std::isfinite(value.toDouble()) &&
                                                   std::abs(value.toDouble() - initialY) <= 1;
                                        }),
                        "Single click moved a rendered Library viewport by more than one "
                        "logical pixel");
            }
            const auto target = clickTarget(c, true);
            d.doubleClick(target.selector);
            c.loaded();
            require(c.editor.photoID() == chosen && activation.count() == 1 &&
                        !c.window->property("libraryMode").toBool(),
                    "Deep Library double click did not open exactly its selected asset");
            d.click("main.libraryButton");
            verify(c, before, "open-and-return", checks);
            require(resets.isEmpty(), "Opening an already loaded asset reset the Library model");
            // Mouse retention must not disable the view's ordinary arrow-key
            // reveal of a current item outside the viewport.
            d.click(clickTarget(c, true).selector);
            auto *grid = d.item("library.grid");
            const int previousRow = grid->property("currentIndex").toInt();
            const int columns =
                static_cast<int>(grid->width() / grid->property("cellWidth").toDouble());
            const double previousY = grid->property("contentY").toDouble();
            d.key(Qt::Key_Down);
            d.wait([&] { return grid->property("contentY").toDouble() > previousY + 1; },
                   "Arrow-key current-item reveal completed", 5000);
            settled(c);
            checks.append(QJsonObject{{"action", "keyboard-down"},
                                      {"before_row", previousRow},
                                      {"after_row", grid->property("currentIndex").toInt()},
                                      {"before_y", previousY},
                                      {"after", snapshot(c)}});
            require(columns > 0 &&
                        grid->property("currentIndex").toInt() == previousRow + columns &&
                        grid->property("contentY").toDouble() > previousY + 1,
                    "Mouse viewport retention broke arrow-key current-item reveal");
        }
        writeJson(c.root + "/library-selection-scroll.json",
                  {{"status", "PASS"},
                   {"photos", count},
                   {"surface", filmstrip ? "filmstrip" : "library"},
                   {"checks", checks},
                   {"native_file_chooser", "fixture-boundary"},
                   {"sort", "default filename/ID"}});
    } catch (...) {
        writeJson(c.root + "/library-selection-scroll.json",
                  {{"status", "FAIL"}, {"checks", checks}});
        throw;
    }
    return true;
}
} // namespace ux
