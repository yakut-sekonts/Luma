#include "apps/shared/PhotoDecoder.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/DisplayProfile.h"
#include "tests/ux/QueuedCallGate.h"
#include "tests/ux/UxContext.h"
#include <QCoreApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace ux {
namespace {
QByteArray bytes(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read preset evidence: " + path);
    const auto result = file.readAll();
    require(file.error() == QFileDevice::NoError, "Cannot read complete preset evidence");
    return result;
}
void fixtureFile(Context &c, const QString &path, const QByteArray &contents) {
    // Fixture preparation, never an application mutation or native chooser selection.
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size(),
            "Cannot prepare preset fixture: " + path);
    file.close();
    require(file.error() == QFileDevice::NoError, "Cannot finish preset fixture");
    c.driver->log({{"action", "fixture_file"}, {"path", path}, {"bytes", contents.size()}});
}
QString storePath(const Context &c) {
    return c.stateRoot + "/presets.json";
}
QSet<QString> identities(const QVariantList &items) {
    QSet<QString> result;
    for (const auto &value : items)
        result.insert(value.toMap()["id"].toString());
    return result;
}
void groups(Context &c, std::uint32_t selected) {
    for (int group = 0; group < 8; ++group) {
        const auto selector = "presetGroup" + QString::number(group);
        const bool expected = selected & (std::uint32_t{1} << group);
        if (c.driver->object(selector)->property("checked").toBool() != expected)
            c.driver->click(selector);
    }
    require(c.driver->object("presetEditor")->property("groups").toUInt() == selected,
            "Preset dialog did not acknowledge its selected groups");
}
void userTab(Context &c) {
    c.driver->click("preset.tabs.presets");
    c.driver->wait([&] { return c.driver->item("presetList")->isVisible(); },
                   "user preset list visible");
}
void visibleRow(Context &c, const QString &listID, const QString &rowID) {
    auto &d = *c.driver;
    auto *list = d.item(listID);
    for (int attempt = 0; attempt < 80; ++attempt) {
        auto *row = qobject_cast<QQuickItem *>(d.find(rowID));
        if (row) {
            const auto center = row->mapToItem(list, {row->width() / 2, row->height() / 2});
            if (row->isVisible() && list->contains(center))
                return;
            d.wheel(listID, center.y() < 0 ? 120 : -120);
        } else {
            d.wheel(listID, -120);
        }
        d.frame();
    }
    require(false, "Preset row did not become reachable by wheel: " + rowID);
}
void openAction(Context &c, const QString &selector) {
    auto &d = *c.driver;
    d.click("preset.actions");
    d.wait([&] { return d.object("preset.actions.menu")->property("opened").toBool(); },
           "preset actions menu opened");
    d.click(selector);
}
void editorOpened(Context &c) {
    c.driver->wait([&] { return c.driver->object("presetEditor")->property("opened").toBool(); },
                   "preset metadata dialog opened");
}
void editorClosed(Context &c) {
    c.driver->wait([&] { return !c.driver->object("presetEditor")->property("visible").toBool(); },
                   "preset metadata dialog closed");
    require(c.presets.error().isEmpty(), c.presets.error());
}
QString create(Context &c, const QString &name, std::uint32_t selected = 3,
               const QString &folder = "UX presets") {
    auto &d = *c.driver;
    userTab(c);
    const auto before = identities(c.presets.items());
    d.click("presetCreate");
    editorOpened(c);
    d.type("presetName", name);
    d.type("presetFolder", folder);
    groups(c, selected);
    d.click("presetSave");
    editorClosed(c);
    auto added = identities(c.presets.items());
    added.subtract(before);
    require(added.size() == 1 && c.presets.items().size() == before.size() + 1,
            "Creating a preset changed another entry or added more than one UUID");
    const auto id = *added.cbegin();
    d.wait([&] { return d.find("preset." + id) != nullptr; }, "created preset row ready");
    return id;
}
void select(Context &c, const QString &id) {
    c.driver->click("preset." + id);
    require(c.driver->object("presetPanel")->property("selectedId").toString() == id,
            "Preset row did not select its own UUID");
}
void openEdit(Context &c, const QString &id) {
    select(c, id);
    openAction(c, "preset.actions.edit");
    editorOpened(c);
    require(!c.driver->object("preset.capture")->property("checked").toBool(),
            "Editing metadata unexpectedly enabled recapturing");
}
void remove(Context &c, const QString &id) {
    auto &d = *c.driver;
    select(c, id);
    openAction(c, "preset.actions.delete");
    d.wait([&] { return d.object("preset.deleteDialog")->property("opened").toBool(); },
           "preset delete confirmation opened");
    require(d.object("preset.deleteDialog")->property("presetId").toString() == id,
            "Delete dialog captured a different preset UUID");
    d.click("preset.deleteConfirm");
    d.wait([&] { return !d.object("preset.deleteDialog")->property("visible").toBool(); },
           "preset delete confirmation accepted");
    require(!identities(c.presets.items()).contains(id) && c.presets.error().isEmpty(),
            "Delete did not remove exactly the selected preset");
}
void chooserBoundary(Context &c, const QString &dialogID, const QString &path,
                     const QString &exportedID = {}) {
    // Select only a known public fixture path at the FileDialog boundary. We do not
    // open/select a native NSOpenPanel/NSSavePanel and do not claim that coverage.
    auto *dialog = c.driver->object(dialogID);
    require(!dialog->property("visible").toBool(), "Chooser boundary requires a closed dialog");
    if (!exportedID.isEmpty())
        require(dialog->setProperty("presetId", exportedID), "Missing export dialog presetId");
    require(dialog->setProperty("selectedFile", QUrl::fromLocalFile(path)),
            "FileDialog refused its selected fixture path");
    require(dialog->property("selectedFile").toUrl().toLocalFile() == path,
            "FileDialog changed the selected fixture path");
    c.driver->log({{"action", "file_dialog_selection_fixture_boundary"},
                   {"id", dialogID},
                   {"path", path},
                   {"native_selection_exercised", false}});
    require(QMetaObject::invokeMethod(dialog, "accepted", Qt::DirectConnection),
            "FileDialog has no production accepted handler");
    c.driver->frame();
}
QImage pixels(Context &c, const luma::EditDocument &document) {
    const auto decoded = luma::PhotoDecoder().decode(c.editor.sourcePath().toStdString());
    require(decoded.image.width <= 512 && decoded.image.height <= 512,
            "Preset pixel oracle requires a small public fixture");
    const auto profile = QGuiApplication::platformName() == "cocoa"
                             ? luma::macos::displayProfile(c.window->winId())
                             : std::vector<std::uint8_t>{};
    const auto rendered =
        luma::renderCPU(decoded.image, document, decoded.metadata.orientation, profile, {},
                        decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
    return QImage(rendered.rgba.data(), rendered.width, rendered.height, rendered.width * 4,
                  QImage::Format_RGBA8888)
        .copy();
}
void ready(Context &c, const luma::EditDocument &document) {
    const auto expected = pixels(c, document);
    try {
        c.driver->wait(
            [&] {
                auto *image = c.driver->item("develop.preview");
                return !c.editor.busy() && !c.editor.previewUrl().isEmpty() && image->isVisible() &&
                       image->property("status").toInt() == 1 &&
                       image->property("source").toUrl().toString() == c.editor.previewUrl() &&
                       c.provider->requestImage({}, nullptr, {}) == expected;
            },
            "preset fixture pixels match the current document");
    } catch (...) {
        auto *image = c.driver->item("develop.preview");
        c.driver->log(
            {{"action", "preset_pixel_readiness_failure"},
             {"provider_matches", c.provider->requestImage({}, nullptr, {}) == expected},
             {"image_visible", image->isVisible()},
             {"image_status", image->property("status").toInt()},
             {"image_source", image->property("source").toUrl().toString()},
             {"preview_url", c.editor.previewUrl()},
             {"preset_preview", c.editor.presetPreview()},
             {"natural_hovered", c.driver->object("filter_natural")->property("hovered").toBool()},
             {"portrait_hovered",
              c.driver->object("filter_soft-portrait")->property("hovered").toBool()},
             {"landscape_hovered",
              c.driver->object("filter_landscape")->property("hovered").toBool()}});
        throw;
    }
}
class HoverTrace final : public QObject {
  public:
    explicit HoverTrace(Context &c) : context_(c) {
        c.window->installEventFilter(this);
        for (const auto *name : {"filter_natural", "filter_soft-portrait", "filter_landscape"})
            c.driver->object(name)->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        const auto type = event->type();
        if (type == QEvent::Enter || type == QEvent::Leave || type == QEvent::HoverEnter ||
            type == QEvent::HoverLeave || type == QEvent::HoverMove || type == QEvent::MouseMove) {
            QJsonObject values{{"action", "hover_input_event"},
                               {"id", object->objectName()},
                               {"type", static_cast<int>(type)},
                               {"spontaneous", event->spontaneous()}};
            if (const auto *point = dynamic_cast<QSinglePointEvent *>(event)) {
                values["x"] = point->position().x();
                values["y"] = point->position().y();
                values["global_x"] = point->globalPosition().x();
                values["global_y"] = point->globalPosition().y();
            }
            context_.driver->log(values);
        }
        return false;
    }

  private:
    Context &context_;
};
void editLightColor(Context &c) {
    for (const auto *selector : {"develop.exposureSlider", "develop.saturationSlider"}) {
        c.reveal(selector);
        c.driver->drag(selector, c.driver->sliderThumb(selector), {.65, .5});
    }
    c.saved();
    ready(c, c.editor.currentSettings());
}
void copyLight(Context &c) {
    auto &d = *c.driver;
    d.click("preset.copy");
    editorOpened(c);
    require(d.object("presetEditor")->property("copyMode").toBool(),
            "Copy button did not open its subset dialog");
    groups(c, 1);
    d.click("presetSave");
    editorClosed(c);
    const auto copied = c.presets.copiedSettings();
    require(copied && copied->groups == 1 && copied->settings == c.editor.currentSettings() &&
                d.item("preset.paste")->isEnabled(),
            "Copy did not capture a complete in-memory Light snapshot");
}
void quit(Context &c) {
    c.driver->quitShortcut();
    c.driver->wait([&] { return !c.window->isVisible() && !c.editor.saving() && !c.editor.busy(); },
                   "preset restart stage closed safely");
    c.checkData();
}
void checkStore(Context &c, const QSet<QString> &expected) {
    const auto saved = luma::PresetStore(storePath(c)).load();
    QSet<QString> stored;
    for (const auto &entry : saved) {
        require(!stored.contains(entry.id), "Preset store contains duplicate UUIDs");
        stored.insert(entry.id);
        require(luma::encodePreset(entry.preset) ==
                    luma::encodePreset(c.presets.snapshot(entry.id)),
                "Preset controller and durable store disagree");
    }
    require(stored == expected && identities(c.presets.items()) == expected,
            "Preset store lost or duplicated a live entry");
    require(!QFileInfo::exists(storePath(c) + ".lock"), "Preset operation leaked its store lock");
}
} // namespace

