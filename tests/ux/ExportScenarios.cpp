#include "tests/ux/UxContext.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <filesystem>

namespace ux {
namespace {
void settings(Context &c, const QString &destination, const QString &name = "{filename}",
              int format = 1, int conflict = 1) {
    auto &d = *c.driver;
    d.click("exportButton");
    d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
           "export settings opened");
    d.type("export.destination", destination);
    d.type("exportNaming", name);
    d.choose("export.conflict", conflict);
    d.choose("exportFormat", format);
}
void start(Context &c, bool selection = false) {
    c.driver->click(selection ? "export.selection" : "export.current");
    require(c.exporter.working(), "Export did not start: " + c.exporter.error());
    require(c.driver->item("export.progress")->isVisible(), "Export progress is invisible");
}
void completed(Context &c, int count) {
    c.driver->wait([&] { return !c.exporter.working(); }, "export queue idle", 60000);
    require(c.exporter.error().isEmpty() && c.exporter.failures().isEmpty(),
            "Export reported failures: " + c.exporter.error() + c.exporter.failures().join(';'));
    require(c.exporter.total() == count && c.exporter.completed() == count &&
                c.exporter.succeeded() + c.exporter.skipped() == count,
            "Queue counters disagree");
}
void noTemporaryFiles(Context &c) {
    QDirIterator it(c.root + "/exports", QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto name = QFileInfo(it.next()).fileName();
        require(!name.endsWith(".tmp") && !name.startsWith(".luma-"),
                "Export leaked temporary output: " + name);
    }
}
void close(Context &c) {
    c.driver->quitShortcut();
    c.driver->wait(
        [&] { return !c.window->isVisible() && !c.exporter.working() && !c.editor.saving(); },
        "quit during export", 60000);
    c.checkData();
}
} // namespace
bool exportScenario(Context &c, const QString &id) {
    auto &d = *c.driver;
    if (id == "UX-EXP-001") {
        c.openFirst();
        const auto before = c.editor.currentSettings();
        for (int i = 0; i < 50; ++i) {
            d.click("exportButton");
            d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
                   "export settings opened");
            d.click("export.close");
            d.wait([&] { return !d.object("exportPanel")->property("visible").toBool(); },
                   "export settings closed");
        }
        require(!c.exporter.working() && c.editor.currentSettings() == before,
                "Opening settings mutated state");
        return true;
    }
    if (id == "UX-EXP-002" || id == "UX-EXP-003") {
        c.openFirst();
        settings(c, id == "UX-EXP-002" ? "relative-folder" : c.root + "/exports",
                 id == "UX-EXP-003" ? QString(2000, 'x') : "{filename}");
        d.click("export.current");
        require(!c.exporter.working() && !c.exporter.error().isEmpty() &&
                    d.object("exportPanel")->property("visible").toBool(),
                "Invalid export input lacks a recoverable error");
        d.click("export.close");
        return true;
    }
    if (id == "UX-EXP-004" || id == "UX-EXP-012" || id == "UX-EXP-013" || id == "UX-EXP-029" ||
        id == "UX-EXP-030" || id == "UX-EXP-015") {
        c.openFirst();
        const auto folder =
            c.root + (id == "UX-EXP-029" ? QString::fromUtf8("/Экспорт-🌅") : QString("/exports"));
        require(QDir().mkpath(folder), "Cannot create export fixture folder");
        if (id == "UX-EXP-015") {
            const auto alias = c.root + "/source-alias";
            std::filesystem::create_directory_symlink(c.fixture.toStdString(), alias.toStdString());
            require(std::filesystem::canonical((alias + "/./").toStdString()) ==
                        std::filesystem::canonical(c.fixture.toStdString()),
                    "Alias fixture does not resolve to protected original folder");
            settings(c, alias + "/./", "Photo-0000", 1, 2);
            d.click("export.current");
            d.wait([&] { return !c.exporter.working(); }, "protected original export refused");
            require((!c.exporter.error().isEmpty() || !c.exporter.failures().isEmpty()) &&
                        c.exporter.succeeded() == 0,
                    "Export accepted replacing an original through a symlink");
            return true;
        }
        const auto name = id == "UX-EXP-029" ? QString::fromUtf8("Снимок-🌅") : QString("Photo");
        const int count = id == "UX-EXP-030" ? 5 : 1;
        for (int format = 0; format < count; ++format) {
            settings(c, folder, name, count == 5 ? format : 1);
            start(c);
            completed(c, 1);
            const auto extension = count == 5
                                       ? QStringList{"jpg", "png", "tif", "tif", "dng"}[format]
                                       : QString("png");
            const auto matches = QDir(folder).entryList({name + "*." + extension}, QDir::Files);
            require(!matches.isEmpty() && QFileInfo(folder + '/' + matches.back()).size() > 0,
                    "Export output missing");
        }
        if (id == "UX-EXP-012" || id == "UX-EXP-013") {
            settings(c, folder, name, 1, id == "UX-EXP-012" ? 0 : 1);
            start(c);
            completed(c, 1);
            require(c.exporter.skipped() == (id == "UX-EXP-012" ? 1 : 0),
                    "Collision policy has wrong result");
            require(QDir(folder).entryList({"*.png"}, QDir::Files).size() ==
                        (id == "UX-EXP-012" ? 1 : 2),
                    "Collision policy overwrote/duplicated output");
        }
        noTemporaryFiles(c);
        return true;
    }
    if (id == "UX-EXP-005" || id == "UX-EXP-006" || id == "UX-EXP-007" || id == "UX-EXP-008" ||
        id == "UX-EXP-010" || id == "UX-EXP-018" || id == "UX-APP-009") {
        c.import(100, id == "UX-EXP-010");
        d.click("library.selectAll");
        d.wait([&] { return !c.library.selecting() && c.library.selectedCount() == 100; },
               "export selection complete");
        require(QDir().mkpath(c.root + "/exports"), "Cannot create queue fixture folder");
        settings(c, c.root + "/exports");
        if (id == "UX-EXP-010") {
            const auto rows =
                luma::Catalog((c.root + "/catalog.sqlite").toStdString()).libraryPage({}, 0, 100);
            const auto source = QString::fromStdString(rows.front().path.string());
            require(source.startsWith(c.root + "/copies/") && QFile::remove(source),
                    "Cannot remove private per-test source copy");
        }
        start(c, true);
        if (id == "UX-EXP-006" || id == "UX-EXP-007" || id == "UX-EXP-008") {
            d.click("export.pause");
            d.wait([&] { return c.exporter.paused(); }, "pause after current file");
            const auto stopped = c.exporter.completed();
            require(stopped < 100 && c.exporter.working() && d.item("export.resume")->isVisible(),
                    "Pause incorrectly completed/stopped queue");
            d.frame();
            d.frame();
            require(c.exporter.completed() == stopped, "Paused queue kept completing files");
            if (id == "UX-EXP-008") {
                d.click("export.cancel");
                d.wait([&] { return !c.exporter.working(); }, "cancel paused queue");
                require(c.exporter.completed() == stopped, "Cancel resumed a paused queue");
            } else {
                d.click("export.resume");
                completed(c, 100);
            }
        } else if (id == "UX-EXP-010") {
            d.wait([&] { return !c.exporter.working(); }, "queue continues after a failed source");
            require(c.exporter.total() == 100 && c.exporter.completed() == 100 &&
                        c.exporter.succeeded() == 99 && c.exporter.failures().size() == 1,
                    "One missing source stopped later exports or disappeared from the report");
            require(d.item("export.failures")->isVisible() &&
                        !d.object("export.failures")->property("text").toString().isEmpty(),
                    "Per-file failure has no visible report");
        } else if (id == "UX-EXP-018" || id == "UX-APP-009")
            close(c);
        else
            completed(c, 100);
        noTemporaryFiles(c);
        return true;
    }
    return false;
}
} // namespace ux
