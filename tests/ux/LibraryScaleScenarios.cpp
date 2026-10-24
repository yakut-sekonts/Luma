#include "tests/ux/UxContext.h"
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>

namespace ux {
namespace {
void metadataFixture(Context &c, int rows = 100000) {
    require(rows == 10000 || rows == 100000 || rows == 500000, "Unsupported metadata fixture size");
    const auto database = c.catalogFile.toStdString();
    const auto prefix = (c.stateRoot + "/metadata-only/").toStdString();
    require(QDir().mkpath(QString::fromStdString(prefix)),
            "Cannot create the isolated empty source directory");
    auto work = std::async(std::launch::async, [database, prefix, rows] {
        sqlite3 *raw = nullptr;
        if (sqlite3_open(database.c_str(), &raw) != SQLITE_OK) {
            if (raw)
                sqlite3_close(raw);
            throw std::runtime_error("Cannot open metadata fixture database");
        }
        const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection(raw, sqlite3_close);
        sqlite3_busy_timeout(raw, 5000);
        require(sqlite3_exec(raw, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK,
                "Cannot begin metadata fixture");
        sqlite3_stmt *statement = nullptr;
        constexpr auto sql =
            R"sql(WITH RECURSIVE n(i) AS (VALUES(1) UNION ALL SELECT i+1 FROM n WHERE i<?2)
            INSERT INTO assets(id,original_path,filename,extension,filesize,checksum,width,height,orientation,camera)
            SELECT printf('%06d',i),?1||printf('%06d',i)||'.png',printf('%06d',i)||'.png','png',1,'metadata-only',256,192,0,'Synthetic' FROM n)sql";
        require(sqlite3_prepare_v2(raw, sql, -1, &statement, nullptr) == SQLITE_OK,
                "Cannot prepare metadata fixture rows");
        const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> query(statement,
                                                                               sqlite3_finalize);
        require(sqlite3_bind_text(statement, 1, prefix.c_str(), -1, SQLITE_TRANSIENT) ==
                        SQLITE_OK &&
                    sqlite3_bind_int(statement, 2, rows) == SQLITE_OK &&
                    sqlite3_step(statement) == SQLITE_DONE,
                "Cannot insert metadata fixture rows");
        require(sqlite3_exec(raw,
                             "INSERT INTO edit_documents(asset_id) SELECT id FROM assets; COMMIT",
                             nullptr, nullptr, nullptr) == SQLITE_OK,
                "Cannot commit metadata fixture");
    });
    c.driver->wait(
        [&] { return work.wait_for(std::chrono::seconds(0)) == std::future_status::ready; },
        "metadata-only fixture worker complete", 60000);
    work.get();
    // Fixture refresh only; acceptance scrolling/typing below uses real input.
    c.library.refresh();
    c.driver->wait(
        [&] { return !c.library.loading() && !c.library.working() && c.library.count() == rows; },
        "Requested metadata rows reflected in the UI");
    c.driver->frame();
    c.driver->log(
        {{"action", "metadata_only_fixture_ready"}, {"rows", rows}, {"real_image_files", 0}});
}
int delegates(QQuickItem *parent) {
    int count = 0;
    for (auto *child : parent->childItems()) {
        if (child->objectName().startsWith("library.card."))
            ++count;
        count += delegates(child);
    }
    return count;
}
bool populatedVisibleCards(QQuickItem *parent, QQuickItem *grid, int rows, int &visible) {
    const auto viewport = grid->mapRectToScene(grid->boundingRect());
    for (auto *child : parent->childItems()) {
        if (child->objectName().startsWith("library.card.") && child->isVisible() &&
            child->mapRectToScene(child->boundingRect()).intersects(viewport)) {
            ++visible;
            const auto id = child->property("assetID").toString();
            bool numeric = false;
            const int row = id.toInt(&numeric);
            if (!numeric || row < 1 || row > rows || id.size() != 6 ||
                child->property("filename").toString() != id + ".png" ||
                !child->property("sourceMissing").toBool())
                return false;
        }
        if (!populatedVisibleCards(child, grid, rows, visible))
            return false;
    }
    return true;
}
void verifyVirtualization(Context &c, int rows) {
    auto &d = *c.driver;
    auto *grid = d.item("library.grid");
    auto *bar = d.object("library.scrollBar");
    int maximumDelegates = 0;
    for (const double fraction : {0., .1, .3, .5, .7, .9, 1., .5, 0.}) {
        auto *thumb =
            qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(bar->property("contentItem")));
        auto *scrollBar = d.item("library.scrollBar");
        require(thumb && thumb->height() > 0, "Metadata scrollbar has no physical thumb");
        const auto center =
            thumb->mapToItem(scrollBar, QPointF(thumb->width() / 2, thumb->height() / 2));
        const double travel = bar->property("availableHeight").toDouble() - thumb->height();
        require(travel > 0, "Metadata scrollbar has no physical thumb travel");
        const double target =
            bar->property("topPadding").toDouble() + fraction * travel + thumb->height() / 2;
        d.drag("library.scrollBar",
               {center.x() / scrollBar->width(), center.y() / scrollBar->height()},
               {.5, target / scrollBar->height()});
        d.wait([&] { return !grid->property("moving").toBool() && !c.library.loading(); },
               "metadata grid settled after actual scrollbar drag");
        d.frame();
        int visible = 0;
        d.wait(
            [&] {
                visible = 0;
                return populatedVisibleCards(grid, grid, rows, visible) && visible > 0;
            },
            "Visible metadata cards have real IDs, filenames and Missing state");
        const double maximumOffset = grid->property("contentHeight").toDouble() - grid->height();
        require(maximumOffset > 0 &&
                    std::abs(grid->property("contentY").toDouble() / maximumOffset - fraction) <=
                        2 / travel + 1e-4,
                "Physical scrollbar drag did not reach the requested metadata region");
        const int count = delegates(grid);
        maximumDelegates = std::max(maximumDelegates, count);
        require(count > 0 && count <= 200 && c.library.cachedPages() <= LibraryModel::maximumPages,
                "Metadata rows created unbounded delegates or model pages");
        d.log({{"action", "metadata_grid_virtualization"},
               {"fraction", fraction},
               {"delegates", count},
               {"populated_visible_cards", visible},
               {"cached_pages", static_cast<int>(c.library.cachedPages())}});
    }
    writeJson(c.root + "/measurements.json", {{"fixture", "metadata_only"},
                                              {"rows", rows},
                                              {"maximum_grid_delegates", maximumDelegates}});
}
QJsonObject summary(const Distribution &distribution) {
    return {{"samples", static_cast<qint64>(distribution.count())},
            {"p50_ms", distribution.percentile(.5)},
            {"p95_ms", distribution.percentile(.95)},
            {"p99_ms", distribution.percentile(.99)},
            {"maximum_ms", distribution.maximum()}};
}
} // namespace
bool catalogScaleRestartScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-P09-CAT-001" && id != "UX-P09-CAT-006" && id != "UX-P09-CAT-011")
        return false;
    const int rows = id == "UX-P09-CAT-001" ? 10000 : id == "UX-P09-CAT-006" ? 100000 : 500000;
    auto &d = *c.driver;
    if (stage == "prepare") {
        metadataFixture(c, rows);
        writeJson(c.stateRoot + "/expected-catalog.json",
                  {{"rows", rows},
                   {"prepare_pid", QCoreApplication::applicationPid()},
                   {"real_image_files", 0}});
        d.quitShortcut();
        d.wait([&] { return !c.window->isVisible() && !c.editor.saving() && !c.editor.busy(); },
               "Prepared large catalog closed before fresh-process open");
        return true;
    }
    require(stage == "verify", "Invalid catalog restart stage");
    QFile file(c.stateRoot + "/expected-catalog.json");
    require(file.open(QIODevice::ReadOnly), "Missing catalog size oracle");
    const auto expected = QJsonDocument::fromJson(file.readAll()).object();
    require(expected["rows"].toInt() == rows &&
                expected["prepare_pid"].toInteger() != QCoreApplication::applicationPid(),
            "Catalog must be opened in a distinct fresh process");
    // Context's normal startup already ran real recovery and LibraryModel load.
    // No refresh/population is allowed in this stage.
    require(!c.library.loading() && !c.library.working() && c.library.error().isEmpty() &&
                c.library.count() == rows && c.library.rowCount() == rows &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == rows,
            "Fresh process failed to open the complete large catalog");
    const int initial = delegates(d.item("library.grid"));
    require(initial > 0 && initial <= 200 && c.library.cachedPages() <= LibraryModel::maximumPages,
            "Opening the catalog materialized unbounded cards or pages");
    d.log({{"action", "fresh_catalog_open"},
           {"rows", rows},
           {"initial_delegates", initial},
           {"cached_pages", static_cast<int>(c.library.cachedPages())},
           {"prepare_pid", expected["prepare_pid"]},
           {"verify_pid", QCoreApplication::applicationPid()},
           {"real_image_files", 0},
           {"performance_qualification", false}});
    verifyVirtualization(c, rows);
    d.screenshot(c.root + "/large-catalog-open.png");
    return true;
}
bool libraryScaleScenario(Context &c, const QString &id) {
    if (id != "UX-LIB-012" && id != "UX-FLT-002" && id != "UX-PERF-006")
        return false;
    if (id == "UX-PERF-006")
        require(QGuiApplication::platformName() == "cocoa" &&
                    qEnvironmentVariable("GITHUB_ACTIONS") != "true",
                "PERF_LOCAL search measurements require native Cocoa on an idle physical Mac");
    metadataFixture(c);
    auto &d = *c.driver;
    if (id == "UX-LIB-012") {
        verifyVirtualization(c, 100000);
        return true;
    }
    const auto path = c.catalogFile.toStdString();
    auto oracle = std::async(std::launch::async, [path] {
        luma::Catalog catalog(path);
        std::vector<std::int64_t> counts;
        luma::LibraryFilter filter;
        for (int index = 0; index < 20; ++index) {
            filter.search += '0';
            counts.push_back(catalog.libraryCount(filter));
        }
        return counts;
    });
    d.wait([&] { return oracle.wait_for(std::chrono::seconds(0)) == std::future_status::ready; },
           "independent search-count fixture oracle ready");
    const auto expectedCounts = oracle.get();
    c.metrics.reset();
    Distribution acknowledgment;
    QSignalSpy resets(&c.library, &QAbstractItemModel::modelReset);
    c.window->raise();
    c.window->requestActivate();
    d.wait([&] { return c.window->isActive() && c.window->isExposed(); },
           "search latency fixture foreground");
    d.click("library.search");
    QString query;
    for (int index = 0; index < 20; ++index) {
        query += '0';
        const auto expected = expectedCounts[static_cast<std::size_t>(index)];
        const auto before = resets.count();
        if (!c.window->isActive()) {
            c.window->requestActivate();
            d.wait([&] { return c.window->isActive(); },
                   "search foreground restored outside timer");
        }
        d.log({{"action", "filter_key_input_prepared"}, {"key", "0"}});
        require(c.window->isActive() && d.item("library.search")->hasActiveFocus(),
                "Search latency fixture lost its native input focus");
        QElapsedTimer timer;
        timer.start();
        QTest::keyClick(c.window, Qt::Key_0, Qt::NoModifier, 0);
        d.wait(
            [&] {
                return resets.count() > before && !c.library.loading() &&
                       c.library.count() == expected;
            },
            "per-keystroke latest filter result acknowledged");
        d.frame();
        const double elapsed = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        require(c.window->isActive(), "Search window lost foreground during the measured input");
        acknowledgment.add(elapsed);
        require(d.object("library.search")->property("text").toString() == query,
                "Search input lost an actual key event");
        d.log({{"action", "filter_model_and_polish_ack"},
               {"characters", index + 1},
               {"count", static_cast<qint64>(expected)},
               {"milliseconds", elapsed}});
    }
    writeJson(c.root + "/measurements.json", {{"mode", "MEASURE_ONLY"},
                                              {"fixture", "metadata_only"},
                                              {"rows", 100000},
                                              {"filter_model_and_polish", summary(acknowledgment)},
                                              {"includes_debounce", true}});
    return true;
}
} // namespace ux
