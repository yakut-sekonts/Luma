#include "tests/ux/UxContext.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <filesystem>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace ux {
namespace {
struct ReleaseWrites {
    SqliteFault &fault;
    ~ReleaseWrites() {
        fault.holdWrites(false);
    }
};
class DirectoryModes {
  public:
    ~DirectoryModes() {
        restore(false);
    }
    void change(const QString &path, mode_t mode) {
        const auto encoded = QFile::encodeName(path);
        struct stat state{};
        require(::lstat(encoded.constData(), &state) == 0 && S_ISDIR(state.st_mode),
                "Permission fixture is not an owned normal directory: " + path);
        saved_.push_back({encoded, state.st_mode & 07777});
        require(::chmod(encoded.constData(), mode) == 0,
                "Cannot set isolated directory permissions: " + path);
    }
    void restore(bool verify = true) {
        bool ok = true;
        for (auto entry = saved_.crbegin(); entry != saved_.crend(); ++entry)
            ok = (::chmod(entry->first.constData(), entry->second) == 0) && ok;
        saved_.clear();
        if (verify)
            require(ok, "Cannot restore isolated fixture directory permissions");
    }

  private:
    std::vector<std::pair<QByteArray, mode_t>> saved_;
};
QByteArray hash(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot hash import fixture: " + path);
    QCryptographicHash digest(QCryptographicHash::Sha256);
    require(digest.addData(&file), "Cannot read all import fixture bytes");
    return digest.result();
}
QList<QUrl> sources(Context &c, int count) {
    QList<QUrl> result;
    QSet<QByteArray> distinct;
    for (int index = 0; index < count; ++index) {
        const auto path = c.fixture + QString("/Photo-%1.png").arg(index, 4, 10, QChar('0'));
        result.append(QUrl::fromLocalFile(path));
        distinct.insert(hash(path));
    }
    require(distinct.size() == count, "Import cycle fixture requires distinct image checksums");
    return result;
}
void start(Context &c, const QList<QUrl> &input, const QString &destination = {},
           bool skip = false) {
    auto &d = *c.driver;
    if (!c.window->property("libraryMode").toBool())
        d.click("main.libraryButton");
    // Only the native file-panel selection is a fixture boundary. Mode, checkboxes,
    // start, report and cancellation below use real production UI inputs.
    d.log({{"action", "import_selection_fixture_boundary"},
           {"source_count", input.size()},
           {"destination", destination},
           {"native_selection_exercised", false}});
    require(d.object("library.pane")->setProperty("sources", QVariant::fromValue(input)),
            "Import pane refused its public fixture selection");
    if (!destination.isEmpty()) {
        require(QDir().mkpath(destination), "Cannot prepare isolated Copy destination");
        require(
            d.object("library.pane")->setProperty("destination", QUrl::fromLocalFile(destination)),
            "Import pane refused its known destination");
    }
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "extra Import dialog opened");
    d.choose("library.importMode", destination.isEmpty() ? 0 : 1);
    for (const auto &setting : {std::pair<QString, bool>{"library.importRecursive", true},
                                {"library.importDuplicates", skip}}) {
        if (d.object(setting.first)->property("checked").toBool() != setting.second)
            d.click(setting.first);
    }
    d.click("library.importStart");
}
void settled(Context &c) {
    c.driver->wait([&] { return !c.library.working() && !c.library.loading(); },
                   "extra import settled with model reload", 60000);
    c.checkData();
    c.driver->frame();
}
QList<QUrl> ownedSources(Context &c, int count) {
    const auto directory = c.root + "/owned-sources";
    require(QDir().mkpath(directory), "Cannot prepare mutable source fixture directory");
    QList<QUrl> result;
    for (int index = 0; index < count; ++index) {
        const auto input = c.fixture + QString("/Photo-%1.png").arg(index, 4, 10, QChar('0'));
        const auto output = directory + QString("/owned-%1.png").arg(index, 4, 10, QChar('0'));
        require(QFile::copy(input, output) && hash(input) == hash(output),
                "Cannot prepare intact mutable source copy");
        result.append(QUrl::fromLocalFile(output));
    }
    return result;
}
// All returned sessions, including terminal history. Existing Catalog intentionally
// retains completed journals; absence of a leak means no unfinished/staged/orphan
// items, not deletion of legitimate terminal audit rows.
struct Session {
    QString id, state;
    int itemCount = 0, pendingItems = 0, missingAssets = 0;
    bool finished = false;
};
std::vector<Session> sessions(Context &c) {
    sqlite3 *rawDatabase = nullptr;
    const auto code = sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &rawDatabase,
                                      SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(rawDatabase, sqlite3_close);
    require(code == SQLITE_OK, "Cannot read isolated import journal evidence");
    // This observer opens its own connection while the real worker can be
    // completing a WAL transaction. Match bounded reader behaviour; never turn a
    // transient SQLITE_BUSY during prepare into an application failure.
    require(sqlite3_busy_timeout(database.get(), 1000) == SQLITE_OK,
            "Cannot configure bounded import-journal observer waiting");
    const char *sql =
        "SELECT s.id,s.state,s.finished_at IS NOT NULL,COUNT(i.position),"
        "COALESCE(SUM(i.state IN ('queued','staged')),0),"
        "COALESCE(SUM(i.state IN ('committed','duplicate') AND i.asset_id IS NULL),0) "
        "FROM import_sessions AS s LEFT JOIN import_items AS i ON i.session_id=s.id "
        "GROUP BY s.id,s.state,s.finished_at ORDER BY s.id";
    sqlite3_stmt *rawStatement = nullptr;
    const int prepared = sqlite3_prepare_v2(database.get(), sql, -1, &rawStatement, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> query(rawStatement,
                                                                           sqlite3_finalize);
    require(prepared == SQLITE_OK,
            "Cannot prepare import journal evidence query; code=" + QString::number(prepared) +
                "; error=" + QString::fromUtf8(sqlite3_errmsg(database.get())));
    std::vector<Session> result;
    int status = SQLITE_OK;
    while ((status = sqlite3_step(query.get())) == SQLITE_ROW) {
        const auto text = [&](int column) {
            const auto *value = sqlite3_column_text(query.get(), column);
            return value ? QString::fromUtf8(reinterpret_cast<const char *>(value)) : QString{};
        };
        result.push_back({text(0), text(1), sqlite3_column_int(query.get(), 3),
                          sqlite3_column_int(query.get(), 4), sqlite3_column_int(query.get(), 5),
                          sqlite3_column_int(query.get(), 2) != 0});
    }
    require(status == SQLITE_DONE, "Cannot read the complete import journal evidence");
    sqlite3_stmt *rawOrphans = nullptr;
    const int orphanQuery =
        sqlite3_prepare_v2(database.get(),
                           "SELECT COUNT(*) FROM import_items AS i LEFT JOIN import_sessions AS s "
                           "ON s.id=i.session_id WHERE s.id IS NULL",
                           -1, &rawOrphans, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> orphans(rawOrphans,
                                                                             sqlite3_finalize);
    require(orphanQuery == SQLITE_OK && sqlite3_step(orphans.get()) == SQLITE_ROW &&
                sqlite3_column_int(orphans.get(), 0) == 0 &&
                sqlite3_step(orphans.get()) == SQLITE_DONE,
            "Import items leaked without their parent session");
    return result;
}
QSet<QString> ids(const std::vector<Session> &rows) {
    QSet<QString> result;
    for (const auto &row : rows)
        result.insert(row.id);
    return result;
}
Session newSession(Context &c, const QSet<QString> &before) {
    std::vector<Session> added;
    for (const auto &row : sessions(c)) {
        if (!before.contains(row.id))
            added.push_back(row);
    }
    require(added.size() == 1, "One Import UI action did not create exactly one session UUID");
    return added.front();
}
luma::ImportJournal publishedCheckpoint(Context &c, const QString &destination, int itemCount,
                                        int previouslyImported) {
    auto &d = *c.driver;
    d.wait([&] { return c.storageFault.writeBlocked(); },
           "first Copy published and entered its actual catalog WAL commit");
    const luma::Catalog catalog(c.catalogFile.toStdString());
    const auto unfinished = catalog.unfinishedImports();
    require(c.library.importing() && d.item("library.cancelImport")->isVisible() &&
                unfinished.size() == 1 && unfinished.front().mode == luma::ImportMode::copy &&
                unfinished.front().items.size() == static_cast<std::size_t>(itemCount) &&
                catalog.libraryCount() == previouslyImported,
            "Import boundary missed the first uncommitted published Copy");
    const auto &journal = unfinished.front();
    const auto &first = journal.items.front();
    require(journal.destination == std::filesystem::canonical(destination.toStdString()) &&
                first.index == 0 && first.state == "staged" &&
                first.target.parent_path().parent_path() ==
                    journal.destination / ("Luma-" + journal.id) &&
                first.target.parent_path().filename() == "0" &&
                QFileInfo::exists(QString::fromStdString(first.temporary.string())) &&
                hash(QString::fromStdString(first.target.string())) ==
                    hash(QString::fromStdString(first.source.string())),
            "Held Copy boundary lacks the exact owned first-file staged/published evidence");
    for (std::size_t index = 1; index < journal.items.size(); ++index)
        require(journal.items[index].index == static_cast<int>(index) &&
                    journal.items[index].state == "queued" && journal.items[index].target.empty(),
                "Import advanced into a later source before the claimed boundary");
    d.log({{"action", "copy_first_published_commit_held"},
           {"session", QString::fromStdString(journal.id)},
           {"staged_index", first.index},
           {"source", QString::fromStdString(first.source.string())},
           {"target", QString::fromStdString(first.target.string())},
           {"queued_after_first", itemCount - 1}});
    return journal;
}
void terminal(Context &c, const QString &sessionID, const QString &state, int count,
              std::int64_t added, std::int64_t duplicates, std::int64_t failed,
              std::int64_t cancelled) {
    const auto rows = sessions(c);
    const auto found = std::find_if(rows.begin(), rows.end(),
                                    [&](const auto &row) { return row.id == sessionID; });
    require(found != rows.end() && found->state == state && found->finished &&
                found->itemCount == count && found->pendingItems == 0 && found->missingAssets == 0,
            "Import terminal session has unfinished items or missing asset references");
    const luma::Catalog catalog(c.catalogFile.toStdString());
    const auto summary = catalog.importSummary(sessionID.toStdString());
    require(summary.added == added && summary.duplicates == duplicates &&
                summary.failed == failed && summary.cancelled == cancelled &&
                added + duplicates + failed + cancelled == count,
            "Import terminal summary lost, duplicated or misclassified files");
    require(catalog.unfinishedImports().empty(), "Settled Import leaked a running journal");
}
void copiedRows(Context &c, int expectedCount) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    require(catalog.libraryCount() == expectedCount && c.library.count() == expectedCount,
            "Import model/catalog cardinalities disagree");
    const auto rows = catalog.libraryPage({}, 0, expectedCount + 1);
    require(rows.size() == static_cast<std::size_t>(expectedCount), "Import row count truncated");
    QSet<QString> paths;
    for (const auto &row : rows) {
        const auto path = QString::fromStdString(row.path.string());
        require(path.startsWith(c.root + '/') && !paths.contains(path) && QFileInfo::exists(path) &&
                    hash(path).toHex().toStdString() == row.checksum,
                "Copy left an aliased, missing or incomplete catalog original");
        paths.insert(path);
    }
    QDirIterator iterator(c.root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    int physicalCopies = 0;
    while (iterator.hasNext()) {
        const auto path = iterator.next();
        require(!QFileInfo(path).fileName().startsWith(".luma-copy-"),
                "Settled Copy leaked a temporary source copy");
        if (path.startsWith(c.root + "/copies") && path.endsWith(".png")) {
            ++physicalCopies;
            require(paths.contains(path), "Copy left a published image without a catalog asset");
        }
    }
    require(physicalCopies == expectedCount,
            "Physical published Copy count disagrees with complete catalog assets");
    require(c.library.error().isEmpty() && c.storageFault.rejectedWrites() == 0,
            "Import failed through an unrelated catalog/VFS error");
}
void report(Context &c, const QStringList &failedPaths) {
    auto &d = *c.driver;
    for (const auto &path : failedPaths)
        require(c.library.importReport().contains(path),
                "Import errors omit the failed source path: " + path);
    d.click("library.importReportButton");
    d.wait([&] { return d.object("library.importReportDialog")->property("opened").toBool(); },
           "extra Import error report opened");
    const auto text = d.object("library.importReportText")->property("text").toString();
    for (const auto &path : failedPaths)
        require(text.contains(path), "Visible Import report lost a failed source path");
    d.key(Qt::Key_Escape);
    d.wait([&] { return !d.object("library.importReportDialog")->property("visible").toBool(); },
           "extra Import error report closed");
}
void deniedDirectory(const QString &path) {
    errno = 0;
    DIR *directory = ::opendir(QFile::encodeName(path).constData());
    const int observed = errno;
    if (directory)
        ::closedir(directory);
    require(!directory && (observed == EACCES || observed == EPERM),
            "Environment cannot establish an inaccessible own subfolder fixture");
}
void deniedCreate(const QString &directory) {
    const auto path = QFile::encodeName(directory + "/ux-write-denial-probe");
    errno = 0;
    const int result = ::mkdir(path.constData(), 0700);
    const int observed = errno;
    if (result == 0)
        ::rmdir(path.constData());
    require(result == -1 && (observed == EACCES || observed == EPERM || observed == EROFS),
            "Environment did not enforce read-only destination directory permissions");
}
} // namespace

bool importExtraScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-IMP-005", "UX-IMP-010", "UX-IMP-011", "UX-IMP-012",
                                         "UX-IMP-019"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    require(sessions(c).empty() && c.library.count() == 0,
            "Extra Import case needs a fresh isolated catalog");
    if (id == "UX-IMP-010") {
        require(::geteuid() != 0, "Inaccessible-folder fixture cannot be qualified as root");
        const auto root = c.root + "/permission-tree";
        const auto locked = root + "/m-locked";
        require(QDir().mkpath(locked), "Cannot prepare owned inaccessible folder");
        const auto input = sources(c, 3);
        const QStringList destinations{root + "/a-readable.png", locked + "/hidden.png",
                                       root + "/z-readable.png"};
        for (int index = 0; index < destinations.size(); ++index)
            require(QFile::copy(input[index].toLocalFile(), destinations[index]),
                    "Cannot prepare permission tree image");
        DirectoryModes modes;
        modes.change(locked, 0000);
        deniedDirectory(locked); // Proven OS refusal, not permission-bit assumption.
        start(c, {QUrl::fromLocalFile(root)});
        settled(c);
        modes.restore();
        d.log({{"action", "inaccessible_subfolder_result"},
               {"valid_files", c.library.count()},
               {"error", c.library.error()},
               {"report", c.library.importReport()},
               {"denied_path", locked}});
        // Existing throwing directory discovery is expected to expose a product
        // regression here; do not weaken this oracle to accept a whole-job abort.
        require(c.library.count() == 2 && c.library.importReport().contains(locked),
                "Inaccessible child aborted valid siblings or is absent from the errors report; "
                "count=" +
                    QString::number(c.library.count()) + "; error=" + c.library.error());
        report(c, {locked});
        const auto all = sessions(c);
        require(all.size() == 1 && all.front().state == "completed" &&
                    all.front().pendingItems == 0 && all.front().finished &&
                    luma::Catalog(c.catalogFile.toStdString()).unfinishedImports().empty(),
                "Inaccessible child left an unfinished import session");
        terminal(c, all.front().id, "completed", 3, 2, 0, 1, 0);
        const auto problems =
            luma::Catalog(c.catalogFile.toStdString()).importProblems(all.front().id.toStdString());
        const auto prefix = locked.toStdString() + "\n";
        require(problems.size() == 1 && problems.front().starts_with(prefix) &&
                    problems.front().size() > prefix.size(),
                "Denied directory is absent from the durable failed import journal");
        for (const auto &row : luma::Catalog(c.catalogFile.toStdString()).libraryPage({}, 0, 10))
            require(QFileInfo::exists(QString::fromStdString(row.path.string())),
                    "Inaccessible-folder import left a half row");
        return true;
    }
    if (id == "UX-IMP-011" || id == "UX-IMP-012") {
        require(id != "UX-IMP-012" || ::geteuid() != 0,
                "Read-only destination permissions cannot be qualified as root");
        const auto input = ownedSources(c, 3);
        const auto destination = c.root + "/copies";
        const ReleaseWrites release{c.storageFault};
        c.storageFault.holdWritesAfterCopy(destination.toStdString());
        start(c, input, destination);
        const auto journal = publishedCheckpoint(c, destination, 3, 0);
        const auto sessionID = QString::fromStdString(journal.id);
        const auto secondSource = QString::fromStdString(journal.items[1].source.string());
        const auto thirdSource = QString::fromStdString(journal.items[2].source.string());
        if (id == "UX-IMP-011") {
            require(secondSource.startsWith(c.root + "/owned-sources/") &&
                        QFile::remove(secondSource) && !QFileInfo::exists(secondSource),
                    "Cannot remove only the queued owned source at the established boundary");
            d.log({{"action", "queued_owned_source_removed_mid_import"},
                   {"session", sessionID},
                   {"queued_index", 1},
                   {"source", secondSource}});
            c.storageFault.holdWrites(false);
            settled(c);
            terminal(c, sessionID, "completed", 3, 2, 0, 1, 0);
            copiedRows(c, 2);
            report(c, {secondSource});
        } else {
            const auto sessionDirectory = QString::fromStdString(
                journal.items.front().target.parent_path().parent_path().string());
            DirectoryModes modes;
            // Keep the already-published index-0 directory writable so its current
            // CopyCleanup can unlink its own temporary hard link. New per-file
            // directories are denied at the existing destination/session parents.
            modes.change(destination, 0555);
            modes.change(sessionDirectory, 0555);
            deniedCreate(destination);
            deniedCreate(sessionDirectory);
            d.log({{"action", "copy_destination_parents_read_only"},
                   {"session", sessionID},
                   {"destination", destination},
                   {"session_directory", sessionDirectory},
                   {"boundary", "first_commit_held"}});
            c.storageFault.holdWrites(false);
            settled(c);
            modes.restore();
            terminal(c, sessionID, "completed", 3, 1, 0, 2, 0);
            copiedRows(c, 1);
            require(c.library.importReport().contains(sessionDirectory),
                    "Copy report omitted the denied destination path");
            report(c, {secondSource, thirdSource});
        }
        require(d.item("library.importButton")->isEnabled(),
                "Per-file failure left the Import workflow blocked");
        return true;
    }
    const bool stress = id == "UX-IMP-019";
    const int cycles = stress ? 50 : 1;
    constexpr int count = 10;
    const auto input = sources(c, count);
    QList<QByteArray> originalHashes;
    for (const auto &url : input)
        originalHashes.append(hash(url.toLocalFile()));
    QElapsedTimer elapsed;
    elapsed.start();
    int expectedRows = 0;
    for (int cycle = 0; cycle < cycles; ++cycle) {
        const auto before = ids(sessions(c));
        const auto destination = c.root + QString("/copies-%1").arg(cycle, 3, 10, QChar('0'));
        {
            const ReleaseWrites release{c.storageFault};
            // Fresh per-cycle directory is required: an existing published .png
            // would arm this VFS checkpoint too early at beginImport instead.
            require(!QFileInfo::exists(destination), "Stress Copy destination is not fresh");
            c.storageFault.holdWritesAfterCopy(destination.toStdString());
            start(c, input, destination, false);
            const auto journal = publishedCheckpoint(c, destination, count, expectedRows);
            const auto sessionID = QString::fromStdString(journal.id);
            const int clicks = stress ? 3 : 20;
            for (int click = 0; click < clicks; ++click) {
                d.click("library.cancelImport");
                require(c.library.importing() && c.storageFault.writeBlocked(),
                        "Repeated Cancel left the held import boundary unexpectedly");
            }
            const auto held = luma::Catalog(c.catalogFile.toStdString()).unfinishedImports();
            require(held.size() == 1 && held.front().id == journal.id &&
                        ids(sessions(c)).size() == before.size() + 1,
                    "Repeated Cancel replaced/duplicated the active import session");
            c.storageFault.holdWrites(false);
            settled(c);
            ++expectedRows;
            terminal(c, sessionID, "cancelled", count, 1, 0, 0, count - 1);
            copiedRows(c, expectedRows);
            require(c.library.importReport().isEmpty() &&
                        c.library.status().contains("cancelled: 9"),
                    "Repeated Cancel produced spurious errors or inconsistent cancelled counts");
        }
        if (stress) {
            const auto previous = ids(sessions(c));
            start(c, input, destination, true);
            settled(c);
            const auto completed = newSession(c, previous);
            const int newlyAdded = cycle == 0 ? count - 1 : 0;
            terminal(c, completed.id, "completed", count, newlyAdded, count - newlyAdded, 0, 0);
            expectedRows += newlyAdded;
            copiedRows(c, expectedRows);
            require(c.library.importReport().isEmpty(), "Duplicate reimport produced false errors");
        }
        const auto all = sessions(c);
        require(all.size() == static_cast<std::size_t>((cycle + 1) * (stress ? 2 : 1)),
                "Import cycle created extra or lost terminal journal sessions");
        for (const auto &row : all)
            require(row.finished && row.state != "running" && row.itemCount == count &&
                        row.pendingItems == 0 && row.missingAssets == 0,
                    "Import/cancel/reimport retained a partial or unfinished journal");
        for (int index = 0; index < input.size(); ++index)
            require(hash(input[index].toLocalFile()) == originalHashes[index],
                    "Import/cancel/reimport changed a shared public source");
        c.checkData();
        d.log({{"action", "import_cycle_integrity_checkpoint"},
               {"cycle", cycle + 1},
               {"terminal_sessions", static_cast<int>(all.size())},
               {"complete_catalog_assets", expectedRows},
               {"pending_items", 0}});
    }
    require(expectedRows == (stress ? 59 : 1), "Import-cycle final asset count is not exact");
    writeJson(c.root + "/measurements.json", {{"scenario", id},
                                              {"cycles", cycles},
                                              {"terminal_sessions", stress ? 100 : 1},
                                              {"journal_items", stress ? 1000 : 10},
                                              {"cancel_clicks_per_active_job", stress ? 3 : 20},
                                              {"final_assets", expectedRows},
                                              {"elapsed_ms", elapsed.elapsed()},
                                              {"native_file_selection_exercised", false}});
    return true;
}
} // namespace ux
