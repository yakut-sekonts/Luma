#include "tests/ux/UxContext.h"
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlContext>
#include <QSGRendererInterface>
#include <QScreen>
#include <algorithm>
#include <sqlite3.h>
std::unique_ptr<NativeUpdater> makeNativeUpdater(UpdateCallbacks) {
    return {};
}
namespace ux {
Context::Context(QString rootPath, QString fixturePath, const QString &qml, QString state)
    : root(std::move(rootPath)), fixture(std::move(fixturePath)),
      stateRoot(state.isEmpty() ? root : std::move(state)),
      catalogFile(stateRoot + "/catalog.sqlite"), storageFault(catalogFile.toStdString()),
      provider(new PreviewProvider), editor(catalogFile.toStdString(), provider),
      presets(stateRoot + "/presets.json", editor), library(catalogFile.toStdString()),
      batch(catalogFile.toStdString(), editor, library, presets),
      exporter(catalogFile.toStdString(), editor, library),
      updates(
          [&] {
              return !editor.transformAutoBusy() && !editor.busy() && !editor.saving() &&
                     !editor.exporting() && !library.working() && !batch.working() &&
                     !exporter.working();
          },
          false),
      metrics(root + "/heartbeat.json") {
    localization = std::make_unique<LocalizationController>(stateRoot + "/language.json", engine);
    engine.rootContext()->setContextProperty("editor", &editor);
    engine.rootContext()->setContextProperty("library", &library);
    engine.rootContext()->setContextProperty("presets", &presets);
    engine.rootContext()->setContextProperty("batch", &batch);
    engine.rootContext()->setContextProperty("exporter", &exporter);
    engine.rootContext()->setContextProperty("updates", &updates);
    engine.rootContext()->setContextProperty("localization", localization.get());
    engine.addImageProvider("preview", provider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider(catalogFile.toStdString()));
    QObject::connect(&updates, &UpdateController::preparingRestart, &editor,
                     &EditorController::endEdit);
    QObject::connect(&updates, &UpdateController::preparingRestart, &batch,
                     &BatchController::cancel);
    QObject::connect(&updates, &UpdateController::preparingRestart, &exporter,
                     &ExportController::cancel);
    QObject::connect(&library, &LibraryModel::openRequested, &editor, &EditorController::openFile);
    QObject::connect(&editor, &EditorController::photoLoaded, &library, &LibraryModel::photoOpened);
    QObject::connect(&editor, &EditorController::editsSaved, &library,
                     &LibraryModel::refreshThumbnails);
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    engine.load(QUrl::fromLocalFile(qml + "/Main.qml"));
    require(!engine.rootObjects().isEmpty(), "Main.qml failed: " + warnings.join('\n'));
    window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    require(window, "Main is not a QQuickWindow");
    metrics.attach(*window);
    editor.setWindow(window);
    driver = std::make_unique<Driver>(*window, metrics, root + "/actions.jsonl");
    window->requestActivate();
    driver->wait([&] { return window->isActive(); }, "native window active", 5000);
    driver->wait([&] { return !library.working() && !library.loading() && window->isVisible(); },
                 "empty app ready");
    driver->frame();
    driver->screenshot(root + "/before.png");
}
Context::~Context() {
    if (window)
        window->hide();
}
void Context::loaded() {
    driver->wait(
        [&] { return editor.hasPhoto() && !editor.busy() && !editor.previewUrl().isEmpty(); },
        "photo and preview ready");
    driver->frame();
    require(editor.error().isEmpty(), editor.error());
}
void Context::saved() {
    editor.endEdit();
    driver->wait([&] { return !editor.saving() && !editor.busy(); }, "autosave committed");
    require(editor.error().isEmpty(), editor.error());
}
void Context::import(int count, bool copy) {
    if (!window->property("libraryMode").toBool())
        driver->click("main.libraryButton");
    QList<QUrl> sources;
    const auto names = QDir(fixture).entryList({"*.png"}, QDir::Files, QDir::Name);
    require(names.size() >= count, "Insufficient public UX fixtures");
    for (int i = 0; i < count; ++i)
        sources.append(QUrl::fromLocalFile(fixture + '/' + names[i]));
    auto *pane = driver->object("library.pane");
    pane->setProperty("sources", QVariant::fromValue(sources));
    if (copy) {
        QDir().mkpath(root + "/copies");
        pane->setProperty("destination", QUrl::fromLocalFile(root + "/copies"));
    }
    driver->click("library.importButton");
    driver->wait(
        [&] { return driver->object("library.importDialog")->property("opened").toBool(); },
        "import dialog opened");
    driver->object("library.importMode")->setProperty("currentIndex", copy ? 1 : 0);
    driver->object("library.importDuplicates")->setProperty("checked", false);
    driver->click("library.importStart");
    driver->wait(
        [&] { return !library.working() && !library.loading() && library.count() == count; },
        "import checkpoint and model", 60000);
    require(library.error().isEmpty(), library.error());
    driver->frame();
}
QString Context::card(int row, bool filmstrip) {
    driver->wait(
        [&] {
            return !library.data(library.index(row), LibraryModel::AssetID).toString().isEmpty();
        },
        "library page ready");
    const auto id = (filmstrip ? "filmstrip.card." : "library.card.") +
                    library.data(library.index(row), LibraryModel::AssetID).toString();
    driver->wait(
        [&] {
            window->requestUpdate();
            return driver->find(id) != nullptr;
        },
        "visible delegate " + id);
    return id;
}
void Context::openFirst() {
    import(10);
    driver->doubleClick(card(0));
    loaded();
    require(!window->property("libraryMode").toBool(), "Double-click failed to open Develop");
}
void Context::addMask(int kind) {
    reveal("localPanelToggle");
    if (!driver->object("localPanelToggle")->property("checked").toBool())
        driver->click("localPanelToggle");
    reveal("localNewKind");
    driver->choose("localNewKind", kind);
    const auto count = editor.currentSettings().localMasks.size();
    reveal("localNewMask");
    driver->click("localNewMask");
    driver->wait(
        [&] {
            return editor.currentSettings().localMasks.size() == count + 1 &&
                   editor.localCanvasReady();
        },
        "new mask and overlay ready");
    driver->frame();
}
void Context::addComponent(int kind, int operation) {
    reveal("localCombineToggle");
    if (!driver->object("localCombineToggle")->property("checked").toBool())
        driver->click("localCombineToggle");
    reveal("localComponentKind");
    driver->choose("localComponentKind", kind);
    reveal("localNewOperation");
    driver->choose("localNewOperation", operation);
    const auto count = editor.currentSettings().localMasks.back().components.size();
    reveal("localAddComponent");
    driver->click("localAddComponent");
    driver->wait(
        [&] {
            return editor.currentSettings().localMasks.back().components.size() == count + 1 &&
                   editor.localCanvasReady();
        },
        "new boolean component ready");
}
QString Context::exportPNG(const QString &name) {
    QDir().mkpath(root + "/exports");
    driver->click("exportButton");
    driver->wait([&] { return driver->object("exportPanel")->property("opened").toBool(); },
                 "Export panel opened");
    driver->type("export.destination", root + "/exports");
    driver->type("exportNaming", name);
    driver->choose("exportFormat", 1);
    driver->click("export.current");
    driver->wait([&] { return !exporter.working() && exporter.completed() == 1; },
                 "PNG export completed");
    require(exporter.error().isEmpty() && exporter.succeeded() == 1 &&
                exporter.failures().isEmpty(),
            "PNG export failed: " + exporter.error() + exporter.failures().join(';'));
    const auto path = root + "/exports/" + name + ".png";
    require(QFileInfo::exists(path), "Expected PNG missing: " + path);
    return path;
}
void Context::reveal(const QString &id) {
    auto *target = driver->item(id);
    auto *scroll = driver->item("develop.adjustmentsScroll");
    auto *flickable = qvariant_cast<QObject *>(scroll->property("contentItem"));
    require(flickable, "Adjustment ScrollView has no Flickable");
    for (int i = 0; i < 8; ++i) {
        driver->wait([&] { return !flickable->property("moving").toBool(); },
                     "adjustment scroll settled", 5000);
        driver->frame();
        const auto p =
            target->mapToItem(scroll, QPointF(target->width() / 2, target->height() / 2));
        if (p.y() >= target->height() / 2 && p.y() < scroll->height() - target->height() / 2)
            return;
        const double content = flickable->property("contentHeight").toDouble();
        const double current = flickable->property("contentY").toDouble();
        const double desired = std::clamp(current + p.y() - scroll->height() / 2, 0.0,
                                          std::max(0.0, content - scroll->height()));
        auto *bar = driver->object("develop.adjustmentsBar");
        const double size = bar->property("size").toDouble();
        const double position = bar->property("position").toDouble();
        driver->drag("develop.adjustmentsBar", {.5, position + size / 2},
                     {.5, desired / content + size / 2}, 8);
    }
    driver->log({{"action", "scroll_failure"},
                 {"target", id},
                 {"target_y", target->mapToItem(scroll, QPointF()).y()},
                 {"viewport_height", scroll->height()},
                 {"content_y", flickable->property("contentY").toDouble()},
                 {"content_height", flickable->property("contentHeight").toDouble()}});
    throw std::runtime_error("Cannot scroll to control: " + id.toStdString());
}
void Context::checkData() {
    luma::Catalog catalog(catalogFile.toStdString());
    catalog.verify();
    sqlite3 *database = nullptr;
    require(sqlite3_open_v2(catalogFile.toUtf8().constData(), &database, SQLITE_OPEN_READONLY,
                            nullptr) == SQLITE_OK,
            "Cannot verify UX catalog");
    sqlite3_stmt *statement = nullptr;
    const bool prepared = sqlite3_prepare_v2(database, "PRAGMA integrity_check", -1, &statement,
                                             nullptr) == SQLITE_OK;
    const bool valid =
        prepared && sqlite3_step(statement) == SQLITE_ROW &&
        QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(statement, 0))) ==
            "ok";
    sqlite3_finalize(statement);
    sqlite3_close(database);
    require(valid, "UX catalog integrity failed");
}
void Context::finish(const QString &id, const QString &status, const QString &message) {
    auto values = metrics.json();
    values["id"] = id;
    values["status"] = status;
    values["message"] = message;
    values["warnings"] = QJsonArray::fromStringList(warnings);
    values["qt_runtime"] = QString::fromLatin1(qVersion());
    values["application_version"] = QCoreApplication::applicationVersion();
    values["platform"] = QGuiApplication::platformName();
    values["quick_backend"] = qEnvironmentVariable("QT_QUICK_BACKEND", "default");
    if (window) {
        const auto api = window->rendererInterface()->graphicsApi();
        values["graphics_api"] = api == QSGRendererInterface::Metal ? QString("metal")
                                 : api == QSGRendererInterface::Software
                                     ? QString("software")
                                     : QString("api-%1").arg(static_cast<int>(api));
        values["device_pixel_ratio"] = window->devicePixelRatio();
        values["window_size"] =
            QJsonObject{{"width", window->width()}, {"height", window->height()}};
        if (const auto *screen = window->screen())
            values["display_geometry"] = QJsonObject{{"width", screen->geometry().width()},
                                                     {"height", screen->geometry().height()}};
    }
    QFile measurements(root + "/measurements.json");
    if (measurements.exists()) {
        QJsonParseError parse;
        const auto document = measurements.open(QIODevice::ReadOnly)
                                  ? QJsonDocument::fromJson(measurements.readAll(), &parse)
                                  : QJsonDocument{};
        if (document.isObject() && parse.error == QJsonParseError::NoError)
            values["measurements"] = document.object();
        else {
            values["status"] = "FAIL";
            values["message"] = message + "; measurement evidence is unreadable or invalid";
        }
    }
    auto recordedMetrics = metrics.json();
    if (values.contains("measurements"))
        recordedMetrics["measurements"] = values["measurements"];
    writeJson(root + "/metrics.json", recordedMetrics);
    values["window_state"] = window && window->isVisible() ? "visible" : "closed";
    if (window && window->isVisible()) {
        try {
            driver->screenshot(root + (status == "FAIL" ? "/failure.png" : "/after.png"));
        } catch (const std::exception &error) {
            values["status"] = "FAIL";
            values["screenshot_error"] = QString::fromUtf8(error.what());
            values["message"] =
                message + "; evidence capture failed: " + QString::fromUtf8(error.what());
        }
    }
    writeJson(root + "/result.json", values);
}
} // namespace ux
