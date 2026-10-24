#include "apps/desktop/EditorController.h"
#include "apps/desktop/PresetController.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QThread>
#include <memory>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void wait(const std::function<bool()> &ready) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 30000) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    require(ready(), "Crop editor wait timed out.");
}
void click(QObject *object, const char *signal = "clicked") {
    require(object && QMetaObject::invokeMethod(object, signal), "Crop QML action failed.");
}
} // namespace
void cropEditorTests(const QString &source, const QString &root, const QString &qmlPath) {
    PreviewProvider provider;
    const auto catalog = root + "/crop.sqlite";
    EditorController editor(catalog.toStdString(), &provider);
    editor.openFile(QUrl::fromLocalFile(source));
    wait([&] { return editor.hasPhoto() && !editor.busy() && !editor.previewUrl().isEmpty(); });
    const auto original = editor.currentSettings();
    editor.beginEdit();
    editor.setCropRect(0.1, 0.15, 0.7, 0.6);
    editor.setCropRect(0.15, 0.2, 0.6, 0.5);
    editor.endEdit();
    const auto saved = editor.currentSettings();
    editor.undo();
    require(editor.currentSettings() == original, "Crop drag was not one Undo.");
    editor.redo();
    require(editor.currentSettings() == saved, "Crop Redo changed geometry.");
    PresetController presets(root + "/crop-presets.json", editor);
    presets.copySettings(32);
    editor.resetCrop();
    presets.pasteSettings();
    require(editor.currentSettings() == saved, "Crop Copy/Paste changed excluded settings.");
    require(presets.save({}, "Crop", {}, 32, true), "Cannot save Crop preset.");
    const auto id = presets.items()[0].toMap()["id"].toString();
    editor.resetCrop();
    presets.apply(id);
    require(editor.currentSettings() == saved, "Crop preset failed.");
    editor.setCropMode(true);
    require(editor.cropMode() && !editor.cropCanvasReady(),
            "Crop tool exposed an old cropped image.");
    const auto beforeUrl = editor.previewUrl();
    wait([&] { return editor.previewUrl() != beforeUrl; });
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("editor", &editor);
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    QQmlComponent component(&engine);
    component.setData(("import QtQuick\nimport QtQuick.Controls\nimport \"" +
                       QUrl::fromLocalFile(qmlPath).toString() +
                       "\"\nApplicationWindow {width: 640; height: 650; visible: true; Row "
                       "{CropOverlay {width: 320; height: 240} CropPanel {width: 270}}}")
                          .toUtf8(),
                      QUrl());
    wait([&] { return component.status() != QQmlComponent::Loading; });
    if (component.isError())
        throw std::runtime_error(component.errorString().toStdString());
    const std::unique_ptr<QObject> window(component.create());
    require(window != nullptr, "Crop QML did not load.");
    const auto child = [&](const char *name) {
        auto *object = window->findChild<QObject *>(QString::fromLatin1(name));
        require(object != nullptr, "Crop QML control missing.");
        return object;
    };
    auto *overlay = child("cropOverlay");
    const auto begin = [&](double x, double y) {
        wait([&] { return editor.cropCanvasReady(); });
        QVariant result;
        require(QMetaObject::invokeMethod(overlay, "beginDrag", Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, x), Q_ARG(QVariant, y)) &&
                    result.toBool(),
                "Crop frame gesture did not begin.");
    };
    const auto move = [&](double x, double y) {
        require(QMetaObject::invokeMethod(overlay, "updateDrag", Q_ARG(QVariant, x),
                                          Q_ARG(QVariant, y)),
                "Crop frame did not move.");
    };
    const auto finish = [&] { click(overlay, "endDrag"); };
    // Move and every edge/corner through the same functions bound to MouseArea.
    begin(144, 108);
    move(154, 113);
    move(164, 118);
    finish();
    require(editor.currentSettings().crop != saved.crop, "Moving Crop did not change bounds.");
    editor.undo();
    require(editor.currentSettings() == saved, "Moving Crop created multiple Undo steps.");
    for (const auto grip : {QPointF(0, 0), QPointF(0.5, 0), QPointF(1, 0), QPointF(0, 0.5),
                            QPointF(1, 0.5), QPointF(0, 1), QPointF(0.5, 1), QPointF(1, 1)}) {
        const auto f = editor.cropState();
        const double x = (f["x"].toDouble() + grip.x() * f["width"].toDouble()) * 320;
        const double y = (f["y"].toDouble() + grip.y() * f["height"].toDouble()) * 240;
        begin(x, y);
        move(x + (grip.x() == 0   ? 8
                  : grip.x() == 1 ? -8
                                  : 0),
             y + (grip.y() == 0   ? 8
                  : grip.y() == 1 ? -8
                                  : 0));
        finish();
        require(editor.currentSettings().crop != saved.crop, "Crop resize grip did not work.");
        editor.undo();
        require(editor.currentSettings() == saved, "Crop resize Undo failed.");
    }
    // Actual pointer events check the MouseArea pressed/move/release bindings.
    auto *quick = qobject_cast<QQuickWindow *>(window.get());
    require(quick, "Crop test window missing.");
    wait([&] { return editor.cropCanvasReady(); });
    QCoreApplication::processEvents();
    const QPointF start(144, 108), end(157, 114);
    QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QMouseEvent moved(QEvent::MouseMove, end, end, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent released(QEvent::MouseButtonRelease, end, end, Qt::LeftButton, Qt::NoButton,
                         Qt::NoModifier);
    QCoreApplication::sendEvent(quick, &press);
    QCoreApplication::sendEvent(quick, &moved);
    QCoreApplication::sendEvent(quick, &released);
    require(editor.currentSettings().crop != saved.crop, "MouseArea did not move Crop.");
    editor.undo();
    require(editor.currentSettings() == saved, "Pointer gesture was not one Undo.");
    auto *aspect = child("cropAspect");
    for (int index = 0; index < 6; ++index) {
        require(QMetaObject::invokeMethod(aspect, "activated", Q_ARG(int, index)),
                "Cannot select crop aspect.");
        const auto f = editor.cropState();
        if (index == 2)
            require(f["outputWidth"] == f["outputHeight"], "Square aspect failed.");
    }
    require(QMetaObject::invokeMethod(aspect, "activated", Q_ARG(int, 2)),
            "Cannot lock square crop.");
    for (const auto grip : {QPointF(0, 0), QPointF(1, 0.5)}) {
        wait([&] { return editor.cropCanvasReady(); });
        const auto beforeLocked = editor.currentSettings();
        const auto f = editor.cropState();
        const double x = (f["x"].toDouble() + grip.x() * f["width"].toDouble()) * 320;
        const double y = (f["y"].toDouble() + grip.y() * f["height"].toDouble()) * 240;
        begin(x, y);
        move(x + (grip.x() == 0 ? 9 : -9), y + (grip.y() == 0 ? 9 : 0));
        finish();
        const auto resized = editor.cropState();
        require(resized["outputWidth"] == resized["outputHeight"],
                "Locked resize lost the square ratio.");
        editor.undo();
        require(editor.currentSettings() == beforeLocked, "Locked resize Undo failed.");
    }
    child("cropRatioWidth")->setProperty("text", "7");
    child("cropRatioHeight")->setProperty("text", "5");
    child("cropRatioWidth")->setProperty("text", "1000");
    child("cropRatioHeight")->setProperty("text", "0.01");
    require(!child("cropCustomApply")->property("enabled").toBool(),
            "Invalid custom ratio is actionable.");
    child("cropRatioWidth")->setProperty("text", "7");
    child("cropRatioHeight")->setProperty("text", "5");
    click(child("cropCustomApply"));
    require(editor.currentSettings().crop.aspect == 1.4, "Custom ratio failed.");
    click(child("cropAspectSwap"));
    require(std::abs(editor.currentSettings().crop.aspect - 1 / 1.4) < 1e-12,
            "Aspect orientation swap failed.");
    click(child("cropRotateRight"));
    require(editor.currentSettings().crop.quarterTurns == 1, "Rotate right failed.");
    click(child("cropRotateLeft"));
    require(editor.currentSettings().crop.quarterTurns == 0, "Rotate left failed.");
    for (const char *name : {"cropFlipHorizontal", "cropFlipVertical"}) {
        auto *toggle = child(name);
        toggle->setProperty("checked", true);
        click(toggle, "toggled");
    }
    auto *angle = child("cropAngleSlider");
    angle->setProperty("pressed", true);
    angle->setProperty("value", 8);
    click(angle, "moved");
    angle->setProperty("value", 12);
    click(angle, "moved");
    angle->setProperty("pressed", false);
    require(editor.currentSettings().crop.angle == 12, "Straighten binding failed.");
    editor.undo();
    require(editor.currentSettings().crop.angle == 0, "Straighten gesture was not one Undo.");
    auto *guide = child("cropGuide");
    const auto beforeGuides = editor.currentSettings();
    for (int index = 0; index < 4; ++index)
        require(QMetaObject::invokeMethod(guide, "activated", Q_ARG(int, index)),
                "Cannot select guide.");
    require(editor.currentSettings() == beforeGuides, "Crop guides changed edits.");
    editor.setCropMode(true);
    const auto croppedDoc = editor.currentSettings();
    const auto f = editor.cropState();
    const auto destination = root + "/crop-editor.png";
    editor.exportPhoto(QUrl::fromLocalFile(destination), 1, 95, false);
    wait([&] { return !editor.exporting(); });
    require(!editor.cropMode() && QImage(destination).size() ==
                                      QSize(f["outputWidth"].toInt(), f["outputHeight"].toInt()),
            "Export included editing canvas/guides.");
    require(editor.currentSettings() == croppedDoc, "Leaving Crop mutated saved geometry.");
    editor.setCropMode(true);
    presets.preview(id);
    require(!editor.cropMode() && editor.currentSettings() == croppedDoc,
            "Preset preview retained crop tool/changed history.");
    presets.clearPreview();
    editor.setCropMode(true);
    editor.setOriginal(true);
    require(!editor.cropMode(), "Original retained crop frame.");
    editor.setOriginal(false);
    editor.setCropMode(true);
    editor.setTargetedTool("curve.rgb");
    require(!editor.cropMode() && editor.beginTargeted(0.5, 0.5),
            "Targeted adjustment unavailable after Crop.");
    editor.endTargeted();
    QObject completionObserver;
    int completionNotifications = 0;
    QObject::connect(&editor, &EditorController::stateChanged, &completionObserver,
                     [&] { ++completionNotifications; });
    editor.endTargeted();
    require(completionNotifications == 0,
            "Repeated targeted completion emitted stateChanged after its gesture ended.");
    const auto beforeUnrelated = editor.currentSettings();
    editor.beginEdit();
    editor.setParameter("exposure", beforeUnrelated.exposure + .2);
    const auto notificationsBeforeCancel = completionNotifications;
    editor.endTargeted();
    require(completionNotifications == notificationsBeforeCancel,
            "Stale targeted cancellation notified an unrelated active gesture.");
    editor.setParameter("exposure", beforeUnrelated.exposure + .4);
    editor.endEdit();
    editor.undo();
    require(editor.currentSettings() == beforeUnrelated,
            "Stale targeted cancellation split an unrelated gesture's Undo history.");
    click(child("cropReset"));
    require(editor.currentSettings().crop == luma::CropSettings{}, "Crop reset failed.");
    editor.undo();
    require(editor.currentSettings() == croppedDoc, "Crop reset changed excluded settings.");
    wait([&] { return !editor.saving(); });
    require(luma::Catalog(catalog.toStdString()).activeAsset()->edits == croppedDoc,
            "Crop persistence changed after gestures.");
    require(editor.error().isEmpty(), "Crop editor reported unexpected error.");
    if (!warnings.isEmpty())
        throw std::runtime_error(warnings.join('\n').toStdString());
}
