#include "apps/desktop/BatchController.h"
#include "apps/desktop/CatalogStartup.h"
#include "apps/desktop/DiagnosticSession.h"
#include "apps/desktop/DiagnosticsController.h"
#include "apps/desktop/EditorController.h"
#include "apps/desktop/ExportController.h"
#include "apps/desktop/LibraryAcceptance.h"
#include "apps/desktop/LibraryModel.h"
#include "apps/desktop/LocalMaskAcceptance.h"
#include "apps/desktop/LocalizationController.h"
#include "apps/desktop/PresetController.h"
#include "apps/desktop/ThumbnailProvider.h"
#include "apps/desktop/UpdateController.h"
#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <iostream>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Luma"));
    app.setApplicationName(QStringLiteral("Luma"));
    app.setApplicationVersion(QStringLiteral(LUMA_VERSION));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Luma — non-destructive photo editor for macOS"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(
        {QStringLiteral("catalog"), QStringLiteral("Catalog path"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("smoke-test"), QStringLiteral("Verify QML shell then exit")});
    parser.addOption({QStringLiteral("test-language"),
                      QStringLiteral("Integration test: runtime language and settings"),
                      QStringLiteral("code")});
    parser.addOption({QStringLiteral("test-window-width"),
                      QStringLiteral("Integration test: responsive window width"),
                      QStringLiteral("pixels")});
    parser.addOption({QStringLiteral("test-edit"), QStringLiteral("Integration test: open photo"),
                      QStringLiteral("raw")});
    parser.addOption({QStringLiteral("test-exposure"), QStringLiteral("Integration test: exposure"),
                      QStringLiteral("ev")});
    parser.addOption({QStringLiteral("test-restore"),
                      QStringLiteral("Integration test: verify restored exposure"),
                      QStringLiteral("ev")});
    parser.addOption({QStringLiteral("test-screenshot"),
                      QStringLiteral("Integration test: save the rendered app window"),
                      QStringLiteral("png")});
    parser.addOption(
        {QStringLiteral("test-develop"), QStringLiteral("Integration test: Develop and history")});
    parser.addOption({QStringLiteral("test-advanced"),
                      QStringLiteral("Integration test: Curves, HSL and targeted gestures")});
    parser.addOption({QStringLiteral("test-lens"),
                      QStringLiteral("Integration test: calibrated Lens Corrections")});
    parser.addOption({QStringLiteral("test-transform"),
                      QStringLiteral("Integration test: perspective geometry and history")});
    parser.addOption({QStringLiteral("test-complete-masks"),
                      QStringLiteral("Integration test: complete phase-0.5 mask workflow")});
    parser.addOption({QStringLiteral("test-local-masks"),
                      QStringLiteral("Integration test: local masks, history and restart")});
    parser.addOption({QStringLiteral("test-filter"),
                      QStringLiteral("Integration test: built-in filter and Undo"),
                      QStringLiteral("id")});
    parser.addOption({QStringLiteral("test-xmp-preset"),
                      QStringLiteral("Integration test: sparse XMP import, apply and Undo"),
                      QStringLiteral("file")});
    parser.addOption({QStringLiteral("test-export"), QStringLiteral("Integration test: export PNG"),
                      QStringLiteral("png")});
    parser.addOption({QStringLiteral("test-export-queue"),
                      QStringLiteral("Integration test: snapshot queue and DNG output"),
                      QStringLiteral("folder")});
    parser.addOption({QStringLiteral("test-cancel-export"),
                      QStringLiteral("Integration test: cancel queued export")});
    parser.addOption({QStringLiteral("test-close-during-export"),
                      QStringLiteral("Integration test: close with queued load/export")});
    parser.addOption({QStringLiteral("test-library"),
                      QStringLiteral("Integration test: Library import and organization"),
                      QStringLiteral("folder")});
    parser.addOption({QStringLiteral("test-close-import"),
                      QStringLiteral("Integration test: close during import")});
    parser.process(app);
    const auto options = parser.optionNames();
    const bool unattended = std::any_of(options.begin(), options.end(), [](const QString &option) {
        return option.startsWith(QStringLiteral("test-")) || option == QStringLiteral("smoke-test");
    });
    luma::desktop::DiagnosticSession diagnosticSession(!unattended);
    const auto remembered = unattended ? QString() : CatalogStartup::rememberedCatalog();
    const bool explicitCatalog = parser.isSet(QStringLiteral("catalog"));
    const auto initialCatalog =
        explicitCatalog
            ? parser.value(QStringLiteral("catalog"))
            : (!remembered.isEmpty()
                   ? remembered
                   : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                         QStringLiteral("/catalog.sqlite"));
    CatalogStartup startup;
    QObject::connect(&startup, &CatalogStartup::changed, &startup,
                     [&, recoveryObserved = false]() mutable {
                         if (startup.recoveryPending() && !recoveryObserved)
                             diagnosticSession.record(luma::desktop::LogLevel::warning,
                                                      luma::desktop::LogEvent::recoveryRequested);
                         recoveryObserved = startup.recoveryPending();
                     });
    if (!startup.prepare(initialCatalog, !explicitCatalog && remembered.isEmpty(), !unattended)) {
        if (!unattended && startup.quitting())
            diagnosticSession.markCleanExit();
        return unattended ? 1 : 0;
    }
    diagnosticSession.record(luma::desktop::LogLevel::info, luma::desktop::LogEvent::catalogReady);
    const auto catalog = startup.catalogPath();
    auto *provider = new PreviewProvider;
    EditorController editor(std::filesystem::path(catalog.toStdString()), provider);
    DiagnosticsController diagnostics(std::filesystem::path(catalog.toStdString()), editor,
                                      diagnosticSession);
    PresetController presets(QFileInfo(catalog).absolutePath() + QStringLiteral("/presets.json"),
                             editor);
    LibraryModel library(std::filesystem::path(catalog.toStdString()));
    BatchController batch(std::filesystem::path(catalog.toStdString()), editor, library, presets);
    ExportController exporter(std::filesystem::path(catalog.toStdString()), editor, library);
    QObject::connect(&library, &LibraryModel::stateChanged, &library, [&] {
        diagnosticSession.observe(luma::desktop::LoggedWorkflow::library, library.working(),
                                  !library.error().isEmpty());
    });
    QObject::connect(&editor, &EditorController::stateChanged, &editor, [&] {
        diagnosticSession.observe(luma::desktop::LoggedWorkflow::editor,
                                  editor.busy() || editor.saving() || editor.aiBusy() ||
                                      editor.retouchBusy(),
                                  !editor.error().isEmpty());
    });
    QObject::connect(&exporter, &ExportController::changed, &exporter, [&] {
        diagnosticSession.observe(luma::desktop::LoggedWorkflow::exportQueue, exporter.working(),
                                  !exporter.error().isEmpty());
    });
    const bool testing = parser.isSet(QStringLiteral("smoke-test")) ||
                         parser.isSet(QStringLiteral("test-edit")) ||
                         parser.isSet(QStringLiteral("test-restore")) ||
                         parser.isSet(QStringLiteral("test-library"));
    UpdateController updates(
        [&] {
            return editor.readyForUpdate() && !library.working() && !batch.working() &&
                   !exporter.working();
        },
        !testing);
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
    std::unique_ptr<LocalizationController> localizationOwner;
    QQmlApplicationEngine engine;
    localizationOwner = std::make_unique<LocalizationController>(
        QFileInfo(catalog).absolutePath() + QStringLiteral("/language.json"), engine);
    auto &localization = *localizationOwner;
    engine.rootContext()->setContextProperty(QStringLiteral("localization"), &localization);
    if (parser.isSet(QStringLiteral("test-language")) &&
        (!parser.isSet(QStringLiteral("smoke-test")) ||
         !localization.selectLanguage(parser.value(QStringLiteral("test-language"))))) {
        std::cerr << "Invalid language acceptance arguments: " << localization.error().toStdString()
                  << '\n';
        return 2;
    }
    engine.addImageProvider(QStringLiteral("thumbnail"),
                            new ThumbnailProvider(std::filesystem::path(catalog.toStdString())));
    engine.addImageProvider(QStringLiteral("preview"), provider);
    engine.rootContext()->setContextProperty(QStringLiteral("editor"), &editor);
    engine.rootContext()->setContextProperty(QStringLiteral("diagnostics"), &diagnostics);
    engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("updates"), &updates);
    engine.rootContext()->setContextProperty(QStringLiteral("presets"), &presets);
    engine.rootContext()->setContextProperty(QStringLiteral("batch"), &batch);
    engine.rootContext()->setContextProperty(QStringLiteral("exporter"), &exporter);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("Luma.Desktop"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window) {
        return 1;
    }
    if (parser.isSet(QStringLiteral("test-window-width"))) {
        bool valid = false;
        const auto width = parser.value(QStringLiteral("test-window-width")).toInt(&valid);
        if (!valid || width < 1000 || width > 2560) {
            std::cerr << "Invalid integration window width\n";
            return 2;
        }
        window->setWidth(width);
    }
    editor.setWindow(window);
    if (parser.isSet(QStringLiteral("smoke-test"))) {
        if (parser.isSet(QStringLiteral("test-language"))) {
            const auto code = localization.language();
            const auto screenshot = parser.value(QStringLiteral("test-screenshot"));
            QTimer::singleShot(100, &app, [&, code, screenshot, window] {
                auto *button = window->findChild<QObject *>(QStringLiteral("exportButton"));
                auto *panel = window->findChild<QObject *>(QStringLiteral("preferencesPanel"));
                if (!button || !panel) {
                    std::cerr << "Language UI controls missing\n";
                    app.exit(1);
                    return;
                }
                for (const auto &language : {QStringLiteral("en"), code}) {
                    if (!localization.selectLanguage(language) ||
                        button->property("text").toString() !=
                            QCoreApplication::translate("Luma", "Export")) {
                        std::cerr << "Main UI did not retranslate\n";
                        app.exit(1);
                        return;
                    }
                }
                QMetaObject::invokeMethod(panel, "open");
                // Retranslation schedules a layout polish; inspect settled geometry.
                QTimer::singleShot(150, &app, [&, screenshot, window] {
                    for (const auto *name :
                         {"batchSync", "batchUndo", "batchRedo", "libraryFlagFilter",
                          "libraryLabelFilter", "libraryAddToCollection",
                          "libraryRemoveFromCollection", "settingsButton", "exportButton",
                          "openPhotoButton"}) {
                        auto *item = window->findChild<QQuickItem *>(QString::fromLatin1(name));
                        if (!item)
                            continue;
                        const auto position = item->mapToScene(QPointF(0, 0));
                        if (item->isVisible() && (position.x() < 0 || position.x() + item->width() >
                                                                          window->width() + 1)) {
                            std::cerr << "Localized control outside window: " << name
                                      << " x=" << position.x() << " width=" << item->width()
                                      << " window=" << window->width() << '\n';
                            for (auto *parent = item->parentItem(); parent;
                                 parent = parent->parentItem())
                                std::cerr << parent->metaObject()->className()
                                          << " x=" << parent->x() << " width=" << parent->width()
                                          << '\n';
                            if (!screenshot.isEmpty())
                                window->grabWindow().save(screenshot);
                            app.exit(1);
                            return;
                        }
                    }
                    if (!screenshot.isEmpty() && !window->grabWindow().save(screenshot)) {
                        app.exit(1);
                        return;
                    }
                    std::cout << "Native language switch verified\n";
                    app.quit();
                });
            });
        } else
            QTimer::singleShot(300, &app, &QCoreApplication::quit);
    } else if (parser.isSet(QStringLiteral("test-library"))) {
        startLibraryAcceptance(app, window, editor, library,
                               parser.value(QStringLiteral("test-library")),
                               parser.value(QStringLiteral("test-screenshot")),
                               parser.isSet(QStringLiteral("test-close-import")));
    } else if (parser.isSet(QStringLiteral("test-export-queue"))) {
        const auto destination = parser.value(QStringLiteral("test-export-queue"));
        const auto screenshot = parser.value(QStringLiteral("test-screenshot"));
        auto started = std::make_shared<bool>(false);
        auto edited = std::make_shared<bool>(false);
        QObject::connect(&editor, &EditorController::stateChanged, &app,
                         [&, started, edited, destination] {
                             if (!editor.error().isEmpty()) {
                                 std::cerr << editor.error().toStdString() << '\n';
                                 app.exit(1);
                                 return;
                             }
                             if (editor.busy() || !editor.hasPhoto() || *started)
                                 return;
                             if (!*edited) {
                                 *edited = true;
                                 editor.setExposure(.42);
                                 return;
                             }
                             if (editor.saving())
                                 return;
                             auto settings = exporter.settings();
                             settings["destination"] = destination;
                             settings["format"] = 4;
                             settings["colorSpace"] = 1;
                             settings["resize"] = 3;
                             settings["resizeValue"] = 512;
                             settings["metadata"] = 3;
                             settings["watermarkKind"] = 1;
                             settings["watermarkText"] = QStringLiteral("Luma QA · ©");
                             if (!exporter.configure(settings)) {
                                 std::cerr << exporter.error().toStdString() << '\n';
                                 app.exit(1);
                                 return;
                             }
                             *started = true;
                             exporter.start(false);
                         });
        auto completed = std::make_shared<bool>(false);
        QObject::connect(
            &exporter, &ExportController::changed, &app,
            [&, started, completed, screenshot, window] {
                if (!*started || exporter.working() || *completed)
                    return;
                *completed = true;
                if (!exporter.error().isEmpty() || !exporter.failures().isEmpty() ||
                    exporter.succeeded() != 1) {
                    std::cerr << exporter.error().toStdString()
                              << exporter.failures().join('\n').toStdString() << " queue failed\n";
                    app.exit(1);
                    return;
                }
                if (!screenshot.isEmpty()) {
                    auto *panel = window->findChild<QObject *>(QStringLiteral("exportPanel"));
                    if (!panel || !QMetaObject::invokeMethod(panel, "open")) {
                        app.exit(1);
                        return;
                    }
                }
                QTimer::singleShot(250, &app, [&, screenshot, window] {
                    if (!screenshot.isEmpty() && !window->grabWindow().save(screenshot)) {
                        app.exit(1);
                        return;
                    }
                    std::cout << "Native immutable export queue and processed DNG verified\n";
                    window->close();
                });
            });
        QTimer::singleShot(90000, &app, [&app] {
            std::cerr << "Queue GUI timed out\n";
            app.exit(1);
        });
        editor.openFile(QUrl::fromLocalFile(parser.value(QStringLiteral("test-edit"))));
    } else if (parser.isSet(QStringLiteral("test-complete-masks"))) {
        const bool restore = parser.isSet(QStringLiteral("test-restore"));
        bool valid = false;
        const double target =
            parser.value(restore ? QStringLiteral("test-restore") : QStringLiteral("test-exposure"))
                .toDouble(&valid);
        if (!valid || parser.value(QStringLiteral("test-export")).isEmpty()) {
            std::cerr << "Invalid complete mask acceptance arguments\n";
            return 2;
        }
        startLocalMaskAcceptance(app, window, editor, parser.value(QStringLiteral("test-edit")),
                                 restore, target, parser.value(QStringLiteral("test-export")),
                                 parser.value(QStringLiteral("test-screenshot")));
    } else if (parser.isSet(QStringLiteral("test-edit")) ||
               parser.isSet(QStringLiteral("test-restore"))) {
        const bool editing = parser.isSet(QStringLiteral("test-edit"));
        bool parsed = false;
        const double target =
            parser.value(editing ? QStringLiteral("test-exposure") : QStringLiteral("test-restore"))
                .toDouble(&parsed);
        if (!parsed) {
            std::cerr << "Invalid test exposure\n";
            return 2;
        }
        auto applied = std::make_shared<bool>(false);
        auto finished = std::make_shared<bool>(false);
        const bool closeDuringExport = parser.isSet(QStringLiteral("test-close-during-export"));
        const auto testSource = parser.value(QStringLiteral("test-edit"));
        const bool cancelExport = parser.isSet(QStringLiteral("test-cancel-export"));
        const bool develop = parser.isSet(QStringLiteral("test-develop"));
        const bool advanced = parser.isSet(QStringLiteral("test-advanced"));
        const bool lens = parser.isSet(QStringLiteral("test-lens"));
        const bool transform = parser.isSet(QStringLiteral("test-transform"));
        const bool localMasks = parser.isSet(QStringLiteral("test-local-masks"));
        const auto filter = parser.value(QStringLiteral("test-filter"));
        const auto xmp = parser.value(QStringLiteral("test-xmp-preset"));
        const auto exportPath = parser.value(QStringLiteral("test-export"));
        auto exportStarted = std::make_shared<bool>(false);
        const auto screenshot = parser.value(QStringLiteral("test-screenshot"));
        // This acceptance state machine waits for previewUrl as well as edits.
        // previewChanged includes stateChanged and the independent render completion.
        QObject::connect(
            &editor, &EditorController::previewChanged, &app,
            [&editor, &app, window, editing, target, applied, finished, screenshot, develop,
             exportPath, exportStarted, cancelExport, closeDuringExport, testSource, advanced, lens,
             transform, localMasks, &presets, filter, xmp] {
                if (!editor.error().isEmpty()) {
                    std::cerr << editor.error().toStdString() << '\n';
                    app.exit(1);
                    return;
                }
                if (!editor.busy() && editor.hasPhoto() && !*applied) {
                    *applied = true;
                    if (editing) {
                        editor.setExposure(target);
                        if (!xmp.isEmpty()) {
                            editor.setParameter(QStringLiteral("contrast"), 37);
                            const auto before = editor.currentSettings();
                            if (!presets.importPreset(QUrl::fromLocalFile(xmp)) ||
                                presets.importedId().isEmpty() ||
                                presets.importUnsupported() !=
                                    QStringList{QStringLiteral("Texture")} ||
                                editor.currentSettings() != before) {
                                std::cerr << "XMP import or compatibility report failed\n";
                                app.exit(1);
                                return;
                            }
                            presets.apply(presets.importedId());
                            auto expected = before;
                            expected.exposure = 1.3;
                            if (editor.currentSettings() != expected) {
                                std::cerr << "XMP apply changed absent parameters\n";
                                app.exit(1);
                                return;
                            }
                            editor.undo();
                            if (editor.currentSettings() != before) {
                                std::cerr << "XMP Undo failed\n";
                                app.exit(1);
                                return;
                            }
                            editor.redo();
                            if (editor.currentSettings() != expected) {
                                std::cerr << "XMP Redo failed\n";
                                app.exit(1);
                                return;
                            }
                        }
                        if (localMasks) {
                            const auto before = editor.currentSettings();
                            if (editor.addLocalMask(QStringLiteral("linear")).isEmpty()) {
                                std::cerr << "Cannot add local gradient\n";
                                app.exit(1);
                                return;
                            }
                            const auto added = editor.currentSettings();
                            editor.beginEdit();
                            editor.setLocalParameter(QStringLiteral("exposure"), 0.4);
                            editor.setLocalParameter(QStringLiteral("exposure"), 0.7);
                            editor.endEdit();
                            const auto adjusted = editor.currentSettings();
                            editor.undo();
                            if (editor.currentSettings() != added || adjusted == added) {
                                std::cerr << "Local gesture Undo failed\n";
                                app.exit(1);
                                return;
                            }
                            editor.redo();
                            if (editor.currentSettings() != adjusted ||
                                before.localMasks.size() + 1 != adjusted.localMasks.size()) {
                                std::cerr << "Local gesture Redo failed\n";
                                app.exit(1);
                                return;
                            }
                            editor.addLocalMask(QStringLiteral("radial"));
                            editor.setLocalParameter(QStringLiteral("exposure"), -0.4);
                            editor.setLocalParameter(QStringLiteral("angle"), 25);
                            editor.setLocalMode(false);
                        }
                        if (lens) {
                            if (!editor.lensState().value("distortionAvailable").toBool() ||
                                !editor.lensState().value("vignettingAvailable").toBool() ||
                                !editor.lensState().value("caAvailable").toBool()) {
                                std::cerr << "Known lens calibration unavailable\n";
                                app.exit(1);
                                return;
                            }
                            editor.setCorrectionEnabled(QStringLiteral("lens"), true);
                        }
                        if (transform) {
                            const auto before = editor.parameters();
                            editor.beginEdit();
                            editor.setParameter(QStringLiteral("transform.rotate"), 8);
                            editor.setParameter(QStringLiteral("transform.horizontal"), 24);
                            editor.setParameter(QStringLiteral("transform.vertical"), -18);
                            editor.setParameter(QStringLiteral("transform.aspect"), 12);
                            editor.setParameter(QStringLiteral("transform.xOffset"), 5);
                            editor.endEdit();
                            const auto after = editor.parameters();
                            editor.undo();
                            if (editor.parameters() != before || after == before) {
                                std::cerr << "Transform gesture Undo failed\n";
                                app.exit(1);
                                return;
                            }
                            editor.redo();
                            if (editor.parameters() != after) {
                                std::cerr << "Transform gesture Redo failed\n";
                                app.exit(1);
                                return;
                            }
                        }
                        if (!filter.isEmpty()) {
                            const auto before = editor.currentSettings();
                            presets.apply(filter);
                            const auto after = editor.currentSettings();
                            editor.undo();
                            if (editor.currentSettings() != before || after == before ||
                                !presets.error().isEmpty()) {
                                std::cerr << "Built-in filter Undo failed\n";
                                app.exit(1);
                                return;
                            }
                            editor.redo();
                            if (editor.currentSettings() != after) {
                                std::cerr << "Built-in filter Redo failed\n";
                                app.exit(1);
                                return;
                            }
                        }
                        if (develop) {
                            editor.beginEdit();
                            editor.setParameter(QStringLiteral("temperature"), 5100);
                            editor.setParameter(QStringLiteral("tint"), 12);
                            editor.endEdit();
                            editor.setParameter(QStringLiteral("contrast"), 18);
                            editor.setParameter(QStringLiteral("highlights"), -40);
                            editor.setParameter(QStringLiteral("shadows"), 25);
                            const auto previousVibrance =
                                editor.parameters().value(QStringLiteral("vibrance")).toDouble();
                            editor.setParameter(QStringLiteral("vibrance"), 35);
                            editor.undo();
                            if (editor.parameters().value(QStringLiteral("vibrance")).toDouble() !=
                                previousVibrance) {
                                std::cerr << "Undo did not restore Vibrance\n";
                                app.exit(1);
                                return;
                            }
                            editor.redo();
                            editor.setCorrectionEnabled(QStringLiteral("shadows"), false);
                        }
                        if (advanced) {
                            const auto originalCurves = editor.curvePoints();
                            editor.beginEdit();
                            const int point =
                                editor.addCurvePoint(QStringLiteral("rgb"), 0.4, 0.55);
                            editor.moveCurvePoint(QStringLiteral("rgb"), point, 0.4, 0.6);
                            editor.moveCurvePoint(QStringLiteral("rgb"), point, 0.4, 0.62);
                            editor.endEdit();
                            editor.undo();
                            if (editor.curvePoints() != originalCurves) {
                                std::cerr << "Curve gesture Undo failed\n";
                                app.exit(1);
                                return;
                            }
                            editor.redo();
                            const auto beforeHSL = editor.parameters();
                            editor.beginEdit();
                            editor.setParameter(QStringLiteral("hsl.red.hue"), 12);
                            editor.setParameter(QStringLiteral("hsl.blue.luminance"), 25);
                            editor.endEdit();
                            editor.undo();
                            if (editor.parameters() != beforeHSL) {
                                std::cerr << "HSL gesture Undo failed\n";
                                app.exit(1);
                                return;
                            }
                            editor.redo();
                            editor.setParameter(QStringLiteral("curve.lights"), 15);
                            const auto beforeTarget = editor.curvePoints();
                            editor.setTargetedTool(QStringLiteral("curve.red"));
                            if (!editor.beginTargeted(0.4, 0.5)) {
                                std::cerr << "Curve target did not begin\n";
                                app.exit(1);
                                return;
                            }
                            editor.adjustTargeted(0.05);
                            editor.adjustTargeted(0.08);
                            editor.endTargeted();
                            if (editor.curvePoints() == beforeTarget) {
                                std::cerr << "Curve target did not change pixels\n";
                                app.exit(1);
                                return;
                            }
                            editor.undo();
                            if (editor.curvePoints() != beforeTarget) {
                                std::cerr << "Targeted curve Undo failed\n";
                                app.exit(1);
                                return;
                            }
                            const auto beforeColorTarget = editor.parameters();
                            editor.setTargetedTool(QStringLiteral("hsl.saturation"));
                            if (!editor.beginTargeted(0.4, 0.5)) {
                                std::cerr << "HSL target did not begin\n";
                                app.exit(1);
                                return;
                            }
                            editor.adjustTargeted(-0.15);
                            editor.adjustTargeted(-0.2);
                            editor.endTargeted();
                            if (editor.parameters() == beforeColorTarget) {
                                std::cerr << "HSL target did not change parameters\n";
                                app.exit(1);
                                return;
                            }
                            editor.undo();
                            if (editor.parameters() != beforeColorTarget) {
                                std::cerr << "Targeted HSL Undo failed\n";
                                app.exit(1);
                                return;
                            }
                            editor.setTargetedTool(QStringLiteral("none"));
                        }
                    } else if (std::abs(editor.exposure() - target) > 1e-9) {
                        std::cerr << "Exposure restoration mismatch\n";
                        app.exit(1);
                        return;
                    }
                }
                if (*applied && localMasks && !editing && !editor.busy() && editor.hasPhoto()) {
                    const auto &masks = editor.currentSettings().localMasks;
                    if (masks.size() != 2 || masks[0].parameter("exposure") != 0.7 ||
                        masks[1].shape != luma::MaskShape::radial ||
                        masks[1].parameter("exposure") != -0.4 || masks[1].angle != 25 ||
                        editor.localMode()) {
                        std::cerr << "Local mask restoration mismatch\n";
                        app.exit(1);
                        return;
                    }
                }
                if (*applied && develop && !editing && !editor.busy() && editor.hasPhoto()) {
                    if (editor.parameters().value(QStringLiteral("temperature")).toDouble() !=
                            5100 ||
                        editor.parameters().value(QStringLiteral("vibrance")).toDouble() != 35 ||
                        editor.correctionStates().value(QStringLiteral("shadows")).toBool()) {
                        std::cerr << "Develop restoration mismatch\n";
                        app.exit(1);
                        return;
                    }
                }
                if (*applied && advanced && !editing && !editor.busy() && editor.hasPhoto()) {
                    const auto points = editor.curvePoints().value(QStringLiteral("rgb")).toList();
                    if (points.size() != 3 ||
                        points[1].toMap().value(QStringLiteral("y")).toDouble() != 0.62 ||
                        editor.parameters().value(QStringLiteral("curve.lights")).toDouble() !=
                            15 ||
                        editor.parameters().value(QStringLiteral("hsl.red.hue")).toDouble() != 12 ||
                        editor.parameters()
                                .value(QStringLiteral("hsl.blue.luminance"))
                                .toDouble() != 25) {
                        std::cerr << "Advanced restoration mismatch\n";
                        app.exit(1);
                        return;
                    }
                }
                if (*applied && !*finished && !editor.saving() && !editor.busy() &&
                    !editor.previewUrl().isEmpty() && !editor.exporting()) {
                    if (!exportPath.isEmpty() && !*exportStarted) {
                        *exportStarted = true;
                        editor.exportPhoto(QUrl::fromLocalFile(exportPath), 1, 92, false);
                        if (cancelExport)
                            editor.cancelExport();
                        if (closeDuringExport) {
                            editor.openFile(QUrl::fromLocalFile(testSource));
                            window->close();
                        }
                        return;
                    }
                    *finished = true;
                    if (localMasks && !screenshot.isEmpty()) {
                        auto *panel =
                            window->findChild<QObject *>(QStringLiteral("localPanelToggle"));
                        if (!panel || !panel->setProperty("checked", true)) {
                            std::cerr << "Cannot expand local mask UI\n";
                            app.exit(1);
                            return;
                        }
                        const auto masks = editor.localMasks();
                        if (!masks.isEmpty())
                            editor.selectLocalMask(masks[0].toMap().value("id").toString());
                        editor.setLocalMode(true);
                    }
                    if (!xmp.isEmpty() && !screenshot.isEmpty()) {
                        auto *tabs = window->findChild<QObject *>(QStringLiteral("presetTabs"));
                        auto *details =
                            window->findChild<QObject *>(QStringLiteral("presetImportDetails"));
                        if (!tabs || !tabs->setProperty("currentIndex", 1) || !details ||
                            !QMetaObject::invokeMethod(details, "clicked")) {
                            std::cerr << "Cannot show XMP compatibility UI\n";
                            app.exit(1);
                            return;
                        }
                    }
                    QTimer::singleShot(120, &app, [window, &app, screenshot, localMasks] {
                        if (localMasks && !screenshot.isEmpty()) {
                            auto *panel =
                                window->findChild<QQuickItem *>(QStringLiteral("localPanel"));
                            if (!panel || panel->mapToScene(QPointF(panel->width(), 0)).x() >
                                              window->width()) {
                                std::cerr << "Local panel escaped the window\n";
                                app.exit(1);
                                return;
                            }
                            for (const auto *name :
                                 {"localPanelToggle", "localMaskChoice", "localMaskName",
                                  "localEditOnPhoto", "local_exposure", "local_opacity",
                                  "local_feather"}) {
                                auto *item =
                                    window->findChild<QQuickItem *>(QString::fromLatin1(name));
                                if (!panel || !item || item->width() < 150 ||
                                    item->mapToItem(panel, QPointF(0, 0)).x() < -1 ||
                                    item->mapToItem(panel, QPointF(item->width(), 0)).x() >
                                        panel->width() + 1) {
                                    std::cerr << "Local control escaped the sidebar: " << name
                                              << '\n';
                                    app.exit(1);
                                    return;
                                }
                            }
                        }
                        if (!screenshot.isEmpty()) {
                            const auto frame = window->grabWindow();
                            if (frame.isNull() || !frame.save(screenshot)) {
                                std::cerr << "Cannot capture the rendered viewer\n";
                                app.exit(1);
                                return;
                            }
                        }
                        std::cout << "Photo viewer and Exposure persistence verified\n";
                        window->close();
                    });
                }
            });
        QTimer::singleShot(90'000, &app, [&app] {
            std::cerr << "GUI acceptance test timed out\n";
            app.exit(1);
        });
        if (editing) {
            editor.openFile(QUrl::fromLocalFile(parser.value(QStringLiteral("test-edit"))));
        } else {
            editor.restore();
        }
    } else if (startup.restorePhoto()) {
        editor.restore();
    }
    const int result = app.exec();
    if (result == 0) {
        startup.markCleanExit();
        diagnosticSession.markCleanExit();
    }
    return result;
}