bool presetRemainingScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-PRS-002", "UX-PRS-003", "UX-PRS-006", "UX-PRS-009",
                                         "UX-PRS-010", "UX-PRS-012", "UX-PRS-017", "UX-PRS-019"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    c.openFirst();
    editLightColor(c);
    const auto document = c.editor.currentSettings();

    if (id == "UX-PRS-002") {
        const auto presetID = create(c, "User preset 🌅");
        const auto original = c.presets.snapshot(presetID);
        openEdit(c, presetID);
        d.type("presetName", "Renamed preset Հայերեն");
        d.type("presetFolder", "Moved folder 中文");
        d.click("presetSave");
        editorClosed(c);
        const auto renamed = c.presets.snapshot(presetID);
        require(renamed.name == "Renamed preset Հայերեն" && renamed.folder == "Moved folder 中文" &&
                    renamed.settings == original.settings && renamed.groups == original.groups &&
                    renamed.parameters == original.parameters,
                "Rename/move changed preset UUID or captured settings");
        checkStore(c, {presetID});
        openEdit(c, presetID);
        groups(c, 1);
        d.click("presetSave");
        editorClosed(c);
        auto reduced = renamed;
        reduced.groups = 1;
        require(luma::encodePreset(c.presets.snapshot(presetID)) == luma::encodePreset(reduced),
                "Removing a preset group recaptured unrelated edits");
        openEdit(c, presetID);
        groups(c, 3);
        const auto beforeRefusedSave = bytes(storePath(c));
        d.click("presetSave");
        require(d.object("presetEditor")->property("visible").toBool() &&
                    c.presets.error().contains("Capture current settings") &&
                    d.item("preset.editorError")->isVisible() &&
                    bytes(storePath(c)) == beforeRefusedSave,
                "Adding a group without explicit recapture did not fail safely");
        d.click("preset.capture");
        d.click("presetSave");
        editorClosed(c);
        const auto recaptured = c.presets.snapshot(presetID);
        require(recaptured.groups == 3 && recaptured.settings == document,
                "Explicit recapture did not save the selected current settings");
        remove(c, presetID);
        checkStore(c, {});
        require(c.editor.currentSettings() == document,
                "Preset metadata/delete operations modified the active photo");
        return true;
    }
    if (id == "UX-PRS-003") {
        // Built-in rows apply a look but never become user-preset selections.
        require(c.presets.items().isEmpty(), "Read-only fixture unexpectedly has user presets");
        const auto originals = c.presets.builtinItems();
        for (const auto &item : originals) {
            const auto presetID = item.toMap()["id"].toString();
            const auto original = luma::encodePreset(c.presets.snapshot(presetID));
            visibleRow(c, "builtinFilterList", "filter_" + presetID.mid(8));
            d.click("filter_" + presetID.mid(8));
            require(d.object("presetPanel")->property("selectedId").toString().isEmpty() &&
                        !d.item("preset.actions")->isVisible(),
                    "Built-in filter exposed user rename/delete actions");
            d.move("develop.preview", {.5, .5});
            require(luma::encodePreset(c.presets.snapshot(presetID)) == original,
                    "Applying a built-in filter changed its authored definition");
        }
        userTab(c);
        d.click("preset.actions");
        d.wait([&] { return d.object("preset.actions.menu")->property("opened").toBool(); },
               "read-only actions menu opened");
        require(!d.item("preset.actions.edit")->isEnabled() &&
                    !d.item("preset.actions.delete")->isEnabled(),
                "No user preset selected, but rename/delete are enabled");
        d.key(Qt::Key_Escape);
        require(c.presets.builtinItems() == originals && c.presets.items().isEmpty() &&
                    !QFileInfo::exists(storePath(c)),
                "Built-in filter interactions created/changed persistent user presets");
        return true;
    }
    if (id == "UX-PRS-006") {
        HoverTrace trace(c);
        const auto before = c.provider->requestImage({}, nullptr, {});
        const auto previewFor = [&](const QString &presetID) {
            const auto preset = c.presets.snapshot(presetID);
            return pixels(c, luma::mergeSettings(document, preset.settings, preset.groups,
                                                 preset.parameters));
        };
        // First three rows fit even the documented minimum 100 px ListView height.
        const auto expectedA = previewFor("builtin:natural");
        const auto expectedB = previewFor("builtin:soft-portrait");
        const auto expectedC = previewFor("builtin:landscape");
        const auto presetC = c.presets.snapshot("builtin:landscape");
        const auto mergedC =
            luma::mergeSettings(document, presetC.settings, presetC.groups, presetC.parameters);
        require(expectedA != expectedB && expectedA != expectedC && expectedB != expectedC &&
                    expectedC != before,
                "Hover race fixture does not distinguish A/B/C pixels");
        d.log({{"action", "hover_transport_positive_control_begin"}});
        d.move("filter_landscape", {.5, .5});
        ready(c, mergedC);
        d.move("develop.preview", {.5, .5});
        ready(c, document);
        d.log({{"action", "hover_transport_positive_control_pass"}});
        // Ordinary, non-reentrant pointer moves reproduce cross-row ownership first.
        d.move("filter_natural", {.5, .5});
        d.move("filter_soft-portrait", {.5, .5});
        d.move("filter_landscape", {.5, .5});
        d.log({{"action", "direct_hover_sequence"},
               {"landscape_hovered", d.object("filter_landscape")->property("hovered").toBool()},
               {"preset_preview", c.editor.presetPreview()}});
        require(d.object("filter_landscape")->property("hovered").toBool(),
                "Direct hover transport did not reach Landscape");
        require(c.editor.presetPreview(),
                "Leaving an old preset cleared the newly hovered preset preview");
        ready(c, mergedC);
        d.move("develop.preview", {.5, .5});
        ready(c, document);
        bool acknowledgedC = false;
        bool staleAfterC = false;
        QObject observationLifetime;
        const auto observe =
            QObject::connect(&c.editor, &EditorController::stateChanged, &observationLifetime, [&] {
                const auto current = c.provider->requestImage({}, nullptr, {});
                if (acknowledgedC && current != expectedC)
                    staleAfterC = true;
                if (current == expectedC)
                    acknowledgedC = true;
            });
        {
            // Hold delivery of A's queued render result at EditorController, then
            // dispatch actual pointer transitions B and C before delivering A.
            QueuedCallGate gate(
                c.editor, [&] { return c.editor.presetPreview(); },
                [&] {
                    d.move("filter_soft-portrait", {.5, .5});
                    d.log({{"action", "hover_B_after_move"},
                           {"hovered",
                            d.object("filter_soft-portrait")->property("hovered").toBool()}});
                    d.move("filter_landscape", {.5, .5});
                    const auto physical = QCursor::pos();
                    const auto expectedPosition =
                        c.window->mapToGlobal(d.point("filter_landscape"));
                    d.log({{"action", "hover_C_after_move"},
                           {"hovered", d.object("filter_landscape")->property("hovered").toBool()},
                           {"physical_x", physical.x()},
                           {"physical_y", physical.y()},
                           {"expected_global_x", expectedPosition.x()},
                           {"expected_global_y", expectedPosition.y()}});
                    ready(c, mergedC);
                });
            d.move("filter_natural", {.5, .5});
            d.wait([&] { return gate.caught; }, "A hover queued callback intercepted");
            if (gate.failure)
                std::rethrow_exception(gate.failure);
            d.frame();
            require(acknowledgedC && !staleAfterC &&
                        c.provider->requestImage({}, nullptr, {}) == expectedC,
                    "Stale A/B render replaced the acknowledged C hover");
        }
        ready(c, mergedC);
        QObject::disconnect(observe);
        d.move("develop.preview", {.5, .5});
        d.wait(
            [&] {
                return !c.editor.presetPreview() &&
                       c.provider->requestImage({}, nullptr, {}) == before;
            },
            "hover race pointer leave restored exact pre-hover pixels");
        ready(c, document);
        const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
        require(c.editor.currentSettings() == document && !c.editor.saving() && active &&
                    active->edits == document,
                "Rapid hover changed applied document or durable edits");
        return true;
    }
    if (id == "UX-PRS-009" || id == "UX-PRS-010") {
        const auto presetID = create(c, "Roundtrip 中文 🌅", 3, "JSON Հայերեն");
        const auto expected = c.presets.snapshot(presetID);
        const auto expectedBytes = luma::encodePreset(expected);
        const auto exported = c.root + "/exported-preset.json";
        select(c, presetID);
        chooserBoundary(c, "presetExportDialog", exported, presetID);
        require(c.presets.error().isEmpty() && bytes(exported) == expectedBytes,
                "Production JSON export changed the selected preset");
        const auto decoded = luma::decodePreset(bytes(exported));
        require(luma::encodePreset(decoded) == expectedBytes,
                "Exported preset does not roundtrip through its public schema");
        if (id == "UX-PRS-010") {
            const auto originalStore = bytes(storePath(c));
            const auto originalItems = c.presets.items();
            auto future = QJsonDocument::fromJson(expectedBytes).object();
            future["schemaVersion"] = 999;
            auto wrongType = QJsonDocument::fromJson(expectedBytes).object();
            wrongType["name"] = 42;
            auto invalidRange = QJsonDocument::fromJson(expectedBytes).object();
            auto settings = invalidRange["settings"].toObject();
            auto light = settings["Light"].toObject();
            light["exposure"] = 1e30;
            settings["Light"] = light;
            invalidRange["settings"] = settings;
            const QList<QByteArray> invalid{"{ invalid JSON", QJsonDocument(future).toJson(),
                                            QJsonDocument(wrongType).toJson(),
                                            QJsonDocument(invalidRange).toJson()};
            for (int variant = 0; variant < invalid.size(); ++variant) {
                const auto path = c.root + QString("/invalid-%1.json").arg(variant);
                fixtureFile(c, path, invalid[variant]);
                chooserBoundary(c, "presetImportDialog", path);
                require(!c.presets.error().isEmpty() && d.item("preset.error")->isVisible() &&
                            d.object("preset.error")->property("text").toString() ==
                                c.presets.error() &&
                            c.presets.items() == originalItems &&
                            bytes(storePath(c)) == originalStore &&
                            luma::encodePreset(c.presets.snapshot(presetID)) == expectedBytes,
                        "Invalid JSON failed without visible error or changed existing presets");
            }
            // The same UI boundary must remain usable after all refused files.
            chooserBoundary(c, "presetImportDialog", exported);
            require(c.presets.error().isEmpty() && c.presets.items().size() == 2,
                    "Valid JSON retry failed after invalid imports");
            checkStore(c, identities(c.presets.items()));
            return true;
        }
        remove(c, presetID);
        checkStore(c, {});
        chooserBoundary(c, "presetImportDialog", exported);
        const auto imported = c.presets.importedId();
        require(!imported.isEmpty() && imported != presetID && c.presets.items().size() == 1 &&
                    luma::encodePreset(c.presets.snapshot(imported)) == expectedBytes,
                "JSON import lost fields, reused the deleted UUID or duplicated a preset");
        c.reveal("develop.exposureSlider");
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.35, .5});
        const auto before = c.editor.currentSettings();
        select(c, imported);
        d.move("develop.preview", {.5, .5});
        const auto after =
            luma::mergeSettings(before, expected.settings, expected.groups, expected.parameters);
        require(c.editor.currentSettings() == after && after != before,
                "Reimported JSON did not apply the exported settings subset");
        ready(c, after);
        d.click("main.undoButton");
        require(c.editor.currentSettings() == before,
                "Reimported JSON application was not one Undo");
        checkStore(c, {imported});
        return true;
    }
    if (id == "UX-PRS-012") {
        // Public on-disk collection fixture, loaded through the actual Refresh UI.
        // Do not use 512 synchronous put() calls merely to prepare the fixture.
        QJsonArray entries;
        QString firstID, lastID;
        for (int index = 0; index < 512; ++index) {
            luma::DevelopPreset preset;
            preset.name = QString("Preset %1").arg(index, 4, 10, QChar('0'));
            preset.folder = "512 public presets";
            preset.groups = 1;
            preset.settings.exposure = -2 + static_cast<double>(index) * 4 / 511;
            const auto presetID = QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (index == 0)
                firstID = presetID;
            if (index == 511)
                lastID = presetID;
            entries.append(QJsonObject{
                {"id", presetID},
                {"preset", QJsonDocument::fromJson(luma::encodePreset(preset)).object()}});
        }
        const auto fixtureBytes = QJsonDocument(QJsonObject{{"format", "luma.preset-library"},
                                                            {"schemaVersion", 6},
                                                            {"presets", entries}})
                                      .toJson();
        require(fixtureBytes.size() <= 1'048'576,
                "512-preset fixture exceeds documented byte limit");
        fixtureFile(c, storePath(c), fixtureBytes);
        userTab(c);
        openAction(c, "preset.actions.refresh");
        d.wait(
            [&] {
                return c.presets.items().size() == 512 &&
                       d.object("presetList")->property("count").toInt() == 512;
            },
            "512 presets loaded through Refresh");
        require(c.presets.error().isEmpty(), c.presets.error());
        auto *list = d.item("presetList");
        d.wait([&] { return list->property("contentHeight").toDouble() > list->height(); },
               "512-preset fixture overflows the actual panel after layout");
        c.metrics.reset();
        QElapsedTimer elapsed;
        elapsed.start();
        int down = 0, up = 0;
        for (; down < 180 && !list->property("atYEnd").toBool(); ++down)
            d.wheel("presetList", -1200);
        d.wait(
            [&] { return list->property("atYEnd").toBool() && !list->property("moving").toBool(); },
            "wheel scroll reached last of 512 presets");
        d.wait([&] { return d.find("preset." + lastID) != nullptr; }, "last preset delegate ready");
        select(c, lastID);
        d.move("develop.preview", {.5, .5});
        require(c.editor.currentSettings().exposure == 2,
                "Clicking last visible preset applied a recycled row's settings");
        for (; up < 180 && !list->property("atYBeginning").toBool(); ++up)
            d.wheel("presetList", 1200);
        d.wait(
            [&] {
                return list->property("atYBeginning").toBool() &&
                       !list->property("moving").toBool();
            },
            "wheel scroll returned to first preset");
        d.wait([&] { return d.find("preset." + firstID) != nullptr; },
               "first preset delegate ready");
        select(c, firstID);
        d.move("develop.preview", {.5, .5});
        require(c.editor.currentSettings().exposure == -2 && c.presets.items().size() == 512 &&
                    bytes(storePath(c)) == fixtureBytes,
                "512-preset scrolling/application changed the collection");
        ready(c, c.editor.currentSettings());
        writeJson(c.root + "/measurements.json", {{"kind", "512_preset_panel"},
                                                  {"status", "MEASURE_ONLY"},
                                                  {"elapsed_ms", elapsed.elapsed()},
                                                  {"down_wheel_events", down},
                                                  {"up_wheel_events", up},
                                                  {"preset_count", 512},
                                                  {"trackpad_exercised", false}});
        return true;
    }
    if (id == "UX-PRS-017") {
        const auto source = c.editor.sourcePath();
        const auto sourceID = c.editor.photoID();
        copyLight(c);
        const auto copied = c.presets.copiedSettings().value();
        // Copy is a snapshot, not a live link to subsequent source edits.
        c.reveal("develop.exposureSlider");
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.35, .5});
        c.saved();
        const auto sourceAfterCopy = c.editor.currentSettings();
        require(sourceAfterCopy.exposure != copied.settings.exposure &&
                    c.presets.copiedSettings()->settings == copied.settings,
                "Editing A changed the copied settings snapshot");
        d.click(c.card(1, true));
        d.wait([&] { return c.editor.sourcePath() != source && !c.editor.busy(); },
               "photo B loaded after copying A");
        c.loaded();
        c.reveal("develop.saturationSlider");
        d.drag("develop.saturationSlider", d.sliderThumb("develop.saturationSlider"), {.35, .5});
        c.saved();
        const auto before = c.editor.currentSettings();
        require(before.saturation != copied.settings.saturation,
                "Copy/Paste fixture does not distinguish B's unselected Color group");
        const auto after =
            luma::mergeSettings(before, copied.settings, copied.groups, copied.parameters);
        d.click("preset.paste");
        require(c.editor.currentSettings() == after && after != before &&
                    after.saturation == before.saturation &&
                    after.exposure == copied.settings.exposure,
                "Paste crossed the Light subset, used newer A edits or changed B's Color");
        ready(c, after);
        d.click("main.undoButton");
        require(c.editor.currentSettings() == before, "Paste on B was not one complete Undo");
        d.click("main.redoButton");
        require(c.editor.currentSettings() == after,
                "Paste Redo on B did not restore the snapshot");
        c.saved();
        const luma::Catalog catalog(c.catalogFile.toStdString());
        const auto sourceAsset = catalog.assetByID(sourceID.toStdString());
        require(sourceAsset && sourceAsset->edits == sourceAfterCopy,
                "Pasting into B modified the saved source A");
        return true;
    }
    require(id == "UX-PRS-019", "Unknown remaining preset case");
    // 333 actual UI create -> rename -> delete triplets = 999 store mutations;
    // one final create makes exactly 1000. All user actions traverse QML handlers.
    userTab(c);
    c.metrics.reset();
    QElapsedTimer elapsed;
    elapsed.start();
    int operations = 0;
    for (int cycle = 0; cycle < 333; ++cycle) {
        const auto presetID = create(c, QString("Stress %1").arg(cycle), 1, "Stress");
        ++operations;
        const auto before = c.presets.snapshot(presetID);
        openEdit(c, presetID);
        d.type("presetName", QString("Renamed %1").arg(cycle));
        d.click("presetSave");
        editorClosed(c);
        ++operations;
        require(c.presets.snapshot(presetID).settings == before.settings,
                "Stress rename changed captured settings");
        remove(c, presetID);
        ++operations;
        checkStore(c, {});
        if (cycle % 10 == 0)
            d.log({{"action", "preset_stress_checkpoint"}, {"store_mutations", operations}});
    }
    const auto finalID = create(c, "Final stable entry", 1, "Stress");
    ++operations;
    checkStore(c, {finalID});
    require(operations == 1000 && c.editor.currentSettings() == document,
            "Preset stress missed operations or changed the active document");
    writeJson(c.root + "/measurements.json", {{"kind", "preset_store_ui_stress"},
                                              {"store_mutations", operations},
                                              {"elapsed_ms", elapsed.elapsed()},
                                              {"final_entry_count", 1}});
    return true;
}

