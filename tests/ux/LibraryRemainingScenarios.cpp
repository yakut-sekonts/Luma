#include "core/catalog/Catalog.h"
#include "tests/ux/QueuedCallGate.h"
#include "tests/ux/UxContext.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QPointer>
#include <QScreen>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <sqlite3.h>
#include <utility>

namespace ux {
namespace {
using Rows = std::vector<luma::LibraryItem>;
Rows catalogRows(Context &c, const QString &search = {}) {
    luma::LibraryFilter filter;
    filter.search = search.toStdString();
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto count = catalog.libraryCount(filter);
    require(count >= 0 && count <= 10000, "Unexpected private Library fixture cardinality");
    Rows rows;
    for (int offset = 0; offset < count; offset += 1024) {
        const int length = static_cast<int>(std::min<std::int64_t>(1024, count - offset));
        const auto page = catalog.libraryPage(filter, offset, length);
        require(static_cast<int>(page.size()) == length,
                "Independent catalog metadata page is truncated");
        rows.insert(rows.end(), page.begin(), page.end());
    }
    require(rows.size() == static_cast<std::size_t>(count),
            "Independent catalog order is truncated");
    return rows;
}
std::vector<std::string> sortedIDs(const Rows &rows) {
    std::vector<std::string> ids;
    for (const auto &row : rows)
        ids.push_back(row.id);
    std::sort(ids.begin(), ids.end());
    return ids;
}
struct AssetInvariant {
    std::string id, checksum;
    std::filesystem::path path;
    luma::EditDocument document;
};
std::vector<AssetInvariant> assetInvariants(Context &c, const Rows &rows) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    std::vector<AssetInvariant> result;
    result.reserve(rows.size());
    for (const auto &row : rows) {
        const auto asset = catalog.assetByID(row.id);
        require(asset && asset->path == row.path && asset->checksum == row.checksum &&
                    asset->edits == luma::EditDocument{},
                "Freshly imported Library fixture lacks its own unchanged default document");
        result.push_back({asset->id, asset->checksum, asset->path, asset->edits});
    }
    return result;
}
void unchangedAssets(Context &c, const std::vector<AssetInvariant> &expected) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    for (const auto &prior : expected) {
        const auto actual = catalog.assetByID(prior.id);
        require(actual && actual->id == prior.id && actual->path == prior.path &&
                    actual->checksum == prior.checksum && actual->edits == prior.document,
                "Library interaction changed an existing asset identity, source or full durable "
                "document");
    }
}
void idle(Context &c) {
    c.driver->wait(
        [&] { return !c.library.working() && !c.library.loading() && !c.library.selecting(); },
        "Library background operation settled", 60000);
    require(c.library.error().isEmpty(), c.library.error());
    c.driver->frame();
}
void dataReady(Context &c, const Rows &rows) {
    require(c.library.count() == static_cast<int>(rows.size()),
            "Observed model cardinality differs from independent Catalog order");
    // One page at a time avoids an impossible simultaneous >maximumPages cache
    // requirement when a partially cancelled import leaves >2048 total assets.
    for (int start = 0; start < static_cast<int>(rows.size()); start += LibraryModel::pageSize) {
        const int end = std::min(start + LibraryModel::pageSize, static_cast<int>(rows.size()));
        c.driver->wait(
            [&] {
                for (int row = start; row < end; ++row)
                    if (c.library.data(c.library.index(row), LibraryModel::AssetID)
                            .toString()
                            .isEmpty())
                        return false;
                return true;
            },
            "actual Library metadata page ready");
        for (int row = start; row < end; ++row) {
            const auto &expected = rows[static_cast<std::size_t>(row)];
            require(c.library.data(c.library.index(row), LibraryModel::AssetID)
                                .toString()
                                .toStdString() == expected.id &&
                        c.library.data(c.library.index(row), LibraryModel::Filename)
                                .toString()
                                .toStdString() == expected.filename &&
                        c.library.data(c.library.index(row), LibraryModel::Path)
                                .toUrl()
                                .toLocalFile()
                                .toStdString() == expected.path.string(),
                    "A stale page changed current model asset/name/path ownership");
        }
        require(c.library.cachedPages() <= LibraryModel::maximumPages,
                "Library page cache exceeded its production bound");
    }
}
struct Card {
    QPointer<QQuickItem> item;
    int row = -1;
    QString id;
    QRectF box;
};
std::vector<Card> visible(Context &c) {
    auto *grid = c.driver->item("library.grid");
    std::vector<Card> cards;
    std::function<void(QQuickItem *)> visit = [&](QQuickItem *parent) {
        for (auto *item : parent->childItems()) {
            if (item->objectName().startsWith("library.card.")) {
                const auto box = item->mapRectToItem(grid, item->boundingRect());
                if (item->isVisible() && box.intersects(grid->boundingRect()))
                    cards.push_back({item, item->property("index").toInt(),
                                     item->property("assetID").toString(), box});
            } else
                visit(item);
        }
    };
    visit(grid);
    std::sort(cards.begin(), cards.end(),
              [](const auto &first, const auto &second) { return first.row < second.row; });
    return cards;
}
int columns(Context &c) {
    auto *grid = c.driver->item("library.grid");
    const auto cell = grid->property("cellWidth").toDouble();
    require(grid->width() > 0 && grid->height() > 0 && cell > 0,
            "A zero-sized viewport cannot prove Library layout correctness");
    const int actual = static_cast<int>(std::lround(grid->width() / cell));
    require(actual == std::max(1, static_cast<int>(std::floor(grid->width() / 220))) &&
                std::abs(cell * actual - grid->width()) < .01,
            "Observed cell geometry no longer matches the actual production column formula");
    return actual;
}
void range(Context &c, bool empty = false) {
    auto *grid = c.driver->item("library.grid");
    require(grid->width() > 0 && grid->height() > 100,
            "Library range assertion has no real viewport");
    const double origin = grid->property("originY").toDouble();
    const double height = grid->property("contentHeight").toDouble();
    const double y = grid->property("contentY").toDouble();
    const double maximum = std::max(0., height - grid->height());
    auto *bar = c.driver->object("library.scrollBar");
    const double size = bar->property("size").toDouble();
    const double position = bar->property("position").toDouble();
    require(std::isfinite(origin) && std::isfinite(height) && std::isfinite(y) && height >= 0 &&
                y >= origin - 1 && y <= origin + maximum + 1 && size > 0 && size <= 1 + 1e-6 &&
                position >= -1e-6 && position <= 1 - std::min(size, 1.) + 1e-6,
            "Actual grid or scrollbar retained a stale/nonfinite/out-of-bounds range");
    if (empty)
        require(c.library.count() == 0 && grid->property("count").toInt() == 0 &&
                    visible(c).empty() && maximum <= 1 && std::abs(y - origin) <= 1 &&
                    size >= 1 - 1e-6 && std::abs(position) <= 1e-6 &&
                    grid->property("atYBeginning").toBool() && grid->property("atYEnd").toBool(),
                "Empty filtered result left a phantom card, offset, thumb or scroll extent");
}
void viewReady(Context &c, const Rows &rows, bool thumbnails = true) {
    auto &d = *c.driver;
    d.wait(
        [&] {
            auto *grid = d.item("library.grid");
            if (c.library.loading() ||
                grid->property("count").toInt() != static_cast<int>(rows.size()) ||
                grid->width() <= 0 || grid->height() <= 100)
                return false;
            const auto cards = visible(c);
            if (rows.empty())
                return cards.empty() && !grid->property("moving").toBool();
            if (cards.empty())
                return false;
            for (const auto &card : cards) {
                if (!card.item || card.row < 0 || card.row >= static_cast<int>(rows.size()) ||
                    card.id != QString::fromStdString(rows[static_cast<std::size_t>(card.row)].id))
                    return false;
                auto *image = card.item->findChild<QQuickItem *>("photoThumbnail");
                if (thumbnails && (!image || image->property("status").toInt() != 1))
                    return false;
            }
            return !grid->property("moving").toBool();
        },
        "current Library viewport owns the expected actual metadata and thumbnails", 60000);
    range(c, rows.empty());
    QSet<QString> identities;
    for (const auto &card : visible(c)) {
        require(!identities.contains(card.id) && card.row >= 0 && card.row < c.library.count() &&
                    c.library.data(c.library.index(card.row), LibraryModel::AssetID).toString() ==
                        card.id,
                "Visible Library delegates duplicate or cross current model identities");
        identities.insert(card.id);
    }
    require(c.library.error().isEmpty(), c.library.error());
}
void scroll(Context &c, double fraction, int steps = 3) {
    auto &d = *c.driver;
    auto *bar = d.item("library.scrollBar");
    auto *thumb =
        qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(bar->property("contentItem")));
    require(thumb && thumb->height() > 0, "Actual scrollbar thumb is absent");
    const auto center = thumb->mapToItem(bar, QPointF(thumb->width() / 2, thumb->height() / 2));
    const double travel = bar->property("availableHeight").toDouble() - thumb->height();
    require(travel > 0, "Positive-control Library scroll has no actual thumb travel");
    const double target =
        bar->property("topPadding").toDouble() + travel * fraction + thumb->height() / 2;
    d.drag("library.scrollBar", {center.x() / bar->width(), center.y() / bar->height()},
           {.5, target / bar->height()}, steps);
    auto *grid = d.item("library.grid");
    d.wait([&] { return !grid->property("moving").toBool(); },
           "real Library scrollbar drag settled");
    d.frame();
    const double maximum = grid->property("contentHeight").toDouble() - grid->height();
    const double offset =
        grid->property("contentY").toDouble() - grid->property("originY").toDouble();
    require(maximum > 0 && std::abs(offset / maximum - fraction) <= 2 / travel + 1e-4,
            "Actual scrollbar input did not reach its claimed logical region");
}
Card anchor(Context &c) {
    auto *grid = c.driver->item("library.grid");
    const auto cards = visible(c);
    // Import progress reduces viewport height. A settled row can straddle each
    // edge even when the viewport is taller than one card. Identity preservation
    // needs a substantially visible asset with an actual clickable center, not
    // an unrelated requirement that an entire row fit at this scroll offset.
    const auto found = std::find_if(cards.begin(), cards.end(), [&](const auto &card) {
        const auto intersection = card.box.intersected(grid->boundingRect());
        return grid->boundingRect().contains(card.box.center()) &&
               intersection.height() >= card.box.height() / 2;
    });
    require(found != cards.end() && found->row >= 100 &&
                grid->property("contentY").toDouble() - grid->property("originY").toDouble() > 1000,
            "Viewport-preservation positive control is not a distant visible logical asset");
    c.driver->log({{"action", "logical_viewport_anchor"},
                   {"row", found->row},
                   {"asset", found->id},
                   {"viewport_height", grid->height()},
                   {"card_top", found->box.top()},
                   {"card_bottom", found->box.bottom()},
                   {"visible_fraction",
                    found->box.intersected(grid->boundingRect()).height() / found->box.height()},
                   {"content_y", grid->property("contentY").toDouble()}});
    return *found;
}
void nearAnchor(Context &c, const Rows &rows, const Card &old, int maximumColumns) {
    viewReady(c, rows);
    const auto cards = visible(c);
    const auto expected = std::find_if(rows.begin(), rows.end(), [&](const auto &row) {
        return QString::fromStdString(row.id) == old.id;
    });
    const auto expectedRow = static_cast<int>(std::distance(rows.begin(), expected));
    c.driver->log({{"action", "logical_viewport_after_reload"},
                   {"anchor", old.id},
                   {"previous_row", old.row},
                   {"expected_row", expectedRow},
                   {"actual_first_row", cards.empty() ? -1 : cards.front().row},
                   {"content_y", c.driver->item("library.grid")->property("contentY").toDouble()}});
    require(!cards.empty() && expected != rows.end() &&
                std::abs(cards.front().row - expectedRow) <= 2 * maximumColumns &&
                c.driver->item("library.grid")->property("contentY").toDouble() -
                        c.driver->item("library.grid")->property("originY").toDouble() >
                    1000,
            "Layout/reload jumped away from the independently identified logical viewport asset");
}
void noRangeWarnings(Context &c, int before) {
    for (int index = before; index < c.warnings.size(); ++index) {
        const auto text = c.warnings[index].toLower();
        require(!text.contains("out of range") && !text.contains("out-of-range") &&
                    !text.contains("invalid model index"),
                "QML reported an invalid Library access during the actual model transition: " +
                    c.warnings[index]);
    }
}
void filter(Context &c, const QString &text, const Rows &rows) {
    QSignalSpy resets(&c.library, &QAbstractItemModel::modelReset);
    c.driver->type("library.search", text);
    c.driver->wait(
        [&] {
            return !resets.isEmpty() && !c.library.loading() &&
                   c.library.count() == static_cast<int>(rows.size());
        },
        "actual filter reset and result, including unchanged-count queries");
    viewReady(c, rows);
    dataReady(c, rows);
}
std::pair<int, int> breakpoint(Context &c) {
    auto &d = *c.driver;
    auto *grid = d.item("library.grid");
    require(columns(c) >= 1, "No actual columns for breakpoint derivation");
    const double chrome = c.window->width() - grid->width();
    // PhotoCard cellWidth is grid.width / floor(grid.width / 220).
    // The fifth column therefore begins at actual grid width 5*220.
    const int threshold = static_cast<int>(std::ceil(chrome + 5 * 220));
    const int lower = threshold - 2, upper = threshold + 2;
    require(lower >= c.window->minimumWidth() && c.window->screen() &&
                upper + c.window->frameMargins().left() + c.window->frameMargins().right() <=
                    c.window->screen()->availableGeometry().width(),
            "This actual screen/window cannot expose the production 4-to-5-column breakpoint");
    d.log({{"action", "derived_grid_breakpoint"},
           {"measured_chrome", chrome},
           {"lower_window_width", lower},
           {"upper_window_width", upper},
           {"grid_threshold", 1100}});
    d.resize({lower, c.window->height()});
    d.wait([&] { return c.window->width() == lower && columns(c) == 4; },
           "derived lower width has exactly four actual columns");
    return {lower, upper};
}
void resizeAnchor(Context &c) {
    auto &d = *c.driver;
    c.import(1000);
    const auto rows = catalogRows(c);
    const auto [lower, upper] = breakpoint(c);
    scroll(c, .55);
    viewReady(c, rows);
    const auto old = anchor(c);
    d.click(old.item->objectName());
    const auto selected = c.library.selectionIDs();
    require(selected == std::vector<std::string>{old.id.toStdString()},
            "Resize positive control did not select its own anchor");
    for (const auto &[width, expectedColumns] : {std::pair{upper, 5}, std::pair{lower, 4}}) {
        d.resize({width, c.window->height()});
        d.wait([&] { return c.window->width() == width && columns(c) == expectedColumns; },
               "actual 4/5 column crossing completed");
        nearAnchor(c, rows, old, 5);
        require(c.library.selectionIDs() == selected && !c.library.selecting() &&
                    !c.editor.hasPhoto(),
                "Responsive column reflow changed selection or activated Develop");
        d.log({{"action", "resize_logical_anchor_verified"},
               {"width", width},
               {"columns", expectedColumns},
               {"anchor", old.id},
               {"first_visible_row", visible(c).front().row},
               {"content_y", d.item("library.grid")->property("contentY").toDouble()}});
    }
}
struct ReleasePointer {
    Context &c;
    QPoint point;
    bool down = false;
    ~ReleasePointer() {
        if (down)
            QTest::mouseRelease(c.window, Qt::LeftButton, Qt::NoModifier, point);
    }
};
void filterWhileDragging(Context &c) {
    auto &d = *c.driver;
    c.import(1000);
    const auto all = catalogRows(c);
    const auto expected = catalogRows(c, "Photo-00");
    require(expected.size() == 100 && all.size() == 1000,
            "Fast-filter fixture has no independent reduced result");
    const auto assets = assetInvariants(c, all);
    const auto selectionBefore = c.library.selectionIDs();
    require(selectionBefore.empty() && !c.editor.hasPhoto(),
            "Fast-filter fixture must start without selection or an active Develop photo");
    scroll(c, .55);
    viewReady(c, all);
    require(c.library.cachedPages() >= 2, "Fast filter did not warm distant actual metadata pages");
    const int warnings = c.warnings.size();
    QSignalSpy resets(&c.library, &QAbstractItemModel::modelReset);
    d.click("library.search");
    require(d.object("library.search")->property("activeFocus").toBool(),
            "Keyboard filter input has no real focus");
    auto *grid = d.item("library.grid");
    ReleasePointer pointer{c, d.point("library.grid", {.75, .15})};
    d.press("library.grid", {.75, .85});
    pointer.down = true;
    for (const double y : {.65, .4, .15})
        d.move("library.grid", {.75, y});
    require(grid->property("dragging").toBool() && grid->property("moving").toBool() &&
                d.object("library.search")->property("activeFocus").toBool(),
            "No actual simultaneous grid drag and keyboard search focus was established");
    // Real text input while the real pointer remains pressed. Driver::type
    // would add another mouse click and cannot represent this concurrency.
    d.key(Qt::Key_A, Qt::ControlModifier);
    d.key(Qt::Key_Backspace);
    require(grid->property("dragging").toBool() &&
                d.object("library.search")->property("activeFocus").toBool() &&
                c.window->activeFocusItem(),
            "Actual scroll or real search focus ended before the filter text input");
    QInputMethodEvent commit;
    commit.setCommitString("Photo-00");
    QCoreApplication::sendEvent(c.window->activeFocusItem(), &commit);
    require(d.object("library.search")->property("text").toString() == "Photo-00",
            "Actual text input was not accepted during the real drag");
    d.log({{"action", "real_search_input_during_grid_drag"},
           {"dragging_before_commit", true},
           {"pointer_still_down", true},
           {"content_y", grid->property("contentY").toDouble()},
           {"cached_pages", static_cast<int>(c.library.cachedPages())}});
    d.wait([&] { return !resets.isEmpty() && !c.library.loading() && c.library.count() == 100; },
           "filter result installed before the held physical pointer release");
    d.release("library.grid", {.75, .15});
    pointer.down = false;
    viewReady(c, expected);
    dataReady(c, expected);
    require(c.library.selectionIDs() == selectionBefore && !c.library.selecting() &&
                !c.editor.hasPhoto(),
            "Physical drag release after filter reset selected or opened another asset");
    unchangedAssets(c, assets);
    noRangeWarnings(c, warnings);
}
void clearScrolledFilter(Context &c) {
    c.import(1000);
    const auto all = catalogRows(c);
    const auto subset = catalogRows(c, "Photo-00");
    filter(c, "Photo-00", subset);
    scroll(c, .8);
    viewReady(c, subset);
    require(c.driver->item("library.grid")->property("contentY").toDouble() > 1000,
            "Filtered-scroll clear never exercised a distant nonempty viewport");
    const int warnings = c.warnings.size();
    filter(c, "", all);
    scroll(c, .65);
    viewReady(c, all);
    scroll(c, 0);
    viewReady(c, all);
    require(visible(c).front().row == 0 && c.library.selectedCount() == 0,
            "Cleared filter cannot reach its valid first unfiltered asset");
    noRangeWarnings(c, warnings);
}
void emptyRange(Context &c) {
    auto &d = *c.driver;
    c.import(1000);
    const auto all = catalogRows(c);
    scroll(c, .6);
    viewReady(c, all);
    require(d.object("library.scrollBar")->property("size").toDouble() < 1 &&
                d.item("library.grid")->property("contentY").toDouble() > 1000,
            "Empty-range positive control was not a real scrollable nonempty Library");
    const int warnings = c.warnings.size();
    filter(c, "no-such-photo-fixture-688f94", {});
    for (const int delta : {-1200, 1200, -1200, 1200}) {
        d.wheel("library.grid", delta);
        d.wait([&] { return !d.item("library.grid")->property("moving").toBool(); },
               "wheel over actual empty grid settled");
        d.frame();
        range(c, true);
    }
    filter(c, "", all);
    scroll(c, .5);
    viewReady(c, all);
    noRangeWarnings(c, warnings);
}
QByteArray hash(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read owned add-source fixture");
    QCryptographicHash value(QCryptographicHash::Sha256);
    require(value.addData(&file), "Cannot hash complete owned add-source fixture");
    return value.result();
}
QList<QUrl> ownedAdds(Context &c, int count, const QString &prefix = "Z") {
    const auto directory = c.root + "/owned-additions-" + prefix;
    require(QDir().mkpath(directory), "Cannot create private import additions");
    require(!QFileInfo(directory).isSymLink() &&
                QFileInfo(directory).canonicalFilePath().startsWith(
                    QFileInfo(c.root).canonicalFilePath() + '/'),
            "Owned additions directory must not resolve into a shared/public source tree");
    const auto publicNames = QDir(c.fixture).entryList({"Photo-*.png"}, QDir::Files, QDir::Name);
    require(publicNames.size() >= 1000,
            "Import/scroll physical workload needs the standard read-only fixture set");
    QList<QUrl> result;
    for (int index = 0; index < count; ++index) {
        const auto source = c.fixture + '/' + publicNames[index % publicNames.size()];
        const auto destination =
            directory + QString("/%1-added-%2.png").arg(prefix).arg(index, 5, 10, QChar('0'));
        require(QFile::copy(source, destination) && !QFileInfo(destination).isSymLink(),
                "Cannot create a distinct private source pathname for real Import");
        result.push_back(QUrl::fromLocalFile(destination));
    }
    require(hash(result.front().toLocalFile()) == hash(c.fixture + '/' + publicNames.front()),
            "Private addition fixture did not copy actual public image bytes");
    return result;
}
void startImport(Context &c, const QList<QUrl> &sources) {
    auto &d = *c.driver;
    require(
        c.window->property("libraryMode").toBool() &&
            d.object("library.importDialog")->setProperty("sources", QVariant::fromValue(sources)),
        "Cannot supply file-panel result to the actual Library Import dialog");
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "concurrent Add Import opened");
    d.choose("library.importMode", 0);
    if (d.object("library.importDuplicates")->property("checked").toBool())
        d.click("library.importDuplicates");
    d.click("library.importStart");
}
std::int64_t count(Context &c) {
    return luma::Catalog(c.catalogFile.toStdString()).libraryCount();
}
void terminalJournal(Context &c, const std::string &session, const QString &state,
                     int expectedItems) {
    sqlite3 *raw = nullptr;
    const int opened = sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &raw,
                                       SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection(raw, sqlite3_close);
    require(opened == SQLITE_OK && sqlite3_busy_timeout(raw, 1000) == SQLITE_OK,
            "Cannot observe the actual private import terminal journal");
    sqlite3_stmt *statement = nullptr;
    const auto sql = "SELECT s.state,s.finished_at IS NOT NULL,COUNT(i.position),"
                     "COALESCE(SUM(i.state IN ('queued','staged')),0),"
                     "COALESCE(SUM(i.state IN ('committed','duplicate') AND i.asset_id IS NULL),0) "
                     "FROM import_sessions s LEFT JOIN import_items i ON i.session_id=s.id "
                     "WHERE s.id=?1 GROUP BY s.id";
    const int prepared = sqlite3_prepare_v2(raw, sql, -1, &statement, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> query(statement,
                                                                           sqlite3_finalize);
    require(prepared == SQLITE_OK &&
                sqlite3_bind_text(statement, 1, session.c_str(), -1, SQLITE_TRANSIENT) ==
                    SQLITE_OK &&
                sqlite3_step(statement) == SQLITE_ROW,
            "Cannot read the complete terminal session/item audit");
    const auto *text = sqlite3_column_text(statement, 0);
    require(text && QString::fromUtf8(reinterpret_cast<const char *>(text)) == state &&
                sqlite3_column_int(statement, 1) == 1 &&
                sqlite3_column_int(statement, 2) == expectedItems &&
                sqlite3_column_int(statement, 3) == 0 && sqlite3_column_int(statement, 4) == 0 &&
                sqlite3_step(statement) == SQLITE_DONE,
            "Concurrent Import left a nonterminal session, pending item or absent asset reference");
}
void selectionDuringInsert(Context &c) {
    auto &d = *c.driver;
    c.import(1000);
    const auto old = catalogRows(c);
    dataReady(c, old);
    viewReady(c, old);
    d.click(c.card(0));
    const auto baseline = c.library.selectionIDs();
    const auto additions = ownedAdds(c, 8);
    bool observedInsert = false;
    const int pages =
        static_cast<int>((old.size() + LibraryModel::pageSize - 1) / LibraryModel::pageSize);
    QueuedCallGate selection(
        c.library,
        [&] {
            return c.library.selecting() && !c.library.working() && !c.library.loading() &&
                   c.library.cachedPages() == static_cast<std::size_t>(pages) &&
                   c.library.selectionIDs() == baseline;
        },
        [&] {
            // All existing pages were delivered before selectAll; this is the real
            // selection completion. The actual operation is still pending in GUI.
            require(baseline.size() == 1 && count(c) == 1000,
                    "Selection boundary did not precede the new import");
            QueuedCallGate inserted(
                c.library,
                [&] { return c.library.importing() && c.library.selecting() && count(c) > 1000; },
                [&] {
                    const auto physicalCount = count(c);
                    require(physicalCount > 1000 && physicalCount <= 1008 &&
                                c.library.count() == 1000 && c.library.selectionIDs() == baseline &&
                                c.library.selecting(),
                            "No actual durable row insertion overlapped the still-pending UI "
                            "selection operation");
                    observedInsert = true;
                    d.log({{"action", "durable_insert_while_real_selection_completion_held"},
                           {"catalog_rows", static_cast<qint64>(physicalCount)},
                           {"model_rows", c.library.count()},
                           {"selecting", true},
                           {"synthetic_selection", false}});
                });
            startImport(c, additions);
            d.wait([&] { return inserted.caught; },
                   "durable new row observed before actual import/selection callbacks settle");
            if (inserted.failure)
                std::rethrow_exception(inserted.failure);
            d.wait(
                [&] {
                    return !c.library.working() && !c.library.loading() &&
                           c.library.count() == 1008;
                },
                "real import reload invalidates the pending old selection revision");
            require(
                !c.library.selecting() && c.library.selectionIDs() == baseline,
                "Import reload installed a stale selection or lost the existing selected asset");
        });
    d.click("library.selectAll");
    d.wait([&] { return selection.caught; }, "actual selectAll completion boundary");
    if (selection.failure)
        std::rethrow_exception(selection.failure);
    require(observedInsert && c.library.selectionIDs() == baseline && !c.library.selecting() &&
                count(c) == 1008,
            "Old selection completion overwrote the post-import selection after its revision was "
            "cancelled");
    const auto final = catalogRows(c);
    dataReady(c, final);
    viewReady(c, final);
    d.click("library.selectAll");
    d.wait([&] { return !c.library.selecting() && c.library.selectionIDs() == sortedIDs(final); },
           "fresh actual selection includes every committed old and new asset");
    require(luma::Catalog(c.catalogFile.toStdString()).unfinishedImports().empty() &&
                c.library.error().isEmpty(),
            "Selection/import overlap left an unfinished operation or catalog error");
}
void importWhileScrollingWave(Context &c, const QString &prefix) {
    auto &d = *c.driver;
    const auto old = catalogRows(c);
    const auto oldAssets = assetInvariants(c, old);
    const auto additions = ownedAdds(c, 2048, prefix);
    const auto baselineCount = static_cast<std::int64_t>(old.size());
    std::vector<QByteArray> sourceHashes;
    sourceHashes.reserve(static_cast<std::size_t>(additions.size()));
    for (const auto &source : additions)
        sourceHashes.push_back(hash(source.toLocalFile()));
    scroll(c, .55);
    viewReady(c, old);
    const auto initialAnchor = anchor(c);
    d.click(initialAnchor.item->objectName());
    const auto selected = c.library.selectionIDs();
    startImport(c, additions);
    d.wait(
        [&] {
            const auto rows = count(c);
            return c.library.importing() && rows > baselineCount && rows < baselineCount + 2048;
        },
        "physical Add worker has committed rows and still has genuine remaining input");
    const auto before = count(c);
    auto *grid = d.item("library.grid");
    for (const int delta : {-1200, -1200}) {
        require(c.library.importing() && count(c) < baselineCount + 2048,
                "Actual import finished before the real concurrent scroll input");
        const double y = grid->property("contentY").toDouble();
        d.wheel("library.grid", delta);
        d.wait(
            [&] {
                return !grid->property("moving").toBool() &&
                       grid->property("contentY").toDouble() > y + 1;
            },
            "actual Library wheel moved while the real Add worker runs");
        require(c.library.importing() && count(c) < baselineCount + 2048 &&
                    c.library.selectionIDs() == selected,
                "Missed genuine import/scroll overlap or scroll lost its existing selection");
    }
    const auto after = count(c);
    require(after > before && after < baselineCount + 2048,
            "Independent durable count did not increase across the actual wheel input sequence");
    const auto active = luma::Catalog(c.catalogFile.toStdString()).unfinishedImports();
    // unfinishedImports intentionally returns only queued/staged items, not
    // already committed children of the running session.
    require(active.size() == 1 && active.front().mode == luma::ImportMode::add &&
                !active.front().items.empty() && active.front().items.size() < 2048 &&
                active.front().items.front().index > 0 && active.front().items.back().index == 2047,
            "Concurrent Add fixture has no single genuine running journal");
    const auto session = active.front().id;
    const auto beforeReload = anchor(c);
    d.log({{"action", "actual_import_worker_progress_during_wheel"},
           {"catalog_before_wheel", static_cast<qint64>(before)},
           {"catalog_after_wheel", static_cast<qint64>(after)},
           {"anchor_before_import_reload", beforeReload.id},
           {"import_worker_paused_or_gated", false},
           {"wheel_inputs", 2}});
    require(c.library.importing() && count(c) < baselineCount + 2048,
            "Cancel fixture missed its actual remaining import work");
    d.click("library.cancelImport");
    idle(c);
    luma::Catalog catalog(c.catalogFile.toStdString());
    const auto summary = catalog.importSummary(session);
    const auto final = catalogRows(c);
    require(summary.added > 0 && summary.cancelled > 0 && summary.failed == 0 &&
                summary.duplicates == 0 && summary.added + summary.cancelled == 2048 &&
                final.size() == old.size() + static_cast<std::size_t>(summary.added) &&
                catalog.unfinishedImports().empty() && c.library.selectionIDs() == selected,
            "Actual concurrent Add cancellation lost its durable checkpoint or selection");
    for (qsizetype index = 0; index < additions.size(); ++index)
        require(hash(additions[index].toLocalFile()) ==
                    sourceHashes[static_cast<std::size_t>(index)],
                "Concurrent Add or cancellation changed an owned original: " +
                    additions[index].toLocalFile());
    unchangedAssets(c, oldAssets);
    terminalJournal(c, session, "cancelled", 2048);
    std::vector<std::string> retainedOrder, originalOrder;
    QSet<QString> originalIDs;
    for (const auto &row : old) {
        originalIDs.insert(QString::fromStdString(row.id));
        originalOrder.push_back(row.id);
    }
    for (const auto &row : final)
        if (originalIDs.contains(QString::fromStdString(row.id)))
            retainedOrder.push_back(row.id);
    require(retainedOrder == originalOrder,
            "Owned additions changed the relative order of existing assets");
    const auto anchorPosition = std::find_if(final.begin(), final.end(), [&](const auto &row) {
        return QString::fromStdString(row.id) == beforeReload.id;
    });
    require(anchorPosition != final.end() &&
                std::distance(final.begin(), anchorPosition) ==
                    beforeReload.row + (prefix == "A" ? summary.added : 0),
            "Import fixture did not establish its declared before/after-anchor sort boundary");
    nearAnchor(c, final, beforeReload, columns(c));
    dataReady(c, final);
    c.checkData();
}
void heldScrollbarAcrossImportCompletion(Context &c) {
    auto &d = *c.driver;
    const auto before = catalogRows(c);
    const auto documents = assetInvariants(c, before);
    dataReady(c, before);
    scroll(c, .5);
    viewReady(c, before);
    d.frame();
    const auto selected = c.library.selectionIDs();
    const auto additions = ownedAdds(c, 8, "H");
    auto *bar = d.item("library.scrollBar");
    bool capturedWhileHeld = false;
    QObject observer;
    QObject::connect(&c.library, &LibraryModel::reloadStarted, &observer, [&](bool preserve) {
        if (preserve && bar->property("pressed").toBool() &&
            QGuiApplication::mouseButtons().testFlag(Qt::LeftButton))
            capturedWhileHeld = true;
    });
    QSignalSpy resolved(&c.library, &LibraryModel::viewportRestored);
    require(resolved.isValid(), "No actual viewport lookup completion signal");
    ReleasePointer pointer{c, {}};
    QueuedCallGate completed(
        c.library,
        [&] {
            // Hold the first eligible Library delivery after actual final progress.
            // A cacheBuffer page may also deliver here; the separate pressed-at-
            // reload observer proves the real import/input overlap either way.
            return c.library.importing() && !c.library.loading() && c.library.progress() == 1 &&
                   count(c) == static_cast<std::int64_t>(before.size() + 8) &&
                   c.library.count() == static_cast<int>(before.size());
        },
        [&] {
            auto *thumb =
                qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(bar->property("contentItem")));
            require(thumb && thumb->height() > 0, "Held-input positive control has no thumb");
            const auto center = thumb->mapToItem(bar, {thumb->width() / 2, thumb->height() / 2});
            const QPointF fraction(center.x() / bar->width(), center.y() / bar->height());
            pointer.point = d.point("library.scrollBar", fraction);
            pointer.down = true;
            d.press("library.scrollBar", fraction);
            require(bar->property("pressed").toBool() &&
                        QGuiApplication::mouseButtons().testFlag(Qt::LeftButton) &&
                        c.library.importing() && !c.library.loading(),
                    "Genuine import completion did not remain held until the real thumb press");
            d.log({{"action", "held_scrollbar_before_genuine_import_completion"},
                   {"actual_pressed", true},
                   {"committed_new_files", 8},
                   {"queued_completion_fixture", true}});
        });
    startImport(c, additions);
    d.wait([&] { return completed.caught; }, "genuine final Import delivery held for thumb press");
    if (completed.failure)
        std::rethrow_exception(completed.failure);
    idle(c);
    d.log({{"action", "preserving_reload_observed_during_held_scrollbar"},
           {"captured_while_held", capturedWhileHeld},
           {"lookup_completions", resolved.count()},
           {"resolved_row", resolved.isEmpty() ? -2 : resolved.front().front().toInt()},
           {"restore_pending", d.object("library.grid")->property("restorePending").toBool()}});
    require(capturedWhileHeld, "Actual import reload did not overlap the held scrollbar");
    require(resolved.count() == 1 && resolved.front().front().toInt() == -1 &&
                !d.object("library.grid")->property("restorePending").toBool(),
            "A preserving reload scheduled viewport restoration over an already-held scrollbar");
    QTest::mouseRelease(c.window, Qt::LeftButton, Qt::NoModifier, pointer.point);
    pointer.down = false;
    QCoreApplication::processEvents();
    require(!bar->property("pressed").toBool(), "Held scrollbar remained pressed after release");
    const auto after = catalogRows(c);
    require(after.size() == before.size() + 8 && c.library.selectionIDs() == selected,
            "Held-scrollbar reload changed selection or lost actual inserted rows");
    unchangedAssets(c, documents);
    scroll(c, .4);
    viewReady(c, after);
    c.checkData();
}
void importWhileScrolling(Context &c) {
    c.import(1000);
    importWhileScrollingWave(c, "Z");
    importWhileScrollingWave(c, "A");
    heldScrollbarAcrossImportCompletion(c);
}
} // namespace

bool libraryRemainingScenario(Context &c, const QString &id) {
    if (id == "UX-LIB-008")
        resizeAnchor(c);
    else if (id == "UX-LIB-014")
        filterWhileDragging(c);
    else if (id == "UX-LIB-015")
        clearScrolledFilter(c);
    else if (id == "UX-LIB-016")
        emptyRange(c);
    else if (id == "UX-LIB-017")
        importWhileScrolling(c);
    else if (id == "UX-RACE-009")
        selectionDuringInsert(c);
    else
        return false;
    return true;
}
} // namespace ux
