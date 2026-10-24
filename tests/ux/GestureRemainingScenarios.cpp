#include "apps/shared/PresetStore.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QPointer>
#include <QSet>
#include <QStyleHints>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <optional>
#include <vector>

namespace ux {
namespace {
using Document = luma::EditDocument;
constexpr auto scrollID = "develop.adjustmentsScroll";
constexpr auto deepID = "noise.colorSmoothnessSlider";
struct KeyboardNavigation {
    Qt::TabFocusBehavior previous = QGuiApplication::styleHints()->tabFocusBehavior();
    KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    }
    ~KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(previous);
    }
};
void exact(Context &c, const Document &expected, const QString &reason) {
    require(c.editor.currentSettings() == expected && c.editor.error().isEmpty(), reason);
}
QObject *flickable(Context &c) {
    auto *result = qvariant_cast<QObject *>(c.driver->object(scrollID)->property("contentItem"));
    require(result, "Adjustments ScrollView has no Flickable");
    return result;
}
void settled(Context &c) {
    c.driver->wait([&] { return !flickable(c)->property("moving").toBool(); },
                   "Adjustments Flickable stopped moving", 5000);
    c.driver->frame();
}
struct ScrollState {
    double y, content, viewport;
};
ScrollState scrollState(Context &c) {
    auto *f = flickable(c);
    return {f->property("contentY").toDouble(), f->property("contentHeight").toDouble(),
            f->property("height").toDouble()};
}
void validRange(Context &c) {
    const auto s = scrollState(c);
    require(std::isfinite(s.y) && std::isfinite(s.content) && std::isfinite(s.viewport) &&
                s.viewport > 0 && s.content > s.viewport && s.y >= -1 &&
                s.y <= std::max(0., s.content - s.viewport) + 1,
            "Relayout left nonfinite/out-of-range deep ScrollView coordinates");
}
void visibleControl(Context &c, const QString &id) {
    auto *target = c.driver->item(id);
    auto *viewport = qobject_cast<QQuickItem *>(flickable(c));
    require(viewport, "ScrollView contentItem is not a QQuickItem");
    const auto rect = target->mapRectToItem(viewport, target->boundingRect());
    // One logical pixel allows layout/raster rounding, not a UX jump budget.
    require(target->isVisible() && target->isEnabled() && rect.width() > 0 && rect.height() > 0 &&
                rect.left() >= -1 && rect.top() >= -1 && rect.right() <= viewport->width() + 1 &&
                rect.bottom() <= viewport->height() + 1,
            "Adjustment control is clipped or unreachable: " + id);
}
void deep(Context &c, const QString &id = deepID) {
    c.reveal(id); // Real ScrollBar drag in existing Context helper.
    settled(c);
    validRange(c);
    visibleControl(c, id);
    const auto s = scrollState(c);
    require(s.y > s.viewport, "Deep-scroll fixture did not pass one full viewport");
}
QImage pixels(Context &c, const Document &document,
              luma::DetailDiagnostic diagnostic = luma::DetailDiagnostic::none) {
    return fixtureReference(c, document, c.editor.sourcePath(), diagnostic);
}
void showPixels(Context &c, const QImage &expected, const QString &reason) {
    c.driver->wait(
        [&] {
            auto *image = c.driver->item("develop.preview");
            return !c.editor.busy() && image->isVisible() &&
                   image->property("status").toInt() == 1 &&
                   image->property("source").toUrl().toString() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == expected;
        },
        reason);
    c.driver->frame();
}
void rendered(Context &c, const Document &expected) {
    const auto image = pixels(c, expected);
    showPixels(c, image, "Preview Ready/current source/exact CPU pixels");
    exact(c, expected, "Rendering changed the full expected document");
}
void durable(Context &c, const QString &photo, const Document &expected) {
    // This helper is intentionally used only AFTER pointer-release checks. Context::saved
    // invokes endEdit and must never repair the gesture being tested.
    c.saved();
    const auto asset = luma::Catalog(c.catalogFile.toStdString()).assetByID(photo.toStdString());
    require(asset && asset->edits == expected, "Full document was not saved for expected asset");
}
void savedWithoutEnd(Context &c, const QString &photo, const Document &expected) {
    c.driver->wait([&] { return !c.editor.saving() && !c.editor.busy(); },
                   "Real gesture autosave completed without fixture endEdit");
    const auto asset = luma::Catalog(c.catalogFile.toStdString()).assetByID(photo.toStdString());
    require(asset && asset->edits == expected && c.editor.error().isEmpty(),
            "Real gesture completion did not durably save its exact state");
}
void undoRedo(Context &c, const Document &before, const Document &after, bool emptyBefore = false) {
    auto &d = *c.driver;
    d.click("main.undoButton");
    exact(c, before, "One UI Undo did not restore the full pre-gesture document");
    if (emptyBefore)
        require(!c.editor.canUndo(), "One gesture produced more than one Undo entry");
    rendered(c, before);
    d.click("main.redoButton");
    exact(c, after, "One UI Redo did not restore the full post-gesture document");
    rendered(c, after);
}
Document dragOnly(Context &c, const QString &id, const char *parameter, QPointF target) {
    c.reveal(id);
    visibleControl(c, id);
    const auto before = c.editor.currentSettings();
    c.driver->drag(id, c.driver->sliderThumb(id), target);
    const auto after = c.editor.currentSettings();
    auto expected = before;
    expected.setParameter(parameter, after.parameter(parameter));
    require(after != before && after.parameter(parameter) != before.parameter(parameter),
            "Positive-control drag did not change the intended parameter");
    exact(c, expected, "Slider input changed unrelated document fields");
    return expected;
}
void focusByTab(Context &c, const QString &id, bool verifyScrollVisibility = false) {
    auto &d = *c.driver;
    auto *target = d.item(id);
    const auto document = c.editor.currentSettings();
    auto *viewport = qobject_cast<QQuickItem *>(flickable(c));
    // Use actual navigation, as in the already-qualified HistoryStress helper.
    // Public nextItemInFocusChain order is not an exact oracle for real Tab traversal.
    for (int step = 0; step < 240 && !target->hasActiveFocus(); ++step) {
        d.key(Qt::Key_Tab);
        exact(c, document, "Tab navigation unexpectedly edited the photo");
        auto *actual = c.window->activeFocusItem();
        d.log(
            {{"action", "actual_tab_focus"},
             {"step", step},
             {"name", actual ? actual->objectName() : QString{}},
             {"class", actual ? QString::fromUtf8(actual->metaObject()->className()) : QString{}}});
        if (verifyScrollVisibility && actual && viewport && viewport->isAncestorOf(actual)) {
            settled(c);
            if (!actual->objectName().isEmpty())
                visibleControl(c, actual->objectName());
            else {
                const auto rect = actual->mapRectToItem(viewport, actual->boundingRect());
                require(actual->isVisible() && rect.top() >= -1 &&
                            rect.bottom() <= viewport->height() + 1,
                        "Unnamed focused adjustment was outside the visible scroll viewport");
            }
        }
    }
    require(target->hasActiveFocus(), "Actual Tab input did not focus its target");
}
QString photoAt(Context &c, int row) {
    c.card(row, true);
    return c.library.data(c.library.index(row), LibraryModel::AssetID).toString();
}
void activate(Context &c, int row, const Document &expected) {
    const auto next = photoAt(c, row);
    c.driver->click(c.card(row, true));
    c.driver->wait([&] { return c.editor.photoID() == next && !c.editor.busy(); },
                   "Filmstrip activated the exact requested asset");
    c.loaded();
    exact(c, expected, "Photo activation showed another asset's document");
    require(!c.editor.canUndo() && !c.editor.canRedo(), "History crossed an asset boundary");
}
void detailControls(Context &c, const Document &document) {
    static const QStringList names{
        "sharpen.amount",    "sharpen.radius",        "sharpen.detail",          "sharpen.masking",
        "noise.luminance",   "noise.luminanceDetail", "noise.luminanceContrast", "noise.color",
        "noise.colorDetail", "noise.colorSmoothness"};
    for (const auto &name : names) {
        auto *slider = c.driver->object(name + "Slider");
        require(slider->property("value").toDouble() == document.parameter(name.toStdString()),
                "Detail thumb retained another asset's state: " + name);
        auto *label = c.driver->object("develop." + name + ".value");
        const auto number = QString::number(document.parameter(name.toStdString()), 'f',
                                            slider->property("stepSize").toDouble() < 1 ? 2 : 0);
        require(label->property("text").toString() == number,
                "Detail numeric label retained another asset's state: " + name);
    }
}
void scrollHeight(Context &c) {
    const KeyboardNavigation keys;
    c.openFirst();
    const auto before = c.editor.currentSettings();
    require(!before.customWhiteBalance, "Dynamic-height fixture must start As Shot");
    c.reveal("develop.whiteBalance.preset");
    focusByTab(c, "develop.whiteBalance.preset");
    deep(c);
    require(c.driver->item("develop.whiteBalance.preset")->hasActiveFocus(),
            "Wheel/ScrollBar drag unexpectedly took keyboard focus from WB ComboBox");
    const auto old = scrollState(c);
    c.driver->key(Qt::Key_Down); // Real closed ComboBox activation: As Shot -> Daylight.
    auto expected = before;
    expected.temperature = 5500;
    expected.tint = 0;
    expected.customWhiteBalance = true;
    exact(c, expected, "WB input did not select exactly the Daylight semantics");
    settled(c);
    const auto changed = scrollState(c);
    require(changed.content < old.content,
            "Positive control failed: As Shot explanation did not change panel height");
    const auto clamped = std::clamp(old.y, 0., std::max(0., changed.content - changed.viewport));
    require(std::abs(changed.y - clamped) <= 1,
            "Dynamic height relayout unexpectedly moved the settled scroll offset");
    validRange(c);
    visibleControl(c, deepID);
    rendered(c, expected);
    const auto daylight = expected;
    // Preserve the same deep viewport; reverse the actual input as a second layout control.
    c.driver->key(Qt::Key_Home);         // QQuickComboBox activates the first item directly.
    expected.customWhiteBalance = false; // As Shot retains Kelvin and resets tint only.
    exact(c, expected, "As Shot reversal discarded retained Kelvin or unrelated settings");
    settled(c);
    require(scrollState(c).content > changed.content, "Reverse relayout positive control failed");
    validRange(c);
    visibleControl(c, deepID);
    c.driver->click("main.undoButton");
    exact(c, daylight, "Relayout As Shot action was not exactly one Undo");
    c.driver->click("main.undoButton");
    exact(c, before, "Dynamic-height input created extra history or lost initial As Shot");
    require(!c.editor.canUndo(), "Two real WB choices produced extra Undo entries");
    c.driver->click("main.redoButton");
    exact(c, daylight, "First WB relayout Redo did not restore Daylight");
    c.driver->click("main.redoButton");
    exact(c, expected, "Second WB relayout Redo did not restore final As Shot");
    durable(c, c.editor.photoID(), expected);
    rendered(c, expected);
}
// Bounded passive photo-switch/layout evidence for UX-DSC-006. No event pumping or input.
class ScrollPhotoTrace {
  public:
    ScrollPhotoTrace(Context &context, double baseline)
        : c(context), baselineY(baseline), viewport(qobject_cast<QQuickItem *>(flickable(c))),
          filmstrip(qobject_cast<QQuickItem *>(c.driver->find("develop.filmstrip"))),
          control(qobject_cast<QQuickItem *>(c.driver->find(deepID))) {
        elapsed.start();
        require(viewport && filmstrip && control && viewport->height() > 0,
                "Photo-switch trace requires the actual visible Develop geometry");
        baselineViewport = viewport->height();
        QObject::connect(&c.library, &LibraryModel::reloadStarted, &observer,
                         [this](bool preserve) {
                             record(preserve ? "reloadStarted.preserve" : "reloadStarted.clear");
                         });
        QObject::connect(&c.library, &LibraryModel::stateChanged, &observer,
                         [this] { record("library.stateChanged"); });
        QObject::connect(&c.library, &QAbstractItemModel::modelAboutToBeReset, &observer,
                         [this] { record("library.modelAboutToBeReset"); });
        QObject::connect(&c.library, &QAbstractItemModel::modelReset, &observer,
                         [this] { record("library.modelReset"); });
        QObject::connect(&c.editor, &EditorController::stateChanged, &observer,
                         [this] { record("editor.stateChanged"); });
        QObject::connect(&c.editor, &EditorController::photoLoaded, &observer,
                         [this] { record("editor.photoLoaded"); });
        QObject::connect(c.window, &QQuickWindow::afterAnimating, &observer,
                         [this] { record("window.afterAnimating"); });
        QObject::connect(c.window, &QQuickWindow::activeFocusItemChanged, &observer,
                         [this] { record("window.activeFocusItemChanged"); });
        if (viewport)
            QObject::connect(viewport.data(), &QQuickItem::heightChanged, &observer,
                             [this] { record("viewport.heightChanged"); });
        if (filmstrip) {
            QObject::connect(filmstrip.data(), &QQuickItem::heightChanged, &observer,
                             [this] { record("filmstrip.heightChanged"); });
            QObject::connect(filmstrip.data(), &QQuickItem::visibleChanged, &observer,
                             [this] { record("filmstrip.visibleChanged"); });
        }
        record("installed.beforeAtoB");
    }
    void mark(const QString &value) {
        phase = value;
        record("checkpoint");
    }
    void save(const QString &reason) const {
        writeJson(c.root + "/dsc006-scroll-trace.json",
                  {{"scope", "passive DSC006 A-to-B/B-to-A layout/focus trace"},
                   {"reason", reason},
                   {"baseline_y", baselineY},
                   {"baseline_viewport_height", baselineViewport},
                   {"dropped_events", dropped},
                   {"events", events},
                   {"current", snapshot("write")}});
    }
    void verifyPhotoSwitchGeometry(const QString &expectedPhase, const QString &from,
                                   const QString &to) const {
        bool observedPrevious = false, observedBusy = false, observedLoaded = false;
        for (const auto &value : events) {
            const auto event = value.toObject();
            if (event["phase"].toString() != expectedPhase)
                continue;
            observedPrevious |= event["photo_id"].toString() == from;
            observedBusy |= event["editor_busy"].toBool();
            observedLoaded |= event["event"].toString() == "editor.photoLoaded" &&
                              event["photo_id"].toString() == to;
            const auto scroll = event["scroll"].toObject();
            require(!event["library_mode"].toBool() &&
                        event["filmstrip"].toObject()["visible"].toBool() &&
                        std::abs(scroll["height"].toDouble() - baselineViewport) <= 1 &&
                        std::abs(scroll["contentY"].toDouble() - baselineY) <= 1,
                    "Photo switch hid the filmstrip or moved the adjustments viewport: " +
                        expectedPhase);
        }
        require(from != to && observedPrevious && observedBusy && observedLoaded && dropped == 0,
                "Photo-switch regression missed the previous/busy/loaded geometry: " +
                    expectedPhase);
    }

