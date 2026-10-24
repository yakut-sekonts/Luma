#include "apps/desktop/EditorController.h"
#include "apps/desktop/PresetController.h"
#include "apps/shared/PresetStore.h"
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <functional>
#include <iostream>
#include <source_location>

void filterEditorTests(const QString &source, const QString &root, const QString &qmlPath);
void cropEditorTests(const QString &source, const QString &root, const QString &qmlPath);
void transformEditorTests(const QString &source, const QString &root, const QString &qmlPath);
void lensEditorTests(const QString &source, const QString &root, const QString &qmlPath);
void xmpPresetTests(const QString &source, const QString &root, const QString &qmlPath);
namespace {
void require(bool result, const char *message) {
    if (!result)
        throw std::runtime_error(message);
}
template <typename F> void throws(F operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Expected invalid preset to be rejected.");
}
QByteArray read(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read test fixture.");
    return file.readAll();
}
void write(const QString &path, const QByteArray &data) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(),
            "Cannot write test fixture.");
}
void waitUntil(const std::function<bool()> &ready,
               std::source_location location = std::source_location::current()) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 30000) {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    if (!ready())
        throw std::runtime_error("Timed out waiting for editor at PresetTests.cpp:" +
                                 std::to_string(location.line()));
}
luma::EditDocument sample() {
    luma::EditDocument doc;
    doc.setParameter("exposure", 1.25);
    doc.setParameter("contrast", 12);
    doc.setParameter("temperature", 5100);
    doc.setParameter("tint", -13);
    doc.customWhiteBalance = false;
    doc.setParameter("vibrance", 23);
    doc.setParameter("curve.lights", 20);
    doc.curves[0] = {{{0, 0.03}, {0.4, 0.65}, {1, 0.98}}};
    doc.curves[2] = {{{0, 0}, {0.3, 0.2}, {1, 1}}};
    doc.setParameter("hsl.red.hue", -25);
    doc.setParameter("hsl.blue.luminance", 15);
    doc.enabledCorrections = 512 | 1 | 8 | 128;
    doc.sharpenAmount = 65;
    doc.sharpenRadius = 1.7;
    doc.sharpenDetail = 80;
    doc.sharpenMasking = 30;
    doc.crop = {0.15, 0.2, 0.6, 0.7, 12, 1.5, 1, true, false};
    doc.setEnabled(luma::Correction::crop, false);
    doc.noiseLuminance = 60;
    doc.noiseLuminanceDetail = 25;
    doc.noiseLuminanceContrast = 70;
    doc.noiseColor = 80;
    doc.noiseColorDetail = 35;
    doc.noiseColorSmoothness = 90;
    doc.setEnabled(luma::Correction::colorNoise, false);
    doc.lens = {"0123456789abcdef0123456789abcdef", 75, 125, false};
    doc.setEnabled(luma::Correction::lens, true);
    doc.transform.rotate = 7;
    doc.transform.horizontal = 12;
    doc.transform.vertical = -9;
    doc.transform.aspect = 15;
    doc.transform.scale = 130;
    doc.transform.xOffset = 5;
    doc.transform.yOffset = 3;
    doc.transform.guided = true;
    doc.transform.guide = {1, 0.02, 0, -0.01, 1, 0, 0.03, -0.04};
    doc.setEnabled(luma::Correction::transform, false);
    return doc;
}
void documentTests() {
    const auto source = sample();
    luma::EditDocument current;
    current.setParameter("exposure", -0.75);
    current.setParameter("temperature", 7800);
    current.setParameter("hsl.red.hue", 35);
    for (std::uint32_t groups = 1; groups <= 255; ++groups) {
        const luma::DevelopPreset preset{QStringLiteral("Мягкий портрет"),
                                         QStringLiteral("Портреты"), source, groups};
        const auto encoded = luma::encodePreset(preset);
        const auto decoded = luma::decodePreset(encoded);
        require(decoded.name == preset.name && decoded.folder == preset.folder &&
                    decoded.groups == groups,
                "Preset metadata did not survive JSON round trip.");
        const auto merged = luma::mergeSettings(current, decoded.settings, decoded.groups);
        require(merged == luma::mergeSettings(current, source, groups),
                "JSON changed selected settings.");
        require(luma::mergeSettings(merged, current, groups) == current,
                "Subset changed unselected groups.");
        if (groups & 2)
            require(!merged.customWhiteBalance && merged.temperature == 5100,
                    "As Shot preset became custom white balance.");
    }
    const auto bytes = luma::encodePreset({"Test", {}, source, 15});
    auto legacy = QJsonDocument::fromJson(bytes).object();
    legacy["schemaVersion"] = 1;
    const auto old = luma::decodePreset(QJsonDocument(legacy).toJson());
    require(old.groups == 15 &&
                luma::mergeSettings(current, old.settings, old.groups).sharpenAmount ==
                    current.sharpenAmount,
            "Legacy preset added absent Detail settings.");
    auto legacyDetail =
        QJsonDocument::fromJson(luma::encodePreset({"Detail", {}, source, 16})).object();
    legacyDetail["schemaVersion"] = 1;
    throws([&] { luma::decodePreset(QJsonDocument(legacyDetail).toJson()); });
    auto version2 =
        QJsonDocument::fromJson(luma::encodePreset({"Legacy Detail", {}, source, 16})).object();
    version2["schemaVersion"] = 2;
    auto settings2 = version2["settings"].toObject();
    auto detail2 = settings2["Detail"].toObject();
    for (const auto *name : {"noise.luminance", "noise.luminanceDetail", "noise.luminanceContrast",
                             "noise.color", "noise.colorDetail", "noise.colorSmoothness"})
        detail2.remove(name);
    detail2["enabledCorrections"] = static_cast<int>(source.enabledCorrections & 2048U);
    settings2["Detail"] = detail2;
    version2["settings"] = settings2;
    const auto oldDetail = luma::decodePreset(QJsonDocument(version2).toJson());
    require(oldDetail.groups == 16 && oldDetail.settings.sharpenAmount == source.sharpenAmount &&
                oldDetail.settings.noiseLuminance == 0 && oldDetail.settings.noiseColor == 0 &&
                oldDetail.settings.enabled(luma::Correction::colorNoise),
            "Schema 2 Detail migration changed sharpening or invented noise.");
    auto invalid2 = detail2;
    invalid2["noise.color"] = 20;
    settings2["Detail"] = invalid2;
    version2["settings"] = settings2;
    throws([&] { luma::decodePreset(QJsonDocument(version2).toJson()); });
    auto legacy3 =
        QJsonDocument::fromJson(luma::encodePreset({"Legacy 3", {}, source, 31})).object();
    legacy3["schemaVersion"] = 3;
    const auto old3 = luma::decodePreset(QJsonDocument(legacy3).toJson());
    require(luma::mergeSettings(current, old3.settings, old3.groups).crop == current.crop,
            "Schema 3 introduced absent Crop settings.");
    auto illegalCrop =
        QJsonDocument::fromJson(luma::encodePreset({"Illegal Crop", {}, source, 32})).object();
    illegalCrop["schemaVersion"] = 3;
    throws([&] { luma::decodePreset(QJsonDocument(illegalCrop).toJson()); });
    auto legacy4 =
        QJsonDocument::fromJson(luma::encodePreset({"Legacy 4", {}, source, 63})).object();
    legacy4["schemaVersion"] = 4;
    const auto old4 = luma::decodePreset(QJsonDocument(legacy4).toJson());
    require(luma::mergeSettings(current, old4.settings, old4.groups).lens == current.lens,
            "Schema 4 introduced absent Optics settings.");
    auto illegalOptics =
        QJsonDocument::fromJson(luma::encodePreset({"Illegal Optics", {}, source, 64})).object();
    illegalOptics["schemaVersion"] = 4;
    throws([&] { luma::decodePreset(QJsonDocument(illegalOptics).toJson()); });
    auto legacy5 =
        QJsonDocument::fromJson(luma::encodePreset({"Legacy 5", {}, source, 127})).object();
    legacy5["schemaVersion"] = 5;
    const auto old5 = luma::decodePreset(QJsonDocument(legacy5).toJson());
    require(luma::mergeSettings(current, old5.settings, old5.groups).transform == current.transform,
            "Schema 5 introduced absent Geometry settings.");
    auto illegalGeometry =
        QJsonDocument::fromJson(luma::encodePreset({"Illegal Geometry", {}, source, 128})).object();
    illegalGeometry["schemaVersion"] = 5;
    throws([&] { luma::decodePreset(QJsonDocument(illegalGeometry).toJson()); });
    const auto change = [&](const std::function<void(QJsonObject &)> &mutation) {
        auto root = QJsonDocument::fromJson(bytes).object();
        mutation(root);
        throws([&] { luma::decodePreset(QJsonDocument(root).toJson()); });
    };
    change([](auto &root) { root["schemaVersion"] = 10; });
    change([](auto &root) { root["unexpected"] = true; });
    change([](auto &root) { root["name"] = ""; });
    change([](auto &root) { root["settings"] = QJsonObject{}; });
    change([](auto &root) {
        auto groups = root["settings"].toObject();
        groups["Masks"] = QJsonObject{};
        root["settings"] = groups;
    });
    for (const QJsonValue &value :
         {QJsonValue{}, QJsonValue(true), QJsonValue(7.0), QJsonValue("1.0")})
        change([&](auto &root) {
            auto groups = root["settings"].toObject();
            auto light = groups["Light"].toObject();
            light["exposure"] = value;
            groups["Light"] = light;
            root["settings"] = groups;
        });
    change([](auto &root) {
        auto groups = root["settings"].toObject();
        auto color = groups["Color"].toObject();
        color["enabledCorrections"] = 512;
        groups["Color"] = color;
        root["settings"] = groups;
    });
    change([](auto &root) {
        auto groups = root["settings"].toObject();
        auto curve = groups["Curve"].toObject();
        curve.remove("points");
        groups["Curve"] = curve;
        root["settings"] = groups;
    });
    throws([&] { luma::decodePreset(QByteArray(1024 * 1024 + 1, ' ')); });
    throws([&] { luma::mergeSettings(current, source, 0); });
    throws([&] { luma::mergeSettings(current, source, 4096); });
    auto creativeSource = source;
    creativeSource.setParameter("presence.texture", 37);
    creativeSource.setParameter("point.hue", -45);
    creativeSource.setParameter("grading.shadows.saturation", 25);
    creativeSource.setParameter("vignette.amount", -34);
    creativeSource.setParameter("grain.amount", 48);
    creativeSource.setEnabled(luma::Correction::presence, false);
    creativeSource.setEnabled(luma::Correction::grain, false);
    for (const std::uint32_t groups : {256U, 512U, 1024U, 2048U, 257U, 4095U}) {
        const auto decoded =
            luma::decodePreset(luma::encodePreset({"Creative", {}, creativeSource, groups}));
        require(luma::mergeSettings(current, decoded.settings, decoded.groups) ==
                    luma::mergeSettings(current, creativeSource, groups),
                "Creative preset lost selected parameters or bypass flags.");
    }
    const luma::DevelopPreset sparse{
        "Creative sparse", {}, creativeSource, 256, {"presence.texture"}};
    const auto decodedSparse = luma::decodePreset(luma::encodePreset(sparse));
    require(decodedSparse.parameters == sparse.parameters &&
                luma::mergeSettings(current, decodedSparse.settings, decodedSparse.groups,
                                    decodedSparse.parameters) ==
                    luma::mergeSettings(current, creativeSource, sparse.groups, sparse.parameters),
            "Sparse creative preset changed unselected settings or bypass flags.");
}
void storeTests(const QString &root) {
    const auto path = root + "/store.json";
    luma::PresetStore a(path), b(path);
    const luma::DevelopPreset preset{"Test", "Folder", sample(), 255};
    const auto first = a.put(preset), second = b.put(preset);
    require(a.load().size() == 2 && first != second, "A second writer erased existing presets.");
    auto renamed = preset;
    renamed.name = "Renamed";
    renamed.folder = "Moved";
    a.put(renamed, first);
    const auto loaded = b.load();
    require(loaded[0].preset.name == "Renamed" && loaded[0].preset.folder == "Moved" &&
                loaded[0].preset.settings == preset.settings,
            "Rename/move changed saved settings.");
    const auto exportPath = root + "/export.json";
    luma::PresetStore::exportFile(exportPath, loaded[0].preset);
    require(luma::PresetStore::importFile(exportPath).settings == preset.settings,
            "Export/import changed settings.");
    const auto before = read(path);
    {
        QLockFile lock(path + ".lock");
        require(lock.tryLock(0), "Cannot acquire test lock.");
        throws([&] { a.remove(first); });
        throws([&] { a.put(preset); });
        require(read(path) == before, "Locked mutation changed the collection.");
    }
    throws([&] { a.put(renamed, "missing"); });
    throws([&] { a.remove("missing"); });
    require(read(path) == before, "Missing-id mutation changed collection.");
    write(path, "{invalid");
    throws([&] { a.put(preset); });
    throws([&] { a.remove(first); });
    require(read(path) == "{invalid", "Corrupt collection was overwritten.");
    auto rootJson = QJsonDocument::fromJson(before).object();
    auto entries = rootJson["presets"].toArray();
    entries.append(entries[0]);
    rootJson["presets"] = entries;
    write(path, QJsonDocument(rootJson).toJson());
    throws([&] { a.load(); });
    throws([&] { a.put(preset); });
    auto future = QJsonDocument::fromJson(before).object();
    future["schemaVersion"] = 10;
    const auto futureBytes = QJsonDocument(future).toJson();
    write(path, futureBytes);
    throws([&] { a.put(preset); });
    require(read(path) == futureBytes, "Future collection was overwritten.");
    write(path, before);
    a.remove(first);
    require(b.load().size() == 1 && b.load()[0].id == second, "Delete removed wrong preset.");
}
void qmlTests(EditorController &editor, PresetController &presets, const QString &qmlPath) {
    QQmlEngine engine;
    engine.rootContext()->setContextProperty("editor", &editor);
    engine.rootContext()->setContextProperty("presets", &presets);
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    QQmlComponent component(&engine);
    component.setData(("import QtQuick\nimport QtQuick.Controls\nimport \"" +
                       QUrl::fromLocalFile(qmlPath).toString() +
                       "\"\nApplicationWindow { width: 480; height: 720; visible: true; "
                       "font.family: \"Helvetica Neue\"; PresetPanel { anchors.fill: parent; "
                       "anchors.margins: 20 } }")
                          .toUtf8(),
                      QUrl());
    waitUntil([&] { return component.status() != QQmlComponent::Loading; });
    if (component.status() == QQmlComponent::Error)
        throw std::runtime_error(component.errorString().toStdString());
    const std::unique_ptr<QObject> window(component.create());
    require(window != nullptr, "Preset QML failed to create.");
    const auto child = [&](const char *name) {
        auto *value = window->findChild<QObject *>(QString::fromLatin1(name));
        if (!value) {
            const std::function<QObject *(QQuickItem *)> visit =
                [&](QQuickItem *item) -> QObject * {
                if (item->objectName() == QString::fromLatin1(name))
                    return item;
                for (auto *descendant : item->childItems())
                    if (auto *found = visit(descendant))
                        return found;
                return nullptr;
            };
            value = visit(qobject_cast<QQuickWindow *>(window.get())->contentItem());
        }
        if (!value)
            throw std::runtime_error(std::string("Preset control was not found: ") + name);
        return value;
    };
    child("presetTabs")->setProperty("currentIndex", 1);
    QCoreApplication::processEvents();
    auto *panel = child("presetPanel");
    require(QMetaObject::invokeMethod(child("presetCreate"), "clicked"),
            "Cannot open create dialog.");
    require(child("presetEditor")->property("visible").toBool(), "Create dialog is not visible.");
    child("presetName")->setProperty("text", "QML <b>preset</b>");
    child("presetFolder")->setProperty("text", "<b>QML folder</b>");
    require(QMetaObject::invokeMethod(child("presetSave"), "clicked"),
            "Cannot save from QML dialog.");
    require(!child("presetEditor")->property("visible").toBool(), "Save did not close dialog.");
    require(presets.items().size() == 2, "QML did not persist new preset.");
    QString id;
    for (const auto &item : presets.items())
        if (item.toMap()["name"].toString() == "QML <b>preset</b>")
            id = item.toMap()["id"].toString();
    require(!id.isEmpty(), "QML preset name was not saved.");
    panel->setProperty("selectedId", id);
    require(QMetaObject::invokeMethod(panel, "openEditor", Q_ARG(QVariant, false)),
            "Cannot open edit dialog.");
    require(child("presetName")->property("text").toString() == "QML <b>preset</b>" &&
                child("presetFolder")->property("text").toString() == "<b>QML folder</b>",
            "Edit dialog lost metadata.");
    child("presetName")->setProperty("text", "QML renamed");
    child("presetGroup0")->setProperty("checked", false);
    require(QMetaObject::invokeMethod(child("presetGroup0"), "clicked"),
            "Cannot select subset group.");
    require(child("presetEditor")->property("groups").toInt() == 4094,
            "Group checkbox ignored subset.");
    require(QMetaObject::invokeMethod(child("presetSave"), "clicked"),
            "Cannot update from QML dialog.");
    for (const auto &item : presets.items())
        if (item.toMap()["id"].toString() == id)
            require(item.toMap()["name"].toString() == "QML renamed" &&
                        item.toMap()["groups"].toInt() == 4094,
                    "QML update lost name or group selection.");
    QCoreApplication::processEvents();
    require(child("presetTitle")->property("textFormat").toInt() == Qt::PlainText &&
                child("presetFolderTitle")->property("textFormat").toInt() == Qt::PlainText,
            "Imported preset labels interpreted markup instead of literal text.");
    if (!warnings.isEmpty())
        throw std::runtime_error(warnings.join('\n').toStdString());
}
void editorTests(const QString &source, const QString &root, const QString &qmlPath) {
    PreviewProvider provider;
    const auto catalog = root + "/editor.sqlite";
    EditorController editor(catalog.toStdString(), &provider);
    PresetController presets(root + "/editor-presets.json", editor);
    editor.openFile(QUrl::fromLocalFile(source));
    waitUntil(
        [&] { return !editor.busy() && editor.hasPhoto() && !editor.previewUrl().isEmpty(); });
    editor.setExposure(0.25);
    editor.setParameter("hsl.red.hue", 17);
    editor.setParameter("temperature", 7200);
    editor.setExposure(0.75);
    const auto previous = editor.previewUrl();
    editor.undo();
    waitUntil([&] { return !editor.saving() && editor.previewUrl() != previous; });
    const auto baseline = editor.currentSettings();
    const auto pixels = provider.requestImage({}, nullptr, {});
    require(editor.canRedo(), "Fixture did not retain redo history.");
    require(presets.save({}, "Warm", "Color", 2, true), "Cannot create preset.");
    const auto id = presets.items()[0].toMap()["id"].toString();
    editor.setParameter("temperature", 5100);
    require(presets.save(id, "Warm", "Color", 2, true), "Cannot update preset settings.");
    editor.undo();
    const auto previewBefore = editor.previewUrl();
    waitUntil([&] { return !editor.saving() && editor.previewUrl() != previewBefore; });
    require(editor.currentSettings() == baseline, "Fixture Undo changed baseline.");
    const bool undo = editor.canUndo(), redo = editor.canRedo();
    const auto hoverBefore = editor.previewUrl();
    presets.preview(id);
    require(editor.presetPreview() && editor.currentSettings() == baseline && !editor.saving() &&
                editor.canUndo() == undo && editor.canRedo() == redo,
            "Hover modified edits, persistence or history.");
    waitUntil([&] { return editor.previewUrl() != hoverBefore; });
    require(provider.requestImage({}, nullptr, {}) != pixels,
            "Preset preview did not change rendered pixels.");
    luma::SessionService service(catalog.toStdString());
    require(service.restore()->asset.edits == baseline, "Preview was persisted in the catalog.");
    const auto hoverUrl = editor.previewUrl();
    presets.clearPreview();
    waitUntil([&] { return editor.previewUrl() != hoverUrl; });
    require(provider.requestImage({}, nullptr, {}) == pixels && editor.canRedo() == redo,
            "Leaving preview changed pixels or redo history.");
    presets.apply(id);
    require(editor.currentSettings().temperature == 5100 &&
                editor.currentSettings().exposure == 0.25 &&
                editor.currentSettings().hsl == baseline.hsl,
            "Apply changed excluded settings.");
    editor.undo();
    require(editor.currentSettings() == baseline, "Preset was not exactly one Undo operation.");
    editor.redo();
    const auto applied = editor.currentSettings();
    presets.preview(id);
    editor.setOriginal(true);
    require(!editor.presetPreview(), "Original mode retained preset hover.");
    presets.preview(id);
    require(!editor.presetPreview(), "Hover overrode Original mode.");
    editor.setOriginal(false);
    presets.copySettings(2 | 8);
    editor.resetEdits();
    presets.pasteSettings();
    const auto pasted = luma::mergeSettings({}, applied, 2 | 8);
    require(editor.currentSettings() == pasted, "Copy/Paste ignored selected groups.");
    editor.undo();
    require(editor.currentSettings() == luma::EditDocument{}, "Paste was not one Undo operation.");
    editor.redo();
    waitUntil([&] { return !editor.saving(); });
    require(service.restore()->asset.edits == pasted,
            "Applied settings did not survive catalog reload.");
    const auto exported = root + "/applied.png", hoveredExport = root + "/hovered.png";
    editor.exportPhoto(QUrl::fromLocalFile(exported), 1, 90, false);
    waitUntil([&] { return !editor.exporting(); });
    auto alternative = sample();
    alternative.temperature = 9000;
    alternative.customWhiteBalance = true;
    editor.previewSettings(alternative, 2);
    editor.exportPhoto(QUrl::fromLocalFile(hoveredExport), 1, 90, false);
    waitUntil([&] { return !editor.exporting(); });
    require(read(exported) == read(hoveredExport),
            "Export used hover instead of applied settings.");
    require(!editor.presetPreview(), "Export retained preset hover.");
    require(!presets.save(id, "Warm", "Color", 15, false),
            "Adding groups silently introduced default settings.");
    require(presets.refresh(), "Cannot refresh saved presets.");
    require(presets.save(id, "Renamed", "Moved", 2, false), "Cannot rename/move from controller.");
    PresetController restarted(root + "/editor-presets.json", editor);
    require(restarted.items()[0].toMap()["name"].toString() == "Renamed",
            "Preset did not survive restart.");
    require(!presets.exportPreset(id, QUrl::fromLocalFile(catalog)),
            "Preset export replaced catalog.");
    require(!presets.exportPreset(id, QUrl::fromLocalFile(source)), "Preset export replaced RAW.");
    require(!presets.exportPreset(id, QUrl::fromLocalFile(root + "/editor-presets.json")),
            "Preset export replaced its collection.");
    const auto alias = root + "/collection-alias.json";
    std::filesystem::create_hard_link((root + "/editor-presets.json").toStdString(),
                                      alias.toStdString());
    require(!presets.exportPreset(id, QUrl::fromLocalFile(alias)),
            "Preset export replaced collection alias.");
    require(presets.refresh(), "Cannot clear expected export failures.");
    qmlTests(editor, presets, qmlPath);
    require(editor.error().isEmpty(), "Editor reported an unexpected error.");
}

