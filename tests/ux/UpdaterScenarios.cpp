#include "tests/ux/UxContext.h"
#include <QDir>
#include <QUrl>
#include <memory>

namespace ux {
namespace {
struct InstallProbe {
    int calls = 0;
    bool ready = false;
    QString failure;
};
struct ReleaseWrites {
    SqliteFault &fault;
    ~ReleaseWrites() {
        fault.holdWrites(false);
    }
};
std::shared_ptr<InstallProbe> requestInstallation(Context &c) {
    auto probe = std::make_shared<InstallProbe>();
    // Public native SDK callback boundary. No package is downloaded or installed.
    c.driver->log({{"action", "native_install_callback_fixture"}});
    c.updates.postponeInstallation([probe, &c] {
        ++probe->calls;
        try {
            probe->ready = !c.editor.transformAutoBusy() && !c.editor.busy() &&
                           !c.editor.saving() && !c.editor.exporting() && !c.library.working() &&
                           !c.batch.working() && !c.exporter.working();
            require(probe->ready, "Installer callback ran before active operations settled");
            if (c.editor.hasPhoto()) {
                const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
                require(active && active->edits == c.editor.currentSettings(),
                        "Installer callback preceded durable autosave");
            }
        } catch (const std::exception &error) {
            probe->failure = QString::fromUtf8(error.what());
        }
    });
    require(c.updates.restartPending() && probe->calls == 0,
            "Installation did not defer to the event loop");
    return probe;
}
void installed(Context &c, const std::shared_ptr<InstallProbe> &probe) {
    c.driver->wait([&] { return probe->calls > 0; }, "guarded installer callback delivered", 60000);
    require(probe->calls == 1 && probe->ready && probe->failure.isEmpty(),
            "Unsafe or duplicate installer callback: " + probe->failure);
}
void stillWaiting(Context &c, const std::shared_ptr<InstallProbe> &probe) {
    const auto samples = c.metrics.json()["rss_samples"].toInteger();
    c.driver->wait([&] { return c.metrics.json()["rss_samples"].toInteger() >= samples + 2; },
                   "main loop remains live while installer is deferred");
    require(probe->calls == 0, "Installer did not wait for a blocked active operation");
}
} // namespace
bool updaterScenario(Context &c, const QString &id, const QString &variant) {
    if (id != "UX-UPD-002" && id != "UX-UPD-003" && id != "UX-UPD-004")
        return false;
    auto &d = *c.driver;
    if (id == "UX-UPD-002") {
        c.openFirst();
        c.saved();
        c.reveal("develop.exposureSlider");
        [[maybe_unused]] const ReleaseWrites release{c.storageFault};
        c.storageFault.holdWrites(true);
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
        require(c.editor.saving(), "Updater autosave fixture missed pending save");
        const auto probe = requestInstallation(c);
        d.wait([&] { return c.storageFault.writeBlocked(); }, "autosave reached actual WAL write");
        stillWaiting(c, probe);
        c.storageFault.holdWrites(false);
        installed(c, probe);
        return true;
    }
    const auto operation = id == "UX-UPD-004" ? QString("import") : variant;
    if (operation == "import") {
        QList<QUrl> sources;
        for (int i = 0; i < 1000; ++i)
            sources.append(QUrl::fromLocalFile(c.fixture +
                                               QString("/Photo-%1.png").arg(i, 4, 10, QChar('0'))));
        auto *pane = d.object("library.pane");
        pane->setProperty("sources", QVariant::fromValue(sources));
        require(QDir().mkpath(c.root + "/copies"), "Cannot create Copy destination");
        pane->setProperty("destination", QUrl::fromLocalFile(c.root + "/copies"));
        d.click("library.importButton");
        d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
               "Copy dialog opened");
        d.choose("library.importMode", 1);
        d.click("library.importStart");
        require(c.library.importing(), "Copy fixture completed before updater boundary");
        const auto probe = requestInstallation(c);
        d.wait([&] { return d.item("restart.cancelImport")->isVisible(); },
               "restart modal offers import cancellation");
        d.click("restart.cancelImport");
        installed(c, probe);
        require(!c.library.working(), "Installer ran while Copy was still active");
        return true;
    }
    require(operation == "batch" || operation == "export", "Unknown updater operation variant");
    c.import(1000);
    d.click("library.selectAll");
    d.wait([&] { return !c.library.selecting() && c.library.selectedCount() == 1000; },
           "updater selection prepared");
    if (operation == "batch") {
        d.choose("batchPresetChoice", 1);
        [[maybe_unused]] const ReleaseWrites release{c.storageFault};
        c.storageFault.holdWrites(true);
        d.click("batchApplyPreset");
        d.wait([&] { return c.storageFault.writeBlocked() && c.batch.working(); },
               "batch entered actual WAL write");
        const auto probe = requestInstallation(c);
        stillWaiting(c, probe);
        c.storageFault.holdWrites(false);
        installed(c, probe);
    } else {
        require(QDir().mkpath(c.root + "/exports"), "Cannot create export destination");
        d.click("exportButton");
        d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
               "Export panel opened");
        d.type("export.destination", c.root + "/exports");
        d.choose("exportFormat", 1);
        d.click("export.selection");
        require(c.exporter.working(), "Export fixture completed before updater boundary");
        const auto probe = requestInstallation(c);
        installed(c, probe);
    }
    return true;
}
} // namespace ux
