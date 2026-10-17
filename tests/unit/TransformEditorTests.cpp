#include "apps/desktop/EditorController.h"
#include "apps/desktop/PresetController.h"
#include <QElapsedTimer>
#include <QImage>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QThread>
#include <memory>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void wait(const std::function<bool()> &ready) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 30000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    require(ready(), "Transform editor wait timed out.");
}
void action(QObject *item, const char *signal = "clicked") {
    require(item && QMetaObject::invokeMethod(item, signal), "Transform QML action failed.");
}
} // namespace
void transformEditorTests(const QString &source, const QString &root, const QString &qmlPath) {
    PreviewProvider provider;
    const auto catalog = root + "/transform.sqlite";
    EditorController editor(catalog.toStdString(), &provider);
    editor.openFile(QUrl::fromLocalFile(source));
    wait([&] { return editor.hasPhoto() && !editor.busy() && !editor.previewUrl().isEmpty(); });
    const auto initial = editor.currentSettings();
    editor.setCropRect(0.1, 0.15, 0.8, 0.7);
    const auto cropped = editor.currentSettings();
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("editor", &editor);
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    QQmlComponent component(&engine);
    component.setData(
        ("import QtQuick\nimport QtQuick.Controls\nimport \"" +
         QUrl::fromLocalFile(qmlPath).toString() +
         "\"\nApplicationWindow {width: 750; height: 950; visible: true; TransformOverlay {width: "
         "400; height: 300} TransformPanel {x:420; width:300}} ")
            .toUtf8(),
        QUrl());
    wait([&] { return component.status() != QQmlComponent::Loading; });
    if (component.isError())
        throw std::runtime_error(component.errorString().toStdString());
    const std::unique_ptr<QObject> window(component.create());
    require(window != nullptr, "Transform QML did not load.");
    auto *quickWindow = qobject_cast<QQuickWindow *>(window.get());
    require(quickWindow != nullptr, "Guided window missing.");
    const std::function<QQuickItem *(QQuickItem *, const QString &)> visualChild =
        [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
        if (parent->objectName() == name)
            return parent;
        for (auto *item : parent->childItems())
            if (auto *found = visualChild(item, name))
                return found;
        return nullptr;
    };
    const auto child = [&](const char *name) {
        auto *item = window->findChild<QObject *>(QString::fromLatin1(name));
        if (!item)
            item = visualChild(quickWindow->contentItem(), QString::fromLatin1(name));
        if (!item)
            throw std::runtime_error(std::string("Transform QML control missing: ") + name + " " +
                                     warnings.join('\n').toStdString());
        return item;
    };
    editor.beginEdit();
    editor.setParameter("transform.rotate", 5);
    editor.setParameter("transform.rotate", 8);
    editor.endEdit();
    const auto manual = editor.currentSettings();
    editor.undo();
    require(editor.currentSettings() == cropped, "Transform drag created multiple Undo entries.");
    editor.redo();
    require(editor.currentSettings() == manual, "Transform Redo changed parameters.");
    editor.setGuidedMode(true);
    require(!editor.guidedCanvasReady(), "Guided tool exposed an old cropped render.");
    wait([&] { return editor.guidedCanvasReady(); });
    auto *overlay = qobject_cast<QQuickItem *>(child("transformOverlay"));
    require(overlay != nullptr, "Guided overlay is not an item.");
    auto *point = qobject_cast<QQuickItem *>(child("transformGuidePoint0_0"));
    require(point != nullptr, "Guide grip missing.");
    const auto start = point->mapToScene(QPointF(8, 8)), finish = start + QPointF(18, 12);
    auto send = [&](QEvent::Type type, const QPointF &p, Qt::MouseButton button,
                    Qt::MouseButtons buttons) {
        QMouseEvent event(type, p, p, button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(quickWindow, &event);
        QCoreApplication::processEvents();
    };
    send(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, finish, Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, finish, Qt::LeftButton, Qt::NoButton);
    require(editor.transformGuides()[0].toMap()["x1"].toDouble() > 0.27,
            "Actual pointer drag did not move a guide endpoint.");
    require(editor.currentSettings() == manual, "Transient guide drag entered autosave/history.");
    for (int layout = 0; layout < 3; ++layout) {
        editor.setGuideLayout(layout);
        const auto defaults = editor.transformGuides();
        editor.moveGuidePoint(0, 0, .4, .3);
        require(editor.transformGuides() != defaults, "Draft guide fixture did not move.");
        editor.setGuidedMode(false);
        require(editor.transformGuides().isEmpty() && editor.currentSettings() == manual,
                "Closing Guided retained a draft or changed persisted edits.");
        editor.setGuidedMode(true);
        wait([&] { return editor.guidedCanvasReady(); });
        require(editor.transformGuideLayout() == layout && editor.transformGuides() == defaults &&
                    child("transformGuideLayout")->property("currentIndex").toInt() == layout,
                "Reopening Guided lost its selected layout or retained draft endpoints.");
    }
    auto project = [](double x, double y) {
        const double d = 1 + 0.13 * x - 0.12 * y;
        return std::array<double, 2>{((0.85 * x - 0.06 * y) / d + 1) / 2,
                                     ((0.04 * x + 0.85 * y) / d + 1) / 2};
    };
    editor.setGuideLayout(2);
    for (int line = 0; line < 4; ++line)
        for (int endpoint = 0; endpoint < 2; ++endpoint) {
            const double edge = endpoint == 0 ? -0.75 : 0.75;
            const double fixed = line % 2 == 0 ? -0.55 : 0.55;
            const auto p = line < 2 ? project(fixed, edge) : project(edge, fixed);
            editor.moveGuidePoint(line, endpoint, p[0], p[1]);
        }
    action(child("transformGuideApply"));
    require(!editor.guidedMode() && editor.currentSettings().transform.guided &&
                editor.currentSettings().crop == cropped.crop,
            "Guided apply failed or erased Crop.");
    const auto guided = editor.currentSettings();
    editor.undo();
    require(editor.currentSettings() == manual, "Guided apply was not one Undo.");
    editor.redo();
    require(editor.currentSettings() == guided, "Guided Redo changed matrix.");
    editor.setGuidedMode(true);
    wait([&] { return editor.guidedCanvasReady(); });
    editor.setGuideLayout(0);
    editor.moveGuidePoint(0, 1, 0.25, 0.15);
    editor.applyGuides();
    require(editor.currentSettings() == guided && !editor.error().isEmpty(),
            "Degenerate guides altered the edit document.");
    editor.setGuidedMode(false);
    editor.resetTransform();
    require(editor.currentSettings() == cropped, "Reset Transform changed Crop or other groups.");
    action(child("transformAuto"));
    require(editor.transformAutoBusy(), "Auto did not start asynchronously.");
    wait([&] { return !editor.transformAutoBusy(); });
    if (!editor.currentSettings().transform.guided || editor.currentSettings().crop != cropped.crop)
        throw std::runtime_error("Auto failed on calibrated building fixture: " +
                                 editor.error().toStdString());
    const auto automatic = editor.currentSettings();
    editor.undo();
    require(editor.currentSettings() == cropped, "Auto was not one Undo.");
    editor.redo();
    require(editor.currentSettings() == automatic, "Auto Redo changed its result.");
    editor.autoTransform();
    editor.setParameter("exposure", 0.3);
    const auto newer = editor.currentSettings();
    require(!editor.transformAutoBusy(), "Edit did not cancel an obsolete Auto request.");
    wait([&] { return !editor.saving(); });
    require(editor.currentSettings() == newer, "Late Auto result overwrote a newer edit.");
    PresetController presets(root + "/transform-presets.json", editor);
    presets.copySettings(128);
    editor.resetTransform();
    presets.pasteSettings();
    require(editor.currentSettings() == newer, "Geometry Copy/Paste lost solved matrix.");
    require(presets.save({}, "Geometry", {}, 128, true), "Cannot save Geometry preset.");
    const auto presetID = presets.items()[0].toMap()["id"].toString();
    editor.resetTransform();
    const auto reset = editor.currentSettings();
    presets.preview(presetID);
    require(editor.currentSettings() == reset && editor.presetPreview(),
            "Geometry hover became durable.");
    presets.clearPreview();
    presets.apply(presetID);
    require(editor.currentSettings() == newer, "Geometry preset did not restore matrix.");
    editor.setGuidedMode(true);
    wait([&] { return editor.guidedCanvasReady(); });
    editor.setCropMode(true);
    require(!editor.guidedMode() && editor.cropMode(), "Crop and Guided tools overlap.");
    editor.setGuidedMode(true);
    wait([&] { return editor.guidedCanvasReady(); });
    editor.setOriginal(true);
    require(!editor.guidedMode() && editor.original(), "Original did not close Guided.");
    editor.setOriginal(false);
    editor.setGuidedMode(true);
    wait([&] { return editor.guidedCanvasReady(); });
    editor.setTargetedTool("curve.red");
    require(!editor.guidedMode(), "Targeted tool did not close Guided.");
    require(editor.beginTargeted(0.4, 0.5), "Targeted tool could not inverse-map Transform.");
    editor.adjustTargeted(0.03);
    editor.endTargeted();
    editor.undo();
    require(editor.currentSettings() == newer, "Targeted Undo changed Transform.");
    editor.setTargetedTool("none");
    editor.setGuidedMode(true);
    wait([&] { return editor.guidedCanvasReady(); });
    const auto output = root + "/transformed.png";
    editor.exportPhoto(QUrl::fromLocalFile(output), 1, 92, false);
    wait([&] { return !editor.exporting(); });
    require(!editor.guidedMode() && !QImage(output).isNull(),
            "Export leaked Guided mode or failed.");
    wait([&] { return !editor.saving(); });
    require(luma::Catalog(catalog.toStdString()).activeAsset()->edits == newer,
            "Geometry autosave lost state.");
    PreviewProvider restoredProvider;
    EditorController restored(catalog.toStdString(), &restoredProvider);
    restored.restore();
    wait([&] { return restored.hasPhoto() && !restored.busy(); });
    require(restored.currentSettings() == newer && !restored.guidedMode(),
            "Restart changed geometry or persisted transient guides.");
    QCoreApplication::processEvents();
    if (!warnings.empty())
        throw std::runtime_error(warnings.join('\n').toStdString());
    (void)initial;
}