bool presetRemainingRestartScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-PRS-011" && id != "UX-PRS-018")
        return false;
    auto &d = *c.driver;
    if (stage == "prepare") {
        c.openFirst();
        editLightColor(c);
        if (id == "UX-PRS-011") {
            const auto presetID = create(c, "Before damaged store", 1);
            checkStore(c, {presetID});
            const auto intact = bytes(storePath(c));
            fixtureFile(c, c.stateRoot + "/intact-presets-evidence.json", intact);
            quit(c);
            fixtureFile(c, storePath(c), "{ damaged preset store \n");
            return true;
        }
        copyLight(c);
        const auto snapshot = c.presets.copiedSettings().value();
        fixtureFile(c, c.stateRoot + "/session-copy-evidence.json", luma::encodePreset(snapshot));
        require(c.presets.hasCopiedSettings() && d.item("preset.paste")->isEnabled(),
                "Prepare process has no Copy snapshot to lose on restart");
        quit(c);
        return true;
    }
    require(stage == "verify", "Unknown preset restart stage");
    if (id == "UX-PRS-011") {
        const QByteArray damaged("{ damaged preset store \n");
        require(bytes(storePath(c)) == damaged && c.presets.items().isEmpty() &&
                    !c.presets.error().isEmpty(),
                "Preset constructor silently replaced/accepted the corrupt store");
        c.editor.restore();
        c.loaded();
        userTab(c);
        require(d.item("preset.error")->isVisible() &&
                    d.object("preset.error")->property("text").toString() == c.presets.error(),
                "Startup corruption has no visible preset error");
        d.click("presetCreate");
        editorOpened(c);
        d.type("presetName", "Must not overwrite corruption");
        groups(c, 1);
        d.click("presetSave");
        require(d.object("presetEditor")->property("visible").toBool() &&
                    d.item("preset.editorError")->isVisible() && !c.presets.error().isEmpty() &&
                    bytes(storePath(c)) == damaged && c.presets.items().isEmpty(),
                "Saving over a corrupt startup store did not fail without changing bytes");
        d.key(Qt::Key_Escape);
        quit(c);
        require(bytes(storePath(c)) == damaged,
                "Clean shutdown silently rewrote the damaged preset store");
        return true;
    }
    const auto previous = luma::decodePreset(bytes(c.stateRoot + "/session-copy-evidence.json"));
    c.editor.restore();
    c.loaded();
    require(!c.presets.hasCopiedSettings() && !c.presets.copiedSettings() &&
                !d.item("preset.paste")->isEnabled() &&
                c.editor.currentSettings().exposure == previous.settings.exposure,
            "Copy snapshot crossed process restart or saved Exposure was not restored");
    // Documented session-local Copy starts working again after an explicit Copy.
    copyLight(c);
    quit(c);
    return true;
}
} // namespace ux