  private:
    static QJsonObject rect(const QRectF &r) {
        return {{"x", r.x()}, {"y", r.y()}, {"width", r.width()}, {"height", r.height()}};
    }
    static QJsonObject item(QQuickItem *value) {
        if (!value)
            return {{"present", false}};
        return {{"present", true},
                {"object_name", value->objectName()},
                {"class_name", QString::fromLatin1(value->metaObject()->className())},
                {"identity", QString::number(reinterpret_cast<quintptr>(value), 16)},
                {"visible", value->isVisible()},
                {"enabled", value->isEnabled()},
                {"active_focus", value->hasActiveFocus()},
                {"scene_rect", rect(value->mapRectToScene(value->boundingRect()))}};
    }
    QJsonObject snapshot(const QString &event) const {
        QJsonObject scroll = item(viewport.data());
        if (viewport) {
            for (const auto *name : {"contentY", "contentHeight", "height", "originY", "moving"})
                scroll.insert(QString::fromLatin1(name),
                              QJsonValue::fromVariant(viewport->property(name)));
        }
        QJsonObject deepControl = item(control.data());
        if (control && viewport)
            deepControl.insert("viewport_rect", rect(control->mapRectToItem(
                                                    viewport.data(), control->boundingRect())));
        return {{"elapsed_ns", elapsed.nsecsElapsed()},
                {"event", event},
                {"phase", phase},
                {"scroll", scroll},
                {"filmstrip", item(filmstrip.data())},
                {"deep_control", deepControl},
                {"library_count", c.library.count()},
                {"library_loading", c.library.loading()},
                {"library_working", c.library.working()},
                {"photo_id", c.editor.photoID()},
                {"editor_busy", c.editor.busy()},
                {"editor_saving", c.editor.saving()},
                {"editor_has_photo", c.editor.hasPhoto()},
                {"library_mode", c.window->property("libraryMode").toBool()},
                {"active_focus", item(c.window->activeFocusItem())}};
    }
    void record(const QString &event) {
        if (events.size() < 256)
            events.append(snapshot(event));
        else
            ++dropped;
    }