void detailEditorTests(const QString &source, const QString &root, const QString &qmlPath) {
    PreviewProvider provider;
    const auto catalog = root + "/detail.sqlite";
    EditorController editor(catalog.toStdString(), &provider);
    editor.openFile(QUrl::fromLocalFile(source));
    waitUntil(
        [&] { return editor.hasPhoto() && !editor.busy() && !editor.previewUrl().isEmpty(); });
    const auto baseline = editor.currentSettings();
    editor.beginEdit();
    editor.setParameter("sharpen.amount", 75);
    editor.setParameter("sharpen.radius", 1.8);
    editor.setParameter("sharpen.detail", 65);
    editor.setParameter("sharpen.masking", 40);
    editor.setParameter("noise.luminance", 55);
    editor.setParameter("noise.luminanceDetail", 70);
    editor.setParameter("noise.luminanceContrast", 20);
    editor.setParameter("noise.color", 65);
    editor.setParameter("noise.colorDetail", 30);
    editor.setParameter("noise.colorSmoothness", 80);
    editor.endEdit();
    const auto saved = editor.currentSettings();
    waitUntil([&] { return !editor.saving(); });
    editor.undo();
    require(editor.currentSettings() == baseline, "Sharpening gesture was not one Undo.");
    editor.redo();
    require(editor.currentSettings() == saved, "Sharpening Redo changed settings.");
    waitUntil([&] { return !editor.saving(); });
    PresetController detailPresets(root + "/detail-presets.json", editor);
    detailPresets.copySettings(16);
    editor.setParameter("sharpen.amount", 10);
    editor.setParameter("noise.color", 15);
    detailPresets.pasteSettings();
    require(editor.currentSettings() == saved, "Detail Copy/Paste did not preserve the subset.");
    editor.undo();
    require(editor.currentSettings().sharpenAmount == 10, "Detail Paste was not one Undo.");
    editor.redo();
    require(detailPresets.save({}, "Sharp", {}, 16, true), "Cannot save Detail preset.");
    const auto detailID = detailPresets.items()[0].toMap()["id"].toString();
    editor.setParameter("sharpen.masking", 90);
    detailPresets.apply(detailID);
    require(editor.currentSettings() == saved, "Applying Detail preset changed settings.");
    editor.undo();
    require(editor.currentSettings().sharpenMasking == 90, "Detail preset was not one Undo.");
    editor.redo();
    waitUntil([&] { return !editor.saving(); });
    const auto oldUrl = editor.previewUrl();
    QKeyEvent option(QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &option);
    editor.setDetailControl("sharpen.masking");
    waitUntil(
        [&] { return editor.previewUrl() != oldUrl && !editor.detailDiagnostic().isEmpty(); });
    const auto undo = editor.canUndo();
    require(editor.currentSettings() == saved && !editor.saving(),
            "Diagnostic changed saved edits.");
    const auto diagnostic = provider.requestImage({}, nullptr, {});
    for (int y = 0; y < diagnostic.height(); ++y)
        for (int x = 0; x < diagnostic.width(); ++x) {
            const auto pixel = diagnostic.pixel(x, y);
            require(qRed(pixel) == qGreen(pixel) && qGreen(pixel) == qBlue(pixel),
                    "Mask diagnostic has color.");
        }
    const auto first = root + "/diagnostic-export.png";
    editor.exportPhoto(QUrl::fromLocalFile(first), 1, 95, false);
    waitUntil([&] { return !editor.exporting(); });
    require(QFile::exists(first), "Export during diagnostic failed.");
    require(editor.detailDiagnostic().isEmpty(), "Export retained the diagnostic gesture.");
    const auto afterExport = editor.previewUrl();
    editor.setDetailControl("sharpen.masking");
    waitUntil(
        [&] { return !editor.detailDiagnostic().isEmpty() && editor.previewUrl() != afterExport; });
    const auto diagnosticUrl = editor.previewUrl();
    QKeyEvent released(QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &released);
    require(editor.detailDiagnostic().isEmpty(), "Option release retained diagnostic state.");
    waitUntil([&] {
        if (!editor.error().isEmpty())
            throw std::runtime_error(editor.error().toStdString());
        return editor.previewUrl() != diagnosticUrl;
    });
    require(editor.currentSettings() == saved && editor.canUndo() == undo && !editor.saving(),
            "Option release changed history/autosave.");
    const auto second = root + "/normal-export.png";
    editor.exportPhoto(QUrl::fromLocalFile(second), 1, 95, false);
    waitUntil([&] { return !editor.exporting(); });
    require(read(first) == read(second), "Diagnostic leaked into exported pixels.");
    editor.setDetailControl("sharpen.radius");
    QCoreApplication::sendEvent(QCoreApplication::instance(), &option);
    require(!editor.detailDiagnostic().isEmpty(),
            "Option during gesture did not start diagnostic.");
    QEvent deactivate(QEvent::ApplicationDeactivate);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &deactivate);
    require(editor.detailDiagnostic().isEmpty(), "App deactivation retained diagnostic.");
    // Actual QML pressed/moved bindings drive the same gesture and temporary state.
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
                       "\"\nApplicationWindow {width: 290; height: 400; visible: true; DetailPanel "
                       "{width: 250} }")
                          .toUtf8(),
                      QUrl());
    waitUntil([&] { return component.status() != QQmlComponent::Loading; });
    if (component.isError())
        throw std::runtime_error(component.errorString().toStdString());
    const std::unique_ptr<QObject> window(component.create());
    require(window != nullptr, "Detail QML did not create.");
    auto *slider = window->findChild<QObject *>("sharpen.amountSlider");
    require(slider && slider->property("enabled").toBool(), "Sharpening slider is not available.");
    QCoreApplication::sendEvent(QCoreApplication::instance(), &option);
    require(slider->setProperty("pressed", true), "Cannot press QML sharpening slider.");
    require(!editor.detailDiagnostic().isEmpty(), "QML press did not start Option diagnostic.");
    slider->setProperty("value", 95);
    require(QMetaObject::invokeMethod(slider, "moved"), "Cannot move QML sharpening slider.");
    slider->setProperty("value", 105);
    QMetaObject::invokeMethod(slider, "moved");
    slider->setProperty("pressed", false);
    require(editor.detailDiagnostic().isEmpty() && editor.currentSettings().sharpenAmount == 105,
            "QML release did not finish the gesture.");
    QCoreApplication::sendEvent(QCoreApplication::instance(), &released);
    editor.undo();
    require(editor.currentSettings() == saved, "QML gesture created more than one Undo.");
    for (const auto *name : {"noise.luminance", "noise.luminanceDetail", "noise.luminanceContrast",
                             "noise.color", "noise.colorDetail", "noise.colorSmoothness"}) {
        auto *noiseSlider = window->findChild<QObject *>(QString::fromLatin1(name) + "Slider");
        require(noiseSlider && noiseSlider->property("enabled").toBool(),
                "Noise slider not available.");
        const auto beforeGesture = editor.currentSettings();
        QCoreApplication::sendEvent(QCoreApplication::instance(), &option);
        noiseSlider->setProperty("pressed", true);
        require(!editor.detailDiagnostic().isEmpty(), "Noise Option diagnostic did not start.");
        noiseSlider->setProperty("value", 45);
        QMetaObject::invokeMethod(noiseSlider, "moved");
        noiseSlider->setProperty("value", 85);
        QMetaObject::invokeMethod(noiseSlider, "moved");
        noiseSlider->setProperty("pressed", false);
        require(editor.detailDiagnostic().isEmpty() &&
                    editor.currentSettings().parameter(name) == 85,
                "Noise QML gesture failed.");
        QCoreApplication::sendEvent(QCoreApplication::instance(), &released);
        editor.undo();
        require(editor.currentSettings() == beforeGesture,
                "Noise gesture created multiple Undo steps.");
    }
    const auto noiseExport = root + "/noise-diagnostic-export.png";
    QCoreApplication::sendEvent(QCoreApplication::instance(), &option);
    editor.setDetailControl("noise.color");
    require(!editor.detailDiagnostic().isEmpty(), "Color diagnostic unavailable.");
    editor.exportPhoto(QUrl::fromLocalFile(noiseExport), 1, 95, false);
    waitUntil([&] { return !editor.exporting(); });
    require(editor.detailDiagnostic().isEmpty() && read(noiseExport) == read(second),
            "Noise diagnostic entered export.");
    editor.setDetailControl("noise.luminanceDetail");
    const auto noiseUrl = editor.previewUrl();
    waitUntil([&] { return editor.previewUrl() != noiseUrl; });
    const auto lumaDiagnostic = provider.requestImage({}, nullptr, {});
    for (int y = 0; y < lumaDiagnostic.height(); ++y)
        for (int x = 0; x < lumaDiagnostic.width(); ++x) {
            const auto pixel = lumaDiagnostic.pixel(x, y);
            require(qRed(pixel) == qGreen(pixel) && qGreen(pixel) == qBlue(pixel),
                    "Noise luminance view contains color.");
        }
    QCoreApplication::sendEvent(QCoreApplication::instance(), &released);
    require(editor.detailDiagnostic().isEmpty(), "Noise Option release retained mode.");
    for (const auto *name : {"luminanceNoise", "colorNoise"}) {
        auto *toggle = window->findChild<QObject *>(QString::fromLatin1(name) + "Enabled");
        require(toggle != nullptr, "Noise enable checkbox missing.");
        toggle->setProperty("checked", false);
        QMetaObject::invokeMethod(toggle, "toggled");
        require(!editor.currentSettings().enabled(luma::EditDocument::correction(name)),
                "Noise toggle failed.");
        editor.undo();
        require(editor.currentSettings() == saved, "Noise toggle Undo failed.");
    }
    waitUntil([&] { return !editor.saving(); });
    require(luma::Catalog(catalog.toStdString()).activeAsset()->edits == saved,
            "Restart data differs from actual sharpening settings.");
    require(editor.error().isEmpty(), "Unexpected Detail editor error.");
    if (!warnings.isEmpty())
        throw std::runtime_error(warnings.join('\n').toStdString());
}
} // namespace
void localEditorTests(const QString &, const QString &, const QString &);
void completeLocalEditorTests(const QString &, const QString &, const QString &);
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    try {
        require(argc == 5, "Expected synthetic RAW and QML fixture paths.");
        QTemporaryDir temporary;
        require(temporary.isValid(), "Cannot create preset test directory.");
        documentTests();
        storeTests(temporary.path());
        xmpPresetTests(QString::fromUtf8(argv[1]), temporary.path(), QString::fromUtf8(argv[2]));
        editorTests(QString::fromUtf8(argv[1]), temporary.path(), QString::fromUtf8(argv[2]));
        filterEditorTests(QString::fromUtf8(argv[1]), temporary.path(), QString::fromUtf8(argv[2]));
        cropEditorTests(QString::fromLocal8Bit(argv[1]), temporary.path(),
                        QString::fromLocal8Bit(argv[2]));
        lensEditorTests(QString::fromLocal8Bit(argv[3]), temporary.path(),
                        QString::fromLocal8Bit(argv[2]));
        transformEditorTests(QString::fromLocal8Bit(argv[4]), temporary.path(),
                             QString::fromLocal8Bit(argv[2]));
        completeLocalEditorTests(QString::fromUtf8(argv[1]), temporary.path(),
                                 QString::fromUtf8(argv[2]));
        localEditorTests(QString::fromUtf8(argv[1]), temporary.path(), QString::fromUtf8(argv[2]));
        detailEditorTests(QString::fromUtf8(argv[1]), temporary.path(), QString::fromUtf8(argv[2]));
        std::cout
            << "Preset subsets, JSON validation, atomic storage, hover, Undo and restart passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
