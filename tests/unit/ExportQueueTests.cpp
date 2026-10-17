#include "apps/desktop/EditorController.h"
#include "apps/desktop/ExportController.h"
#include "apps/desktop/LibraryModel.h"
#include "apps/desktop/PresetController.h"
#include "apps/desktop/UpdateController.h"
#include "core/image/Bayer.h"
#include "core/raw/RawDecoder.h"
#include "platform/macos/FileHasher.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlContext>
#include <QThread>
#include <atomic>
#include <fstream>
#include <mutex>
#include <sqlite3.h>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <typename F> void wait(F ready) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 30000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    require(ready(), "Export queue timed out.");
}
template <typename F> void rejects(F operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Invalid export settings were accepted.");
}
struct Gate {
    std::atomic<int> entered{0}, permitted{0};
    std::mutex mutex;
    std::vector<luma::AssetRecord> assets;
    std::vector<luma::ExportOptions> options;
    bool failSecond = false;
    ExportController::Runner runner() {
        return [this](const auto &asset, const auto &destination, auto settings, auto stop,
                      const auto &progress) {
            const int index = entered.fetch_add(1) + 1;
            {
                const std::lock_guard lock(mutex);
                assets.push_back(asset);
                options.push_back(settings);
            }
            progress(.25);
            while (permitted.load() < index) {
                luma::checkCancellation(stop);
                QThread::msleep(1);
            }
            luma::checkCancellation(stop);
            if (failSecond && index == 2)
                throw std::runtime_error("Injected encoder error.");
            std::ofstream file(destination);
            file << asset.edits.exposure;
            file.close();
            require(bool(file), "Cannot write queue fixture output.");
            progress(1);
        };
    }
};
void settingsTests(const QString &destination) {
    luma::ExportSettings settings;
    settings.destination = destination;
    require(luma::ExportSettings::fromJson(settings.json()).json() == settings.json(),
            "Export settings roundtrip changed defaults.");
    require(settings.filename("photo.raw", 1, "2026-10-14") == "photo.jpg",
            "Default export naming changed.");
    settings.naming = "{date}_{sequence}_{filename}";
    require(settings.filename("Фото.NEF", 12, "2026-10-14") == "2026-10-14_0012_Фото.jpg",
            "Export naming tokens changed.");
    const auto literal = QString::fromUtf8("Снимок-%{filename}-{sequence}-{date}-🌅");
    require(settings.filename(literal + ".png", 12, "2026-10-14") ==
                "2026-10-14_0012_" + literal + ".jpg",
            "Export reinterpreted naming tokens inside the source filename.");
    settings.naming = "{filename}-{filename}-{date}-{sequence}";
    require(settings.filename(literal + ".png", 7, "date-{sequence}") ==
                literal + "-" + literal + "-date-{sequence}-0007.jpg",
            "Export recursively expanded repeated source/date replacement values.");
    for (const auto &name : {"../{filename}", "{unknown}", "a/b", "a:b", ""}) {
        auto invalid = settings;
        invalid.naming = QString::fromUtf8(name);
        rejects([&] { invalid.validate(); });
    }
    auto json = settings.json();
    json.insert("quality", 1.5);
    rejects([&] { luma::ExportSettings::fromJson(json); });
    json = settings.json();
    json.insert("format", "4");
    rejects([&] { luma::ExportSettings::fromJson(json); });
    json = settings.json();
    json.insert("unknown", 0);
    rejects([&] { luma::ExportSettings::fromJson(json); });
    json = settings.json();
    json.remove("metadata");
    rejects([&] { luma::ExportSettings::fromJson(json); });
}
void qmlTest(ExportController &exporter, EditorController &editor, LibraryModel &library,
             PresetController &presets, const QString &qmlPath) {
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("exporter", &exporter);
    engine.rootContext()->setContextProperty("editor", &editor);
    engine.rootContext()->setContextProperty("library", &library);
    engine.rootContext()->setContextProperty("presets", &presets);
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    QQmlComponent component(&engine);
    component.setData(("import QtQuick\nimport QtQuick.Controls\nimport \"" +
                       QUrl::fromLocalFile(qmlPath).toString() +
                       "\"\nApplicationWindow { width: 1000; height: 850; visible: true; "
                       "ExportPanel { id: exportPanel; Component.onCompleted: open() } }")
                          .toUtf8(),
                      QUrl());
    wait([&] { return component.status() != QQmlComponent::Loading; });
    if (component.isError())
        throw std::runtime_error(component.errorString().toStdString());
    const std::unique_ptr<QObject> window(component.create());
    require(bool(window), "Export panel did not load.");
    QCoreApplication::processEvents();
    auto *panel = window->findChild<QObject *>("exportPanel");
    auto *format = window->findChild<QObject *>("exportFormat");
    auto *naming = window->findChild<QObject *>("exportNaming");
    require(panel && format && naming, "Export panel controls are missing.");
    require(format->setProperty("currentIndex", 4), "Cannot select DNG in QML.");
    require(QMetaObject::invokeMethod(format, "activated", Q_ARG(int, 4)),
            "Cannot activate export format.");
    naming->setProperty("text", "QML-{sequence}");
    require(QMetaObject::invokeMethod(naming, "editingFinished"), "Cannot edit QML naming.");
    require(exporter.configure(panel->property("form").toMap()),
            "Cannot apply actual export QML settings.");
    require(exporter.settings().value("format").toInt() == 4 &&
                exporter.settings().value("naming").toString() == "QML-{sequence}",
            "QML did not update export settings.");
    if (!warnings.isEmpty())
        throw std::runtime_error(warnings.join('\n').toStdString());
}
} // namespace
void exportQueueTests(const QString &source, const QString &root, const QString &qmlPath) {
    settingsTests(root);
    const auto directory = std::filesystem::path(root.toStdString()) / "export-queue";
    std::filesystem::create_directories(directory / "output");
    const auto catalog = directory / "catalog.sqlite";
    std::vector<std::string> ids;
    {
        luma::Catalog db(catalog);
        const auto checksum = luma::macos::sha256File(source.toStdString());
        const auto metadata = luma::RawDecoder().inspect(source.toStdString());
        for (int i = 0; i < 3; ++i) {
            const auto path = directory / ("photo-" + std::to_string(i) + ".DNG");
            std::filesystem::copy_file(source.toStdString(), path);
            auto asset = db.addAndSelect(path, checksum, metadata);
            asset.edits.exposure = i;
            db.saveEdits(asset.id, asset.edits);
            ids.push_back(asset.id);
        }
    }
    PreviewProvider provider;
    EditorController editor(catalog, &provider);
    LibraryModel library(catalog);
    PresetController presets(QString::fromStdString((directory / "presets.json").string()), editor);
    Gate gate;
    gate.failSecond = true;
    ExportController exporter(catalog, editor, library, gate.runner());
    auto config = exporter.settings();
    config["destination"] = QString::fromStdString((directory / "output").string());
    require(exporter.configure(config), "Cannot configure export queue.");
    exporter.saveProfile("Web JPEG");
    require(exporter.profiles().size() == 1, "Export profile was not saved.");
    const auto profileID = exporter.profiles().first().toMap().value("id").toString();
    qmlTest(exporter, editor, library, presets, qmlPath);
    exporter.loadProfile(profileID);
    require(exporter.settings() == config, "Export profile did not restore every setting.");
    {
        ExportController restored(catalog, editor, library, gate.runner());
        require(restored.settings() == config && restored.profiles().size() == 1,
                "Export settings were not restored after restart.");
        auto changed = config;
        changed["quality"] = 91;
        require(restored.configure(changed), "Cannot save second export instance.");
        require(!exporter.configure(config), "Concurrent export settings overwrite was accepted.");
    }
    // Reload a fresh controller after the deliberate conflict; its queue settings are independent.
    Gate snapshotGate;
    snapshotGate.failSecond = true;
    ExportController queue(catalog, editor, library, snapshotGate.runner());
    require(queue.configure(config), "Cannot configure snapshot queue.");
    editor.openFile(
        QUrl::fromLocalFile(QString::fromStdString((directory / "photo-0.DNG").string())));
    wait([&] { return editor.hasPhoto() && !editor.busy(); });
    editor.setExposure(.75);
    queue.startIDs({ids[2], ids[0], ids[1], ids[0]});
    wait([&] { return snapshotGate.entered.load() == 1; });
    require(queue.total() == 3 && !editor.editingLocked() && !library.working(),
            "Planning did not deduplicate or release editor.");
    queue.pause();
    {
        luma::Catalog db(catalog);
        auto asset = db.assetByID(ids[2]);
        asset->edits.exposure = -3;
        db.saveEdits(asset->id, asset->edits);
    }
    auto later = config;
    later["quality"] = 17;
    require(queue.configure(later), "Cannot change settings while queue snapshot is running.");
    snapshotGate.permitted = 1;
    wait([&] { return queue.paused(); });
    require(queue.completed() == 1 && queue.succeeded() == 1 && snapshotGate.entered == 1,
            "Pause did not wait for completed current file.");
    queue.resume();
    wait([&] { return snapshotGate.entered == 2; });
    snapshotGate.permitted = 3;
    wait([&] { return !queue.working(); });
    require(queue.completed() == 3 && queue.succeeded() == 2 && queue.failures().size() == 1,
            "Queue did not continue after per-file failure.");
    {
        const std::lock_guard lock(snapshotGate.mutex);
        require(snapshotGate.assets.size() == 3 && snapshotGate.assets[0].edits.exposure == .75 &&
                    snapshotGate.assets[2].edits.exposure == 2 &&
                    snapshotGate.options[2].quality == 92,
                "Queued edit/options snapshot changed after planning.");
    }
    Gate cancelGate;
    ExportController cancelled(catalog, editor, library, cancelGate.runner());
    config["naming"] = "cancel-{filename}";
    require(cancelled.configure(config), "Cannot configure cancellation queue.");
    cancelled.startIDs(ids);
    wait([&] { return cancelGate.entered == 1; });
    cancelGate.permitted = 1;
    wait([&] { return cancelGate.entered == 2; });
    bool closed = false;
    QObject::connect(&cancelled, &ExportController::closeReady, [&] { closed = true; });
    require(!cancelled.requestClose(), "Close bypassed a running export.");
    wait([&] { return closed; });
    require(cancelled.succeeded() == 1 && cancelled.completed() == 1 && !cancelled.working(),
            "Close cancellation lost completed output or counted unfinished output.");
    require(std::filesystem::exists(directory / "output/cancel-photo-0.jpg") &&
                !std::filesystem::exists(directory / "output/cancel-photo-1.jpg"),
            "Cancelled queue wrote pending files.");
    Gate updateGate;
    ExportController updating(catalog, editor, library, updateGate.runner());
    config["naming"] = "update-{sequence}";
    require(updating.configure(config), "Cannot configure update queue.");
    updating.startIDs(ids);
    wait([&] { return updateGate.entered == 1; });
    bool installed = false;
    UpdateController updates([&] { return !updating.working(); }, false);
    QObject::connect(&updates, &UpdateController::preparingRestart, &updating,
                     &ExportController::cancel);
    updates.postponeInstallation([&] {
        require(!updating.working(), "Update restarted during export.");
        installed = true;
    });
    wait([&] { return installed; });
    require(updating.succeeded() == 0, "Update cancellation wrote an incomplete export.");
    // Real backend, skip/conflicts and a second start from an idle completed queue.
    ExportController native(catalog, editor, library);
    config["format"] = 1;
    config["naming"] = "native-{sequence}";
    config["resize"] = 3;
    config["resizeValue"] = 96;
    require(native.configure(config), "Cannot configure native queue.");
    native.startIDs(ids);
    wait([&] { return !native.working(); });
    require(native.succeeded() == 3 && native.failures().isEmpty(), "Native export queue failed.");
    config["conflict"] = 0;
    require(native.configure(config), "Cannot configure Skip.");
    native.startIDs(ids);
    wait([&] { return !native.working(); });
    require(native.skipped() == 3 && native.succeeded() == 0,
            "Skip conflict policy changed existing outputs.");
    config["conflict"] = 1;
    require(native.configure(config), "Cannot configure Unique.");
    native.startIDs(ids);
    wait([&] { return !native.working(); });
    require(native.succeeded() == 3 &&
                std::filesystem::exists(directory / "output/native-0001-2.png"),
            "Unique conflict policy overwrote existing output.");
    native.startIDs(ids);
    native.pause();
    wait([&] { return native.paused(); });
    native.cancel();
    require(!native.working(), "Paused queue cancellation did not complete.");
    const auto sql = [&](const char *statement) {
        sqlite3 *raw = nullptr;
        require(sqlite3_open(catalog.c_str(), &raw) == SQLITE_OK, "Cannot open autosave fixture.");
        const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
        require(sqlite3_exec(db.get(), statement, nullptr, nullptr, nullptr) == SQLITE_OK,
                "Cannot prepare autosave failure.");
    };
    sql("CREATE TRIGGER fail_export_save BEFORE UPDATE ON edit_documents BEGIN SELECT "
        "RAISE(ABORT,'injected autosave failure'); END");
    editor.setExposure(.9);
    native.startIDs(ids);
    wait([&] { return !editor.error().isEmpty(); });
    require(native.waitingForSave() && editor.saving(), "Export bypassed failed autosave.");
    const auto draft = editor.currentSettings();
    native.cancel();
    require(!native.working() && !editor.editingLocked() && editor.currentSettings() == draft,
            "Cancelling export preparation lost unsaved draft.");
    sql("DROP TRIGGER fail_export_save");
    editor.retrySave();
    wait([&] { return !editor.saving(); });
}