    Context &c;
    double baselineY;
    double baselineViewport = 0;
    QPointer<QQuickItem> viewport, filmstrip, control;
    QElapsedTimer elapsed;
    QJsonArray events;
    QString phase = "A-to-B";
    int dropped = 0;
    // Destroyed first: all callbacks disconnect before captured storage is destroyed.
    QObject observer;
};
void scrollPhoto(Context &c) {
    c.openFirst();
    const auto a = c.editor.photoID();
    const auto defaults = c.editor.currentSettings();
    activate(c, 1, defaults);
    const auto b = c.editor.photoID();
    auto bDoc = dragOnly(c, "sharpen.amountSlider", "sharpen.amount", {.72, .5});
    bDoc = dragOnly(c, "noise.colorSlider", "noise.color", {.42, .5});
    bDoc = dragOnly(c, deepID, "noise.colorSmoothness", {.76, .5});
    durable(c, b, bDoc);
    activate(c, 0, defaults);
    auto aDoc = dragOnly(c, "sharpen.amountSlider", "sharpen.amount", {.25, .5});
    aDoc = dragOnly(c, deepID, "noise.colorSmoothness", {.23, .5});
    durable(c, a, aDoc);
    require(aDoc != bDoc && aDoc.sharpenAmount != bDoc.sharpenAmount &&
                aDoc.noiseColorSmoothness != bDoc.noiseColorSmoothness,
            "Deep-scroll A/B fixture did not create distinguishable controls");
    deep(c);
    const auto y = scrollState(c).y;
    ScrollPhotoTrace trace(c, y);
    try {
        activate(c, 1, bDoc);
        // No reveal after activation: checking the retained deep viewport is the contract.
        settled(c);
        trace.mark("before.original.strict.offset.assert");
        trace.save("before-original-strict-offset-assert");
        trace.verifyPhotoSwitchGeometry("A-to-B", a, b);
        require(scrollState(c).y > scrollState(c).viewport && std::abs(scrollState(c).y - y) <= 1,
                "Photo switch unexpectedly abandoned its deep adjustments viewport");
        validRange(c);
        visibleControl(c, deepID);
        detailControls(c, bDoc);
        rendered(c, bDoc);
        durable(c, b, bDoc);
        trace.mark("B-to-A");
        activate(c, 0, aDoc);
        settled(c);
        require(scrollState(c).y > scrollState(c).viewport && std::abs(scrollState(c).y - y) <= 1,
                "Reverse photo switch unexpectedly abandoned its deep adjustments viewport");
        validRange(c);
        visibleControl(c, deepID);
        detailControls(c, aDoc);
        rendered(c, aDoc);
        durable(c, a, aDoc);
        trace.verifyPhotoSwitchGeometry("B-to-A", b, a);
        trace.mark("completed");
        trace.save("completed");
    } catch (...) {
        const auto failure = std::current_exception();
        try {
            trace.mark("failure");
            trace.save("original-scenario-failure");
        } catch (const std::exception &error) {
            std::fprintf(stderr, "DSC006 diagnostic write failed: %s\n", error.what());
        } catch (...) {
            std::fputs("DSC006 diagnostic write failed\n", stderr);
        }
        std::rethrow_exception(failure);
    }
}
void languageDeep(Context &c) {
    c.openFirst();
    const auto doc = dragOnly(c, "noise.colorSlider", "noise.color", {.35, .5});
    durable(c, c.editor.photoID(), doc);
    deep(c);
    const auto before =
        luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(c.editor.photoID().toStdString());
    require(before.has_value(), "Language fixture has no persisted snapshot");
    const auto historyUndo = c.editor.canUndo(), historyRedo = c.editor.canRedo();
    QSet<QString> captions;
    const auto languages = c.localization->languages();
    require(languages.size() == 8, "Current language fixture no longer has eight supported packs");
    for (int index = 0; index < languages.size(); ++index) {
        const auto code = languages[index].toMap()["code"].toString();
        c.driver->click("settingsButton");
        c.driver->wait(
            [&] { return c.driver->object("preferencesPanel")->property("opened").toBool(); },
            "Language popup opened while adjustments remain deep-scrolled");
        c.driver->choose("languageChoice", index);
        require(c.localization->language() == code && c.localization->error().isEmpty(),
                "Actual language selection did not activate its supported pack");
        c.driver->key(Qt::Key_Escape);
        c.driver->wait(
            [&] { return !c.driver->object("preferencesPanel")->property("visible").toBool(); },
            "Language popup fully closed");
        settled(c);
        validRange(c);
        require(scrollState(c).y > scrollState(c).viewport,
                "Language relayout reset the deep scroll position to the top");
        // Relayout may change which exact control occupies the viewport; prove reachability
        // using actual ScrollBar input AFTER checking the unmodified range/deep position.
        c.reveal(deepID);
        visibleControl(c, deepID);
        captions.insert(c.driver->object("detail.heading")->property("text").toString());
        exact(c, doc, "Language relayout changed edits");
        require(c.editor.canUndo() == historyUndo && c.editor.canRedo() == historyRedo &&
                    !c.editor.saving(),
                "Language relayout changed history or triggered autosave");
        rendered(c, doc);
    }
    require(captions.size() > 1, "Positive control failed: deep Detail label never translated");
    const auto after =
        luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(c.editor.photoID().toStdString());
    require(after && after->revision == before->revision && after->asset.edits == doc,
            "Display-only language change wrote an edit revision");
    undoRedo(c, Document{}, doc, true);
    durable(c, c.editor.photoID(), doc);
}
void keyboardReachability(Context &c) {
    const KeyboardNavigation keys;
    c.openFirst();
    dragOnly(c, "noise.colorSlider", "noise.color", {.4, .5});
    c.reveal("sharpen.amountSlider");
    focusByTab(c, "sharpen.amountSlider");
    visibleControl(c, "sharpen.amountSlider");
    const auto before = c.editor.currentSettings();
    // Deliberately no Context::reveal between Tabs or after reaching the target.
    // Automatic ScrollView/focus behavior itself must keep each adjustment reachable.
    focusByTab(c, deepID, true);
    visibleControl(c, deepID);
    auto expected = before;
    expected.noiseColorSmoothness += 1;
    c.driver->key(Qt::Key_Right);
    exact(c, expected, "Focused visible Slider did not handle the positive-control arrow input");
    require(c.driver->item(deepID)->hasActiveFocus(), "Arrow input lost its visible focus");
    visibleControl(c, deepID);
    rendered(c, expected);
    undoRedo(c, before, expected);
    durable(c, c.editor.photoID(), expected);
}
void openSelectionBoundaryWhileHeld(Context &c, const QString &path) {
    auto &d = *c.driver;
    d.key(Qt::Key_O, Qt::ControlModifier);
    auto *dialog = d.object("main.openDialog");
    d.wait([&] { return dialog->property("visible").toBool(); },
           "Actual Open keyboard shortcut opened FileDialog with pointer held");
    require(dialog->setProperty("selectedFile", QUrl::fromLocalFile(path)) &&
                dialog->property("selectedFile").toUrl().toLocalFile() == path,
            "FileDialog fixture rejected selected public image");
    d.log({{"action", "file_dialog_selection_fixture_boundary"},
           {"id", "main.openDialog"},
           {"path", path},
           {"native_selection_exercised", false},
           {"left_button_held", true}});
    // Close the actual panel, then run its actual accepted handler. Closing does not
    // release the test's Left button; the matched release is sent only after B loads.
    require(QMetaObject::invokeMethod(dialog, "close", Qt::DirectConnection),
            "FileDialog lacks close method");
    require(QMetaObject::invokeMethod(dialog, "accepted", Qt::DirectConnection),
            "FileDialog lacks production accepted handler");
}
void heldPhoto(Context &c) {
    c.openFirst();
    const auto a = c.editor.photoID(), sourceA = c.editor.sourcePath();
    const auto before = c.editor.currentSettings();
    const auto b = photoAt(c, 1);
    const auto bAsset = luma::Catalog(c.catalogFile.toStdString()).assetByID(b.toStdString());
    require(bAsset && bAsset->edits == Document{}, "Second asset is not independently pristine");
    const auto sourceB = QString::fromStdString(bAsset->path.string());
    c.reveal("develop.exposureSlider");
    c.driver->press("develop.exposureSlider", c.driver->sliderThumb("develop.exposureSlider"));
    c.driver->move("develop.exposureSlider", {.65, .5});
    const auto partial = c.editor.currentSettings();
    auto expectedA = before;
    expectedA.exposure = partial.exposure;
    require(partial != before &&
                c.driver->object("develop.exposureSlider")->property("pressed").toBool(),
            "Photo-switch fixture never held a real changed Slider gesture");
    exact(c, expectedA, "Held exposure changed another field before photo switch");
    openSelectionBoundaryWhileHeld(c, sourceB);
    c.driver->wait([&] { return c.editor.photoID() == b && !c.editor.busy(); },
                   "B loaded before physical Slider release");
    c.loaded();
    exact(c, Document{}, "Mid-drag switch leaked A edits into B before pointer continuation");
    require(!c.editor.canUndo() && !c.editor.canRedo(), "A history crossed into B");
    // Continue the same input stream after transition; this must not edit B.
    c.driver->move("develop.exposureSlider", {.8, .5});
    c.driver->release("develop.exposureSlider", {.8, .5});
    c.driver->frame();
    exact(c, Document{}, "Continued old Slider pointer stream mutated new asset B");
    require(!c.driver->object("develop.exposureSlider")->property("pressed").toBool() &&
                !c.editor.canUndo() && !c.editor.canRedo(),
            "B retained old gesture or history");
    rendered(c, Document{});
    savedWithoutEnd(c, b, Document{});
    const auto aSaved = luma::Catalog(c.catalogFile.toStdString()).assetByID(a.toStdString());
    require(aSaved && aSaved->edits == expectedA, "Switch lost A's accepted held gesture");
    activate(c, 0, expectedA);
    require(c.editor.sourcePath() == sourceA, "Returning to A loaded another public source");
    rendered(c, expectedA);
    // Positive control after transition: a fresh drag still works and forms one Undo unit.
    const auto fresh = dragOnly(c, "develop.exposureSlider", "exposure", {.4, .5});
    undoRedo(c, expectedA, fresh, true);
    durable(c, a, fresh);
    const auto bSaved = luma::Catalog(c.catalogFile.toStdString()).assetByID(b.toStdString());
    require(bSaved && bSaved->edits == Document{}, "Fresh A edit changed inactive B");
}
void heldLibrary(Context &c) {
    const KeyboardNavigation keys;
    c.openFirst();
    const auto before = c.editor.currentSettings();
    const auto photo = c.editor.photoID();
    focusByTab(c, "main.libraryButton");
    c.reveal("develop.exposureSlider");
    c.driver->press("develop.exposureSlider", c.driver->sliderThumb("develop.exposureSlider"));
    c.driver->move("develop.exposureSlider", {.67, .5});
    const auto partial = c.editor.currentSettings();
    auto expected = before;
    expected.exposure = partial.exposure;
    require(partial != before &&
                c.driver->object("develop.exposureSlider")->property("pressed").toBool(),
            "Library-switch fixture never held a real changed exposure gesture");
    exact(c, expected, "Held Slider changed unrelated fields");
    focusByTab(c, "main.libraryButton"); // Valid keyboard input while Left remains held.
    require(c.window->activeFocusItem() == c.driver->item("main.libraryButton"),
            "Library switch lacked actual QQuick active focus");
    c.driver->key(Qt::Key_Return);
    require(c.window->property("libraryMode").toBool(),
            "Real focused Return did not enter Library");
    c.driver->move("library.grid", {.7, .5});
    // Paired with the earlier Slider press; no second mouse press is manufactured.
    c.driver->release("library.grid", {.7, .5});
    c.driver->frame();
    exact(c, expected, "Hiding Develop leaked further held-pointer edits");
    require(!c.driver->object("develop.exposureSlider")->property("pressed").toBool(),
            "Library transition retained Slider pressed state");
    savedWithoutEnd(c, photo, expected);
    c.driver->click("main.developButton");
    require(!c.window->property("libraryMode").toBool(), "Develop failed to reopen after release");
    rendered(c, expected);
    undoRedo(c, before, expected, true);
    const auto fresh = dragOnly(c, "develop.exposureSlider", "exposure", {.4, .5});
    undoRedo(c, expected, fresh);
    durable(c, photo, fresh);
}
void selectGroups(Context &c, std::uint32_t groups) {
    c.selectPresetGroups(groups);
}
void presetOpened(Context &c) {
    c.driver->wait([&] { return c.driver->object("presetEditor")->property("opened").toBool(); },
                   "Real preset/copy dialog opened");
}
void presetClosed(Context &c) {
    c.driver->wait([&] { return !c.driver->object("presetEditor")->property("visible").toBool(); },
                   "Real preset/copy dialog closed");
    require(c.presets.error().isEmpty(), c.presets.error());
}
Document opticsOnly(const Document &target, const Document &source) {
    // Independent of mergeSettings(), the production operation under test.
    auto result = target;
    result.lens = source.lens;
    result.setEnabled(luma::Correction::lens, source.enabled(luma::Correction::lens));
    return result;
}
void opticsSubset(Context &c) {
    openKnownLens(c);
    selectKnownLensProfile(c);
    dragOnly(c, "develop.lens.distortionSlider", "lens.distortion", {.65, .5});
    dragOnly(c, "develop.lens.vignettingSlider", "lens.vignetting", {.3, .5});
    c.reveal("lensCA");
    const auto beforeCA = c.editor.currentSettings();
    c.driver->click("lensCA");
    auto captured = beforeCA;
    captured.lens.chromaticAberration = !beforeCA.lens.chromaticAberration;
    exact(c, captured, "CA fixture changed unrelated settings");
    captured = dragOnly(c, "develop.exposureSlider", "exposure", {.58, .5});
    captured = dragOnly(c, "develop.saturationSlider", "saturation", {.36, .5});
    durable(c, c.editor.photoID(), captured);
    c.driver->click("preset.tabs.presets");
    c.driver->click("presetCreate");
    presetOpened(c);
    c.driver->type("presetName", "UX optics only");
    selectGroups(c, 64);
    c.driver->click("presetSave");
    presetClosed(c);
    require(c.presets.items().size() == 1, "Optics fixture did not create one user preset");
    const auto presetID = c.presets.items().front().toMap()["id"].toString();
    const auto preset = c.presets.snapshot(presetID);
    const auto persistedSubset = opticsOnly(Document{}, captured);
    require(preset.groups == 64 && preset.parameters.empty() && preset.settings == persistedSubset,
            "Optics preset did not retain an independent exact captured document/subset");
    const auto stored = luma::PresetStore(c.stateRoot + "/presets.json").load();
    require(stored.size() == 1 && stored.front().id == presetID &&
                stored.front().preset.groups == 64 &&
                stored.front().preset.settings == persistedSubset,
            "Optics-only user preset differs from durable preset store");
    c.driver->click("preset.copy");
    presetOpened(c);
    selectGroups(c, 64);
    c.driver->click("presetSave");
    presetClosed(c);
    const auto copy = c.presets.copiedSettings();
    require(copy && copy->groups == 64 && copy->settings == captured,
            "Copy dialog did not capture exactly the optics subset snapshot");
    c.reveal("lensReset");
    c.driver->click("lensReset");
    auto target = captured;
    target.lens = {};
    target.setEnabled(luma::Correction::lens, false);
    exact(c, target, "Lens reset fixture changed unrelated groups");
    target = dragOnly(c, "develop.exposureSlider", "exposure", {.43, .5});
    target = dragOnly(c, "develop.saturationSlider", "saturation", {.59, .5});
    const auto expected = opticsOnly(target, captured);
    require(expected != target && target.exposure != captured.exposure &&
                target.saturation != captured.saturation,
            "Optics subset fixture has no positive selected/unselected differences");
    const auto expectedPixels = pixels(c, expected), targetPixels = pixels(c, target);
    require(expectedPixels != targetPixels, "Known lens fixture has no visible optics effect");
    c.driver->click("preset.paste");
    exact(c, expected, "Optics Paste changed an unselected group or failed to restore optics");
    rendered(c, expected);
    undoRedo(c, target, expected);
    c.driver->click("main.undoButton");
    exact(c, target, "Cannot restore independent target before preset application");
    c.driver->wait([&] { return c.driver->find("preset." + presetID) != nullptr; },
                   "Optics-only preset row ready");
    c.driver->click("preset." + presetID);
    c.driver->move("develop.preview", {.5, .5});
    exact(c, expected, "Optics preset changed unselected target fields");
    rendered(c, expected);
    undoRedo(c, target, expected);
    durable(c, c.editor.photoID(), expected);
    require(c.presets.copiedSettings()->settings == captured &&
                c.presets.snapshot(presetID).settings == persistedSubset,
            "Applying optics linked/mutated the captured Copy or durable preset source");
}
} // namespace

bool gestureRemainingScenario(Context &c, const QString &id) {
    if (id == "UX-DSC-005")
        scrollHeight(c);
    else if (id == "UX-DSC-006")
        scrollPhoto(c);
    else if (id == "UX-DSC-008")
        languageDeep(c);
    else if (id == "UX-DSC-009")
        keyboardReachability(c);
    else if (id == "UX-SLD-010")
        heldPhoto(c);
    else if (id == "UX-SLD-011")
        heldLibrary(c);
    else if (id == "UX-LNS-009")
        opticsSubset(c);
    else
        return false;
    return true;
}

} // namespace ux
