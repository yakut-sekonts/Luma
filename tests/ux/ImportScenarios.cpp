#include "tests/ux/UxContext.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>

namespace ux {
namespace {
QList<QUrl> files(Context &c, int count) {
    QList<QUrl> result;
    for (int i = 0; i < count; ++i)
        result.append(
            QUrl::fromLocalFile(c.fixture + QString("/Photo-%1.png").arg(i, 4, 10, QChar('0'))));
    return result;
}
void start(Context &c, const QList<QUrl> &sources, const QString &destination = {},
           bool skip = false) {
    auto &d = *c.driver;
    if (!c.window->property("libraryMode").toBool())
        d.click("main.libraryButton");
    // Native file-panel selection is a fixture boundary; all subsequent UI is real.
    d.object("library.pane")->setProperty("sources", QVariant::fromValue(sources));
    if (!destination.isEmpty()) {
        require(QDir().mkpath(destination), "Cannot create import destination fixture");
        d.object("library.pane")->setProperty("destination", QUrl::fromLocalFile(destination));
    }
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "import settings opened");
    d.choose("library.importMode", destination.isEmpty() ? 0 : 1);
    if (d.object("library.importDuplicates")->property("checked").toBool() != skip)
        d.click("library.importDuplicates");
    d.click("library.importStart");
}
void settled(Context &c) {
    c.driver->wait([&] { return !c.library.working() && !c.library.loading(); }, "import settled",
                   60000);
    c.checkData();
}
QByteArray hash(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot hash import fixture: " + path);
    QCryptographicHash digest(QCryptographicHash::Sha256);
    require(digest.addData(&file), "Cannot read complete import fixture");
    return digest.result();
}
void noTemporaryFiles(Context &c) {
    QDirIterator it(c.root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto path = it.next();
        require(!QFileInfo(path).fileName().startsWith(".luma-copy-"),
                "Cancelled import leaked its temporary file: " + path);
    }
}
} // namespace
bool copyRestartScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-IMP-020")
        return false;
    auto &d = *c.driver;
    const auto destination = c.stateRoot + "/copies";
    if (stage == "prepare") {
        struct ReleaseWrites {
            SqliteFault &fault;
            ~ReleaseWrites() {
                fault.holdWrites(false);
            }
        };
        [[maybe_unused]] const ReleaseWrites release{c.storageFault};
        c.storageFault.holdWritesAfterCopy(destination.toStdString());
        start(c, files(c, 100), destination);
        d.wait([&] { return c.storageFault.writeBlocked(); },
               "Copy published a file and entered its catalog WAL commit");
        const auto journals = luma::Catalog(c.catalogFile.toStdString()).unfinishedImports();
        require(journals.size() == 1 && journals.front().items.size() == 100,
                "Copy interruption has no durable import journal");
        const auto &journal = journals.front();
        const auto &item = journal.items.front();
        const auto target = QString::fromStdString(item.target.string());
        const auto source = QString::fromStdString(item.source.string());
        require(QFileInfo::exists(target) && hash(target) == hash(source),
                "Copy checkpoint has no intact published file");
        require(luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 0,
                "Copy checkpoint missed the first uncommitted asset");
        writeJson(c.stateRoot + "/expected-copy.json",
                  {{"session", QString::fromStdString(journal.id)},
                   {"target", target},
                   {"source", source},
                   {"sha256", QString::fromLatin1(hash(source).toHex())}});
        d.log({{"action", "copy_published_before_catalog_commit"}, {"target", target}});
        writeJson(c.root + "/metrics.json", c.metrics.json());
        writeJson(c.root + "/kill-ready.json", {{"id", id},
                                                {"stage", stage},
                                                {"pid", QCoreApplication::applicationPid()},
                                                {"utc_ms", QDateTime::currentMSecsSinceEpoch()}});
        d.wait([] { return false; }, "parent SIGKILL was not delivered", 10000);
        throw std::runtime_error("Copy kill checkpoint unexpectedly returned");
    }
    require(stage == "verify", "Invalid Copy recovery stage");
    QFile oracle(c.stateRoot + "/expected-copy.json");
    require(oracle.open(QIODevice::ReadOnly), "Missing Copy interruption oracle");
    const auto expected = QJsonDocument::fromJson(oracle.readAll()).object();
    // LibraryModel's real constructor performs recovery; the test never calls recover().
    require(!c.library.working() && !c.library.loading() && c.library.count() == 1 &&
                c.library.error().isEmpty(),
            "Startup did not recover exactly the published Copy asset");
    luma::Catalog catalog(c.catalogFile.toStdString());
    require(catalog.unfinishedImports().empty(), "Startup left the dead owner's import unfinished");
    const auto summary = catalog.importSummary(expected["session"].toString().toStdString());
    require(summary.added == 1 && summary.failed == 0 && summary.cancelled == 99,
            "Recovery summary lost or duplicated Copy journal items");
    QDirIterator iterator(destination, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    int published = 0;
    while (iterator.hasNext()) {
        const auto path = iterator.next();
        require(!QFileInfo(path).fileName().startsWith(".luma-copy-"),
                "Recovery leaked interrupted Copy temporary file");
        ++published;
        require(path == expected["target"].toString(), "Recovery published an unexpected file");
    }
    require(published == 1 && QString::fromLatin1(hash(expected["target"].toString()).toHex()) ==
                                  expected["sha256"].toString(),
            "Recovery changed published Copy bytes");
    d.doubleClick(c.card(0));
    c.loaded();
    require(c.editor.sourcePath() == expected["target"].toString(),
            "Recovered card did not open the copied file");
    return true;
}
bool importScenario(Context &c, const QString &id) {
    auto &d = *c.driver;
    if (id == "UX-IMP-001") {
        d.click("library.importButton");
        d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
               "import opened");
        d.key(Qt::Key_Escape);
        d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
               "import cancelled");
        require(c.library.count() == 0 && !c.library.working() && !c.editor.hasPhoto(),
                "Cancel import changed data");
        return true;
    }
    if (id == "UX-IMP-002" || id == "UX-IMP-003" || id == "UX-IMP-007" || id == "UX-IMP-008") {
        const bool copy = id == "UX-IMP-003";
        start(c, files(c, 100), copy ? c.root + "/copies" : QString{});
        require(c.library.importing() && d.item("library.cancelImport")->isVisible(),
                "Import has no progress/cancel UI");
        settled(c);
        require(c.library.count() == 100 && c.library.error().isEmpty(),
                "100 valid files did not import: " + c.library.error());
        if (copy) {
            const auto imported =
                luma::Catalog((c.root + "/catalog.sqlite").toStdString()).libraryPage({}, 0, 100);
            require(imported.size() == 100, "Copy catalog count mismatch");
            for (const auto &item : imported) {
                const auto path = QString::fromStdString(item.path.string());
                require(path.startsWith(c.root + "/copies/") &&
                            hash(path) ==
                                hash(c.fixture + '/' + QString::fromStdString(item.filename)),
                        "Copy changed bytes or retained the original path");
            }
        }
        if (id == "UX-IMP-007" || id == "UX-IMP-008") {
            start(c, files(c, 100), id == "UX-IMP-008" ? c.root + "/duplicates" : QString{},
                  id == "UX-IMP-007");
            settled(c);
            require(c.library.count() == (id == "UX-IMP-007" ? 100 : 200),
                    "Duplicate policy produced wrong row count");
            require(!c.library.status().isEmpty() && c.library.importReport().isEmpty(),
                    "Duplicate result lacks status or reports errors");
        }
        noTemporaryFiles(c);
        return true;
    }
    if (id == "UX-IMP-004" || id == "UX-IMP-006" || id == "UX-APP-008") {
        const int cycles = id == "UX-IMP-004" ? 3 : 1;
        const double fractions[]{.1, .5, .9};
        const int checkpoints[]{100, 500, 900};
        for (int i = 0; i < cycles; ++i) {
            struct ReleaseWrites {
                SqliteFault &fault;
                ~ReleaseWrites() {
                    fault.holdWrites(false);
                }
            } release{c.storageFault};
            const int before = c.library.count();
            const auto destination = c.root + QString("/copies-%1").arg(i);
            c.storageFault.holdWritesAfterCopy(destination.toStdString(),
                                               static_cast<std::size_t>(checkpoints[i] - 1));
            start(c, files(c, 1000), destination);
            if (id == "UX-IMP-006")
                require(!d.item("library.importButton")->isEnabled(),
                        "Concurrent Import button is enabled");
            d.wait([&] { return c.storageFault.writeBlocked() && c.library.importing(); },
                   "actual published Copy checkpoint before cancellation", 60000);
            require(c.library.importing() && c.library.progress() < 1 &&
                        d.item("library.cancelImport")->isVisible() &&
                        d.item("library.cancelImport")->isEnabled(),
                    "Held import checkpoint has no real cancellation action");
            d.log({{"action", "cancel_checkpoint"},
                   {"target", fractions[i]},
                   {"observed", c.library.progress()},
                   {"published_copy_target", checkpoints[i]},
                   {"worker_write_held", true}});
            if (id == "UX-APP-008") {
                d.quitShortcut();
                c.storageFault.holdWrites(false);
                d.wait([&] { return !c.window->isVisible() && !c.library.importing(); },
                       "quit during import", 60000);
            } else {
                d.click("library.cancelImport");
                require(c.storageFault.writeBlocked() && c.library.importing(),
                        "Cancellation did not stay within the held worker boundary");
                c.storageFault.holdWrites(false);
                settled(c);
            }
            luma::Catalog catalog((c.root + "/catalog.sqlite").toStdString());
            std::vector<luma::LibraryItem> rows;
            for (int offset = 0;; offset += 512) {
                const auto page = catalog.libraryPage({}, offset, 512);
                rows.insert(rows.end(), page.begin(), page.end());
                if (page.size() < 512)
                    break;
            }
            const int added = static_cast<int>(rows.size()) - before;
            require(added >= checkpoints[i] - 1 && added <= checkpoints[i],
                    "Cancellation did not retain exactly the known atomic Copy boundary");
            for (const auto &item : rows)
                require(QFileInfo::exists(QString::fromStdString(item.path.string())),
                        "Import left a half row with missing copied file");
            noTemporaryFiles(c);
            c.checkData();
        }
        return true;
    }
    if (id == "UX-IMP-009") {
        QString directory = c.root + "/tree";
        QList<QUrl> sources;
        for (int i = 0; i < 20; ++i) {
            directory += "/level";
            require(QDir().mkpath(directory), "Cannot create deep fixture tree");
            require(
                QFile::copy(files(c, 20)[i].toLocalFile(), directory + QString("/%1.png").arg(i)),
                "Cannot copy deep fixture");
        }
        start(c, {QUrl::fromLocalFile(c.root + "/tree")});
        settled(c);
        require(c.library.count() == 20 && c.library.error().isEmpty(),
                "Recursive import skipped deep valid files");
        return true;
    }
    if (id == "UX-IMP-013" || id == "UX-IMP-014" || id == "UX-IMP-015" || id == "UX-IMP-016") {
        const bool unsupported = id == "UX-IMP-014";
        const int badCount = id == "UX-IMP-016" ? 40 : 1;
        auto sources = files(c, 10);
        const auto directory = c.root + '/' + QString(120, 'x');
        require(QDir().mkpath(directory), "Cannot create error-path fixture");
        for (int i = 0; i < badCount; ++i) {
            const auto path =
                directory + QString("/invalid-%1.").arg(i) + (unsupported ? "txt" : "png");
            QFile file(path);
            require(file.open(QIODevice::WriteOnly) && file.write("invalid image fixture") > 0,
                    "Cannot write corrupt fixture");
            file.close();
            sources.append(QUrl::fromLocalFile(path));
        }
        start(c, sources);
        settled(c);
        require(c.library.count() == 10 && c.library.importReport().contains("invalid-0"),
                "Import failed valid files or omitted failed path");
        d.click("library.importReportButton");
        d.wait([&] { return d.object("library.importReportDialog")->property("opened").toBool(); },
               "error report opened");
        require(
            d.object("library.importReportText")->property("text").toString().contains("invalid-0"),
            "Report is empty");
        d.click("library.importReportScroll");
        d.key(Qt::Key_A, Qt::ControlModifier);
        require(
            !d.object("library.importReportText")->property("selectedText").toString().isEmpty(),
            "Report text cannot be selected");
        if (id == "UX-IMP-016") {
            auto *view = qvariant_cast<QObject *>(
                d.object("library.importReportScroll")->property("contentItem"));
            d.wheel("library.importReportScroll", -480);
            d.wait([&] { return view->property("contentY").toDouble() > 0; },
                   "error report scrolls");
        }
        d.key(Qt::Key_Escape);
        d.wait(
            [&] { return !d.object("library.importReportDialog")->property("visible").toBool(); },
            "error report closes");
        require(d.item("library.importButton")->isEnabled(), "Errors left Import blocked");
        return true;
    }
    return false;
}
} // namespace ux
