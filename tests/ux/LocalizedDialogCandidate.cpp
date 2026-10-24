#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStyleHints>
#include <array>
#include <stdexcept>
#include <tuple>

// A finite dialog slice, not full VIS007/VIS008 or native chooser qualification.
namespace ux {
namespace {
constexpr auto candidateID = "CANDIDATE-LOCALIZED-PRESET-DIALOGS";
struct KeyboardNavigation {
    const Qt::TabFocusBehavior previous = QGuiApplication::styleHints()->tabFocusBehavior();
    KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    }
    ~KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(previous);
    }
};
bool sameMetadata(const luma::PhotoMetadata &a, const luma::PhotoMetadata &b) {
    return std::tie(a.camera, a.lens, a.iso, a.shutterSeconds, a.aperture, a.focalLength,
                    a.captureTimestamp, a.width, a.height, a.orientation) ==
           std::tie(b.camera, b.lens, b.iso, b.shutterSeconds, b.aperture, b.focalLength,
                    b.captureTimestamp, b.width, b.height, b.orientation);
}
luma::AssetSnapshot snapshot(Context &c, const QString &id) {
    const auto value = luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(id.toStdString());
    if (!value)
        throw std::runtime_error("Dialog fixture asset disappeared from its catalog");
    return *value;
}
QString digest(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read dialog guard file: " + path);
    return QString::fromLatin1(
        QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}
QRectF bounds(QQuickItem *item) {
    return item->mapRectToScene(QRectF(QPointF{}, item->size()));
}
bool fullyVisible(Context &c, QQuickItem *item) {
    if (!item || item->window() != c.window || item->width() <= 0 || item->height() <= 0)
        return false;
    const auto rect = bounds(item);
    if (!QRectF(QPointF{}, QSizeF(c.window->size())).adjusted(-.5, -.5, .5, .5).contains(rect))
        return false;
    auto *dialog = c.driver->find("presetEditor");
    auto *body =
        dialog
            ? qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(dialog->property("contentItem")))
            : nullptr;
    for (auto *parent = item; parent; parent = parent->parentItem()) {
        if (!parent->isVisible() || parent->opacity() <= 0)
            return false;
        if (parent->clip() && !bounds(parent).adjusted(-.5, -.5, .5, .5).contains(rect))
            return false;
        // A non-clipped Column can legitimately paint beyond its allocation.
        // Reject actual overlap with the separately painted footer, rather than
        // infer clipping from the body's allocated rectangle alone.
        if (parent == body) {
            auto *footer =
                qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(dialog->property("footer")));
            if (footer && footer->isVisible()) {
                const auto overlap = rect.intersected(bounds(footer));
                if (overlap.width() > .5 && overlap.height() > .5)
                    return false;
            }
        }
    }
    return true;
}
void revealSidebar(Context &c, const QString &id) {
    auto *view = qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(
        c.driver->object("develop.presetsScroll")->property("contentItem")));
    require(view, "Preset sidebar has no actual scroll viewport");
    for (int step = 0; step < 24; ++step) {
        c.driver->wait([&] { return !view->property("moving").toBool(); },
                       "Preset sidebar scroll settled");
        c.driver->frame();
        auto *item = c.driver->item(id);
        if (fullyVisible(c, item))
            return;
        const auto rect = item->mapRectToItem(view, QRectF(QPointF{}, item->size()));
        c.driver->wheel("develop.presetsScroll", rect.top() < 0 ? 120 : -120);
    }
    require(false, "Preset action cannot be reached by sidebar scrolling: " + id);
}
QQuickItem *itemProperty(QObject *owner, const char *name) {
    return qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(owner->property(name)));
}
QString expected(const QString &code, const QString &key) {
    QFile file(":/luma/i18n/" + code + ".json");
    require(file.open(QIODevice::ReadOnly), "Missing dialog language resource");
    const auto messages = QJsonDocument::fromJson(file.readAll()).object()["messages"].toObject();
    require(messages.contains(key), "Missing explicit dialog translation key: " + key);
    return messages[key].toString();
}
void opened(Context &c, const QString &id) {
    c.driver->wait([&] { return c.driver->object(id)->property("opened").toBool(); },
                   "Dialog opened: " + id);
    c.driver->frame();
    auto *background = itemProperty(c.driver->object(id), "background");
    require(fullyVisible(c, background), "Dialog background extends outside minimum window: " + id);
}
void reveal(Context &c, const QString &id) {
    for (int step = 0; step < 24; ++step) {
        c.driver->frame();
        if (fullyVisible(c, c.driver->item(id)))
            return;
        auto *scroll = c.driver->find("preset.editorScroll");
        require(scroll, "Dialog control is clipped without a scroll route: " + id);
        auto *view = itemProperty(scroll, "contentItem");
        require(view, "Dialog ScrollView has no actual Flickable");
        c.driver->wait([&] { return !view->property("moving").toBool(); },
                       "Dialog scrolling settled");
        const auto rect =
            c.driver->item(id)->mapRectToItem(view, QRectF(QPointF{}, c.driver->item(id)->size()));
        require(rect.width() <= view->width() + .5,
                "Dialog control exceeds horizontal viewport: " + id);
        c.driver->wheel("preset.editorScroll", rect.top() < 0 ? 120 : -120);
    }
    require(false, "Actual wheel could not reveal dialog control: " + id);
}
void keyboardToggle(Context &c, const QString &id) {
    auto *control = c.driver->item(id);
    for (int step = 0; step < 48 && c.window->activeFocusItem() != control; ++step)
        c.driver->key(Qt::Key_Tab);
    require(c.window->activeFocusItem() == control,
            "Actual Tab traversal cannot reach dialog control: " + id);
    c.driver->wait([&] { return fullyVisible(c, control); },
                   "Focused dialog control revealed under actual clips");
    const bool before = control->property("checked").toBool();
    QSignalSpy clicks(control, SIGNAL(clicked()));
    c.driver->key(Qt::Key_Space);
    require(control->property("checked").toBool() != before && clicks.count() == 1,
            "Focused dialog control did not receive one Space activation");
    c.driver->key(Qt::Key_Space);
    require(control->property("checked").toBool() == before && clicks.count() == 2,
            "Focused dialog control did not restore its state on second Space");
}
QQuickItem *textNode(QQuickItem *root, const QString &text) {
    if (!root)
        return nullptr;
    if (root->inherits("QQuickText") && root->property("text").toString() == text)
        return root;
    for (auto *child : root->childItems())
        if (auto *found = textNode(child, text))
            return found;
    return nullptr;
}
void text(Context &c, QJsonArray &ledger, const QString &code, const QString &scene,
          const QString &id, const QString &key, bool control = true) {
    auto *owner = c.driver->item(id);
    const auto literal = expected(code, key);
    require(owner->property("text").toString() == literal,
            "Dialog logical translation differs from resource: " + id);
    auto *paint = control ? textNode(itemProperty(owner, "contentItem"), literal) : owner;
    require(paint && fullyVisible(c, owner) && fullyVisible(c, paint),
            "Dialog text/control is outside its visible allocation: " + id);
    const double width = paint->property("paintedWidth").toDouble();
    const double height = paint->property("paintedHeight").toDouble();
    require(width > 0 && height > 0 && width <= paint->width() + .5 &&
                height <= paint->height() + .5 && !paint->property("truncated").toBool(),
            "Localized dialog text is clipped or elided: " + code + " / " + scene + " / " + id);
    const auto box = bounds(paint);
    ledger.append(QJsonObject{{"language", code},
                              {"scene", scene},
                              {"id", id},
                              {"text", literal},
                              {"painted_width", width},
                              {"painted_height", height},
                              {"x", box.x()},
                              {"y", box.y()},
                              {"width", box.width()},
                              {"height", box.height()}});
}
} // namespace
bool localizedDialogCandidate(Context &c, const QString &id) {
    if (id != candidateID)
        return false;
    const KeyboardNavigation keyboardNavigation;
    auto &d = *c.driver;
    QJsonArray ledger;
    QJsonObject historyEvidence;
    auto saveLedger = [&] {
        QFile file(c.root + "/localized-dialogs.json");
        require(file.open(QIODevice::WriteOnly), "Cannot write localized dialog ledger");
        const auto bytes =
            QJsonDocument(
                QJsonObject{
                    {"official_qualified", false},
                    {"bindings", ledger},
                    {"history_positive_control", historyEvidence},
                    {"scope",
                     "Settings and preset Create/Edit/Copy dialogs; eight languages, 1000x640"}})
                .toJson();
        require(file.write(bytes) == bytes.size(), "Incomplete localized dialog ledger");
    };
    try {
        c.import(1); // Native chooser result is an explicit fixture seam.
        d.doubleClick(c.card(0));
        c.loaded();
        c.reveal("develop.exposureSlider");
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
        c.saved();
        luma::EditDocument edited;
        edited.exposure = c.editor.currentSettings().exposure;
        require(edited.exposure > 0 && c.editor.currentSettings() == edited && c.editor.canUndo() &&
                    !c.editor.canRedo(),
                "Dialog fixture needs a genuine non-neutral Exposure edit and undo history");
        d.click("preset.tabs.presets");
        d.click("presetCreate");
        opened(c, "presetEditor");
        d.type("presetName", "Dialog QA");
        d.click("presetSave");
        d.wait([&] { return c.presets.items().size() == 1; }, "One real fixture preset saved");
        const auto presetID = c.presets.items()[0].toMap()["id"].toString();
        d.wait([&] { return d.find("preset." + presetID); }, "Actual preset delegate");
        d.click("preset." + presetID);
        d.move("develop.preview", {.5, .5});
        c.saved();
        const auto document = c.editor.currentSettings();
        const auto source = c.editor.sourcePath();
        const auto sourceHash = digest(source);
        const auto presetHash = digest(c.stateRoot + "/presets.json");
        const auto selection = c.library.selectionIDs();
        const bool undo = c.editor.canUndo(), redo = c.editor.canRedo();
        const auto assetID = c.library.data(c.library.index(0), LibraryModel::AssetID).toString();
        const auto before = snapshot(c, assetID);
        require(document == edited && before.asset.edits == document,
                "Fixture preset changed the genuine prior edit");
        const auto expectedPreview = fixtureReference(c, document, source);
        d.wait([&] { return c.provider->requestImage({}, nullptr, {}) == expectedPreview; },
               "Edited dialog fixture has independently expected current pixels");
        const auto preview = c.provider->requestImage({}, nullptr, {});
        historyEvidence = {{"edited_exposure", edited.exposure}, {"completed", false}};
        d.resize({1000, 640}); // Geometry fixture, not native APP014 resize qualification.
        constexpr std::array codes{"en", "ru", "zh_CN", "hy", "de", "es", "it", "fr"};
        constexpr std::array groups{"Light",    "Color",       "Curve",         "HSL",
                                    "Detail",   "Crop",        "Optics",        "Geometry",
                                    "Presence", "Point Color", "Color Grading", "Effects"};
        for (int language = 0; language < 8; ++language) {
            const QString code = codes.at(static_cast<std::size_t>(language));
            d.click("settingsButton");
            opened(c, "preferencesPanel");
            d.choose("languageChoice", language);
            d.wait([&] { return c.localization->language() == code; }, "Dialog language activated");
            d.frame();
            text(c, ledger, code, "settings", "settingsLanguageLabel", "Interface language", false);
            text(c, ledger, code, "settings", "settingsEffect", "Changes take effect immediately.",
                 false);
            d.screenshot(c.root + "/dialog-" + code + "-settings.png");
            d.key(Qt::Key_Escape);
            d.wait([&] { return !d.object("preferencesPanel")->property("visible").toBool(); },
                   "Settings closed");
            for (const auto &mode : {"create", "edit", "copy"}) {
                const QString scene = mode;
                if (scene == "edit") {
                    revealSidebar(c, "preset.actions");
                    d.click("preset.actions");
                    d.wait(
                        [&] {
                            return d.object("preset.actions.menu")->property("opened").toBool();
                        },
                        "Preset actions opened");
                    d.click("preset.actions.edit");
                } else {
                    const auto action = scene == "copy" ? "preset.copy" : "presetCreate";
                    revealSidebar(c, action);
                    d.click(action);
                }
                opened(c, "presetEditor");
                require(d.object("presetEditor")->property("title").toString() ==
                            expected(code, scene == "copy"   ? "Copy settings"
                                           : scene == "edit" ? "Edit preset"
                                                             : "Save preset"),
                        "Preset dialog title has wrong translation/mode");
                keyboardToggle(c, "presetGroup7");
                require(d.object("presetEditor")->property("groups").toUInt() ==
                            luma::allSettingsGroups,
                        "Keyboard group round trip did not preserve all preset groups");
                if (scene == "edit")
                    keyboardToggle(c, "preset.capture");
                for (std::size_t group = 0; group < groups.size(); ++group) {
                    const auto control = "presetGroup" + QString::number(group);
                    reveal(c, control);
                    // HSL is a deliberately untranslated technical identifier.
                    if (group == 3)
                        require(d.object(control)->property("text").toString() == "HSL",
                                "HSL identifier changed");
                    else
                        text(c, ledger, code, scene, control,
                             groups.at(static_cast<std::size_t>(group)));
                }
                if (scene == "edit") {
                    reveal(c, "preset.capture");
                    text(c, ledger, code, scene, "preset.capture",
                         "Replace settings with current edits");
                }
                if (scene != "copy") {
                    reveal(c, "preset.editorHelp");
                    text(c, ledger, code, scene, "preset.editorHelp",
                         "Renaming or moving preserves settings. Replace with current edits to add "
                         "new groups.",
                         false);
                }
                text(c, ledger, code, scene, "preset.cancel", "Cancel");
                text(c, ledger, code, scene, "presetSave", scene == "copy" ? "Copy" : "Save");
                d.screenshot(c.root + "/dialog-" + code + "-" + scene + ".png");
                d.click("preset.cancel");
                d.wait([&] { return !d.object("presetEditor")->property("visible").toBool(); },
                       "Preset dialog cancelled through actual button");
            }
            const auto after = snapshot(c, assetID);
            require(
                c.editor.currentSettings() == document && c.editor.sourcePath() == source &&
                    c.editor.canUndo() == undo && c.editor.canRedo() == redo &&
                    c.library.selectionIDs() == selection && digest(source) == sourceHash &&
                    digest(c.stateRoot + "/presets.json") == presetHash &&
                    after.asset.edits == before.asset.edits && after.revision == before.revision &&
                    after.asset.id == before.asset.id && after.asset.path == before.asset.path &&
                    after.asset.checksum == before.asset.checksum &&
                    sameMetadata(after.asset.metadata, before.asset.metadata) &&
                    !c.editor.presetPreview() && !c.presets.hasCopiedSettings() &&
                    c.provider->requestImage({}, nullptr, {}) == preview,
                "Cancelled translated dialogs mutated source, preset, selection or edit history");
            c.checkData();
            saveLedger();
        }
        d.click("main.undoButton");
        c.saved();
        require(c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo() &&
                    c.editor.canRedo() && snapshot(c, assetID).asset.edits == luma::EditDocument{},
                "Post-dialog real Undo did not restore the complete neutral document");
        d.click("main.redoButton");
        c.saved();
        require(c.editor.currentSettings() == document && c.editor.canUndo() &&
                    !c.editor.canRedo() && snapshot(c, assetID).asset.edits == document &&
                    digest(source) == sourceHash &&
                    digest(c.stateRoot + "/presets.json") == presetHash &&
                    c.library.selectionIDs() == selection,
                "Post-dialog real Redo did not restore the full original edit");
        d.wait([&] { return c.provider->requestImage({}, nullptr, {}) == preview; },
               "Post-dialog Redo restores exact edited preview pixels");
        historyEvidence["completed"] = true;
        saveLedger();
        c.checkData();
        return true;
    } catch (...) {
        saveLedger();
        throw;
    }
}
} // namespace ux
