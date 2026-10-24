#include "apps/shared/PhotoDecoder.h"
#include "apps/shared/PresetStore.h"
#include "core/render/CpuRenderer.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QPointer>
#include <QQuickWindow>
#include <QSet>
#include <QSignalSpy>
#include <QStyleHints>
#include <QUuid>
#include <cstdio>
#include <exception>

// Bounded supplemental functional regression. Not a complete KEY015 adapter.
namespace ux {
namespace {
constexpr int maximumTabSteps = 96; // One bounded actual traversal, not a stress count.
constexpr auto candidateID = "CANDIDATE-KEYBOARD-SELECTED-ASSET-ACTIVATION";
constexpr auto keypadCandidateID = "CANDIDATE-KEYBOARD-KEYPAD-ASSET-ACTIVATION";
constexpr auto workflowCandidateID = "CANDIDATE-KEYBOARD-IMPORT-EDIT-EXPORT";
struct KeyboardNavigation {
    const Qt::TabFocusBehavior previous = QGuiApplication::styleHints()->tabFocusBehavior();
    KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    }
    ~KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(previous);
    }
};
QString digest(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read keyboard activation fixture: " + path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "Cannot hash complete keyboard activation source");
    return QString::fromLatin1(hash.result().toHex());
}
QJsonObject fullDocument(const luma::EditDocument &document) {
    const auto encoded = QJsonDocument::fromJson(luma::encodePreset(
        {"Keyboard activation document", {}, document, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot retain complete keyboard activation document");
    return encoded.object();
}
bool inside(QQuickItem *item, QQuickItem *ancestor) {
    if (!ancestor)
        return false;
    for (auto *parent = item; parent; parent = parent->parentItem())
        if (parent == ancestor)
            return true;
    return false;
}
bool centerVisible(QQuickItem *item) {
    if (!item || !item->isVisible() || !item->isEnabled() || !item->window() ||
        item->width() <= 0 || item->height() <= 0)
        return false;
    const auto center = item->mapToScene({item->width() / 2, item->height() / 2});
    if (!QRectF(0, 0, item->window()->width(), item->window()->height()).contains(center))
        return false;
    for (auto *ancestor = item; ancestor; ancestor = ancestor->parentItem())
        if (ancestor->clip() && !ancestor->boundingRect().contains(ancestor->mapFromScene(center)))
            return false;
    return true;
}
QJsonObject itemState(QQuickItem *item) {
    if (!item)
        return {{"present", false}};
    return {{"present", true},
            {"object_name", item->objectName()},
            {"class", QString::fromUtf8(item->metaObject()->className())},
            {"visible", item->isVisible()},
            {"enabled", item->isEnabled()},
            {"active_focus", item->hasActiveFocus()},
            {"active_focus_on_tab", item->activeFocusOnTab()},
            {"center_visible_under_clips", centerVisible(item)}};
}
// Observes only; never synthesizes input or throws from Qt dispatch.
class PointerAudit final : public QObject {
  public:
    explicit PointerAudit(QQuickWindow *caseWindow) : window(caseWindow) {
        QCoreApplication::instance()->installEventFilter(this);
    }
    ~PointerAudit() override {
        QCoreApplication::instance()->removeEventFilter(this);
    }
    QJsonArray events, keyEvents;
    int keypadPresses = 0, keypadRepeats = 0;

  protected:
    bool eventFilter(QObject *receiver, QEvent *event) override {
        if (event->type() == QEvent::KeyPress && receiver == window.data()) {
            const auto *key = static_cast<QKeyEvent *>(event);
            keyEvents.append(QJsonObject{{"key", key->key()},
                                         {"modifiers", static_cast<int>(key->modifiers())},
                                         {"auto_repeat", key->isAutoRepeat()}});
            if (key->key() == Qt::Key_Enter && key->modifiers() == Qt::KeypadModifier) {
                if (key->isAutoRepeat())
                    ++keypadRepeats;
                else
                    ++keypadPresses;
            }
        }
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
            events.append(QJsonObject{
                {"type", static_cast<int>(event->type())},
                {"receiver_name", receiver->objectName()},
                {"receiver_class", QString::fromUtf8(receiver->metaObject()->className())},
                {"spontaneous", event->spontaneous()}});
            break;
        default:
            break;
        }
        return false;
    }

  private:
    QPointer<QQuickWindow> window;
};
struct Fixture {
    QString publicSource, source, sha, id;
    QImage expectedPixels;
};
Fixture fixture(Context &c) {
    auto &d = *c.driver;
    require(c.library.count() == 0 && !c.editor.hasPhoto(),
            "Keyboard activation candidate requires a fresh private Context");
    Fixture value;
    value.publicSource = c.fixture + "/Photo-0000.png";
    value.sha = digest(value.publicSource);
    const auto directory = c.root + "/owned-input";
    const auto copy = directory + "/Keyboard-source.png";
    require(QDir().mkpath(directory) && !QFileInfo::exists(copy) &&
                QFile::copy(value.publicSource, copy) && !QFileInfo(copy).isSymLink() &&
                digest(copy) == value.sha,
            "Cannot create a new private generated keyboard activation source");
    value.source = QFileInfo(copy).canonicalFilePath();
    require(!value.source.isEmpty() &&
                value.source.startsWith(QFileInfo(c.root).canonicalFilePath() + '/'),
            "Keyboard activation original escaped the private case root");
    // All pointer inputs below belong ONLY to declared import fixture setup.
    // The keyboard acceptance boundary is recorded after setup has fully settled.
    d.log({{"action", "declared_keyboard_activation_import_fixture_begin"},
           {"pointer_setup_inputs", true},
           {"native_chooser_exercised", false}});
    require(d.object("library.importDialog")
                ->setProperty("sources",
                              QVariant::fromValue(QList<QUrl>{QUrl::fromLocalFile(value.source)})),
            "Declared file-chooser result fixture rejected its single owned source");
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "declared Import fixture opened");
    d.choose("library.importMode", 0);
    d.click("library.importStart");
    d.wait(
        [&] {
            return !c.library.importing() && !c.library.working() && !c.library.loading() &&
                   c.library.count() == 1;
        },
        "declared one-file Add fixture completed");
    d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
           "declared Import fixture modal exit completed");
    const luma::Catalog catalog(c.catalogFile.toStdString());
    const auto ids = catalog.libraryIDs({});
    require(ids.size() == 1, "Declared Import fixture did not produce exactly one catalog UUID");
    value.id = QString::fromStdString(ids.front());
    const auto asset = catalog.assetByID(ids.front());
    require(!QUuid(value.id).isNull() && asset &&
                QString::fromStdString(asset->path.string()) == value.source &&
                asset->checksum == value.sha.toStdString() &&
                asset->edits == luma::EditDocument{} && c.library.selectedCount() == 0 &&
                !c.editor.hasPhoto() && c.library.error().isEmpty(),
            "Declared Import fixture has ambiguous UUID/source/SHA/default document/selection");
    d.wait(
        [&] {
            auto *card = d.find("library.card." + value.id);
            return card && card->property("assetID").toString() == value.id &&
                   card->property("filename").toString() == QFileInfo(value.source).fileName();
        },
        "GATE: actual Library card metadata appears without harness model.data retries");
    value.expectedPixels = fixtureReference(c, luma::EditDocument{}, value.publicSource);
    require(!value.expectedPixels.isNull(), "Keyboard activation source pixel oracle is empty");
    d.frame();
    return value;
}
void reproduce(Context &c, bool keypadOnly) {
    const KeyboardNavigation keyboardNavigation;
    const auto known = fixture(c);
    auto &d = *c.driver;
    QPointer<QQuickItem> card = d.item("library.card." + known.id);
    QPointer<QQuickItem> grid = d.item("library.grid");
    QSignalSpy opens(&c.library, &LibraryModel::openRequested);
    QSignalSpy loads(&c.editor, &EditorController::photoLoaded);
    QSignalSpy selected(d.object("library.selectAll"), SIGNAL(clicked()));
    QSignalSpy developed(d.object("main.developButton"), SIGNAL(clicked()));
    require(opens.isValid() && loads.isValid() && selected.isValid() && developed.isValid(),
            "Keyboard activation signals are not observable");
    QJsonArray trace, attempts;
    int cardInstanceChanges = 0;
    const auto resolveCard = [&] {
        // A virtualized delegate may be replaced after import metadata settles.
        // Observe the current UUID-matched card; never set focus or mutate the model.
        auto *current = qobject_cast<QQuickItem *>(d.find("library.card." + known.id));
        require(!current || (current->property("assetID").toString() == known.id &&
                             inside(current, grid.data())),
                "Keyboard observer found an unrelated card for the imported UUID");
        if (card.data() != current) {
            ++cardInstanceChanges;
            card = current;
        }
    };
    bool reachedCard = false, reachedGrid = false;
    bool forwardClosed = false, backwardClosed = false;
    QString phase = "acceptance_begin";
    const auto observe = [&](const QString &label, bool afterActualTab = false) {
        resolveCard();
        auto *focus = c.window->activeFocusItem();
        trace.append(QJsonObject{{"label", label},
                                 {"after_actual_tab", afterActualTab},
                                 {"focus", itemState(focus)},
                                 {"inside_expected_card", inside(focus, card.data())},
                                 {"grid_is_exact_focus", focus && focus == grid.data()},
                                 {"library_mode", c.window->property("libraryMode").toBool()},
                                 {"selected_count", c.library.selectedCount()},
                                 {"open_requested_count", opens.count()},
                                 {"photo_loaded_count", loads.count()}});
        reachedCard = reachedCard || (afterActualTab && inside(focus, card.data()));
        reachedGrid = reachedGrid || (afterActualTab && focus && focus == grid.data());
    };
    PointerAudit audit(c.window); // Installed only AFTER all declared setup pointer events.
    const auto evidence = [&](const QString &outcome) {
        resolveCard();
        auto *preview = d.item("develop.preview");
        writeJson(
            c.root + "/keyboard-activation.json",
            {{"candidate", keypadOnly ? keypadCandidateID : candidateID},
             {"outcome", outcome},
             {"phase", phase},
             {"official_KEY015_qualified", false},
             {"native_chooser_keyboard", false},
             {"fixture_import_uses_pointer", true},
             {"acceptance_allowed_inputs",
              keypadOnly ? "Tab/Shift+Tab/Space; modified Enter rejection; keypad Enter"
                         : "Tab/Shift+Tab/Return/Space only"},
             {"auto_repeat_probe",
              keypadOnly
                  ? "one public autoRepeat QKeyEvent at actual QQuickWindow; no OS cadence claim"
                  : "not exercised by this variant"},
             {"source", known.source},
             {"expected_uuid", known.id},
             {"expected_source_sha256", known.sha},
             {"actual_source_sha256", digest(known.source)},
             {"public_source_sha256", digest(known.publicSource)},
             {"card_reached_by_actual_tab", reachedCard},
             {"card_tab_observation", reachedCard ? "REACHED"
                                      : forwardClosed && backwardClosed
                                          ? "NOT_REACHED_IN_TWO_COMPLETE_CYCLES"
                                          : "INCOMPLETE"},
             {"grid_reached_by_actual_tab", reachedGrid},
             {"forward_cycle_closed", forwardClosed},
             {"backward_cycle_closed", backwardClosed},
             {"card", itemState(card.data())},
             {"card_instance_changes", cardInstanceChanges},
             {"grid", itemState(grid.data())},
             {"select_all_clicks", selected.count()},
             {"develop_clicks", developed.count()},
             {"open_requested_count", opens.count()},
             {"photo_loaded_count", loads.count()},
             {"actual_uuid", c.editor.photoID()},
             {"actual_source", c.editor.sourcePath()},
             {"has_photo", c.editor.hasPhoto()},
             {"editor_error", c.editor.error()},
             {"preview_status", preview->property("status").toInt()},
             {"preview_source", preview->property("source").toUrl().toString()},
             {"current_preview_url", c.editor.previewUrl()},
             {"provider_pixels_match_independent_default",
              c.provider->requestImage({}, nullptr, {}) == known.expectedPixels},
             {"actual_full_document", fullDocument(c.editor.currentSettings())},
             {"expected_full_document", fullDocument(luma::EditDocument{})},
             {"pointer_events_during_acceptance", audit.events},
             {"actual_window_key_presses", audit.keyEvents},
             {"keypad_primary_presses", audit.keypadPresses},
             {"keypad_repeat_presses", audit.keypadRepeats},
             {"route_attempts", attempts},
             {"actual_focus_trace", trace}});
    };
    const auto key = [&](Qt::Key value, Qt::KeyboardModifiers modifiers, const QString &label) {
        require(QGuiApplication::focusWindow() == c.window && c.window->isActive(),
                "GATE: keyboard input lost the actual case window; no focus setter fallback");
        d.key(value, modifiers);
        d.frame();
        observe(label, value == Qt::Key_Tab);
        require(audit.events.isEmpty(), "Unexpected pointer input during keyboard-only acceptance");
        require(opens.count() <= 1 && loads.count() <= 1,
                "Keyboard input duplicated selected-asset open/load");
    };
    const auto tabTo = [&](const QString &id) {
        QPointer<QQuickItem> target = d.item(id);
        for (int step = 0; target && step < maximumTabSteps; ++step) {
            if (target->hasActiveFocus() && c.window->activeFocusItem() == target.data())
                break;
            key(Qt::Key_Tab, Qt::NoModifier, "tab_to_" + id);
        }
        require(target && target->hasActiveFocus() &&
                    c.window->activeFocusItem() == target.data() && centerVisible(target.data()),
                "GATE: real Tab cannot reach required keyboard button " + id);
    };
    QSet<QString> tried;
    const auto tryCurrentAssetRoute = [&] {
        resolveCard();
        auto *focus = c.window->activeFocusItem();
        QString route;
        if (focus && focus == grid.data())
            route = "library_grid";
        else if (card && (focus == card.data() ||
                          (inside(focus, card.data()) && focus->inherits("QQuickMouseArea"))))
            route = "selected_photo_card";
        if (route.isEmpty() || tried.contains(route) || !centerVisible(focus) ||
            (keypadOnly && route != "selected_photo_card"))
            return;
        tried.insert(route);
        if (keypadOnly) {
            require(card && focus == card.data() && card->hasActiveFocus(),
                    "GATE: keypad acceptance requires the actual Tab-focused PhotoCard");
            const auto unchanged = [&] {
                require(opens.isEmpty() && loads.isEmpty() && !c.editor.hasPhoto() &&
                            c.editor.currentSettings() == luma::EditDocument{} &&
                            !c.editor.canUndo() && !c.editor.canRedo() &&
                            c.library.selectionIDs() ==
                                std::vector<std::string>{known.id.toStdString()} &&
                            card && c.window->activeFocusItem() == card.data() &&
                            card->hasActiveFocus() && audit.events.isEmpty(),
                        "Modified/repeated Enter changed focus, opened an asset or mutated state");
            };
            for (const auto modifier :
                 {Qt::ShiftModifier, Qt::ControlModifier, Qt::AltModifier, Qt::MetaModifier}) {
                key(Qt::Key_Enter, Qt::KeypadModifier | modifier, "reject_modified_keypad_enter");
                unchanged();
            }
            require(QGuiApplication::focusWindow() == c.window && c.window->isActive(),
                    "GATE: repeat probe lost the actual keyboard window");
            d.log({{"action", "single_keypad_enter_auto_repeat_input"},
                   {"delivery", "public QKeyEvent at actual QQuickWindow"},
                   {"native_os_repeat_cadence_exercised", false}});
            QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Enter, Qt::KeypadModifier,
                             QString(QChar(0x0d)), true, 1);
            QCoreApplication::sendEvent(c.window, &repeat);
            QCoreApplication::processEvents();
            d.frame();
            observe("reject_auto_repeat_keypad_enter");
            unchanged();
            require(audit.keypadRepeats == 1 && audit.keypadPresses == 0,
                    "GATE: explicit repeat did not reach the actual window with its repeat flag");
            const auto before = itemState(c.window->activeFocusItem());
            key(Qt::Key_Enter, Qt::KeypadModifier, "asset_activation_keypad_enter");
            attempts.append(QJsonObject{{"route", route},
                                        {"key", static_cast<int>(Qt::Key_Enter)},
                                        {"modifiers", static_cast<int>(Qt::KeypadModifier)},
                                        {"focus_before", before},
                                        {"open_requested_count", opens.count()},
                                        {"photo_loaded_count", loads.count()}});
            require(audit.keypadPresses == 1 && opens.count() == 1,
                    "Keypad Enter did not dispatch once and request the expected asset once");
            return;
        }
        // Probe only the known asset surface, never arbitrary dialog, delete,
        // import, batch or file-picker controls encountered during traversal.
        for (const auto value : {Qt::Key_Return, Qt::Key_Space}) {
            const auto before = itemState(c.window->activeFocusItem());
            key(value, Qt::NoModifier, "asset_activation_" + route);
            attempts.append(QJsonObject{{"route", route},
                                        {"key", static_cast<int>(value)},
                                        {"focus_before", before},
                                        {"open_requested_count", opens.count()},
                                        {"photo_loaded_count", loads.count()}});
            if (!opens.isEmpty())
                return;
            require(!c.editor.hasPhoto() && c.editor.currentSettings() == luma::EditDocument{} &&
                        c.library.selectionIDs() ==
                            std::vector<std::string>{known.id.toStdString()},
                    "Unsupported asset key changed the selected identity or document");
        }
    };
    const auto traverse = [&](Qt::KeyboardModifiers modifiers, const QString &direction) {
        QPointer<QQuickItem> start = c.window->activeFocusItem();
        require(start, "GATE: keyboard traversal has no actual starting focus");
        for (int step = 0; step < maximumTabSteps; ++step) {
            key(Qt::Key_Tab, modifiers, direction + "_tab");
            auto *focus = c.window->activeFocusItem();
            require(focus && focus->isVisible() && focus->isEnabled(),
                    "GATE: Tab traversal reached absent/hidden/disabled focus");
            tryCurrentAssetRoute();
            if (!opens.isEmpty())
                return false; // Positive activation stops navigation immediately.
            if (c.window->activeFocusItem() == start.data())
                return true;
        }
        return false; // Bound reached is a gate failure, not proof of unreachability.
    };
    d.log({{"action", "keyboard_activation_acceptance_begin"},
           {"native_file_chooser_excluded", true},
           {"public_fixture_count", 1}});
    try {
        observe("acceptance_begin");
        phase = "keyboard_select_all";
        tabTo("library.selectAll");
        key(Qt::Key_Space, Qt::NoModifier, "select_all_space");
        d.wait([&] { return !c.library.selecting(); }, "keyboard Select all completed");
        require(selected.count() == 1 && c.library.selectedCount() == 1 &&
                    c.library.selectionIDs() == std::vector<std::string>{known.id.toStdString()} &&
                    !c.editor.hasPhoto() && opens.isEmpty() && loads.isEmpty(),
                "GATE: real keyboard Select all did not select only the expected imported UUID");
        phase = "forward_tab_asset_routes";
        forwardClosed = traverse(Qt::NoModifier, "forward");
        if (opens.isEmpty()) {
            require(forwardClosed,
                    "GATE: bounded forward Tab traversal did not close its actual cycle");
            phase = "backward_tab_asset_routes";
            backwardClosed = traverse(Qt::ShiftModifier, "backward");
            require(!opens.isEmpty() || backwardClosed,
                    "GATE: bounded backward Tab traversal did not close its actual cycle");
        }
        if (keypadOnly)
            require(!opens.isEmpty() && reachedCard,
                    "GATE: real Tab/card keypad activation was not reached; no Develop fallback");
        if (opens.isEmpty()) {
            phase = "selected_asset_develop_button";
            tabTo("main.developButton");
            key(Qt::Key_Space, Qt::NoModifier, "develop_button_space");
            require(developed.count() == 1 && !c.window->property("libraryMode").toBool(),
                    "GATE: real focused Develop button did not activate exactly once");
        }
        phase = "actual_selected_asset_activation";
        // Environment guard only. No more input/focus mutations while waiting.
        d.wait([&] { return !opens.isEmpty() || !loads.isEmpty() || !c.editor.error().isEmpty(); },
               "selected imported asset opens from actual tested keyboard routes", 5000);
        require(opens.count() == 1 && opens[0][0].toUrl().toLocalFile() == known.source &&
                    c.editor.error().isEmpty(),
                "Keyboard route opened the wrong source or did not emit one real openRequested");
        phase = "exact_default_document_ready_pixels";
        d.wait(
            [&] {
                auto *preview = d.item("develop.preview");
                return loads.count() == 1 && c.editor.hasPhoto() && c.editor.error().isEmpty() &&
                       !c.editor.busy() && !c.editor.saving() && c.editor.photoID() == known.id &&
                       c.editor.sourcePath() == known.source &&
                       c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo() &&
                       !c.editor.canRedo() && !c.editor.original() && !c.editor.presetPreview() &&
                       !c.window->property("libraryMode").toBool() && preview->isVisible() &&
                       preview->property("status").toInt() == 1 &&
                       preview->property("source").toUrl().toString() == c.editor.previewUrl() &&
                       c.provider->requestImage({}, nullptr, {}) == known.expectedPixels;
            },
            "keyboard-opened exact UUID/default document/current-source Ready pixels");
        require(loads[0][0].toString() == known.id && audit.events.isEmpty() &&
                    digest(known.source) == known.sha && digest(known.publicSource) == known.sha,
                "Keyboard activation crossed UUID/input/source integrity boundaries");
        const luma::Catalog catalog(c.catalogFile.toStdString());
        const auto active = catalog.activeAsset();
        require(catalog.libraryIDs({}) == std::vector<std::string>{known.id.toStdString()} &&
                    active && active->id == known.id.toStdString() &&
                    QString::fromStdString(active->path.string()) == known.source &&
                    active->checksum == known.sha.toStdString() &&
                    active->edits == luma::EditDocument{},
                "Keyboard activation changed membership or the durable source/full document");
        c.checkData();
        phase = "completed";
        evidence("CANDIDATE_ACTIVATION_REACHED_NOT_KEY015_QUALIFIED");
    } catch (...) {
        const auto failure = std::current_exception();
        try {
            evidence("FAILED_OR_PRECONDITION_GATE_NOT_QUALIFIED");
        } catch (const std::exception &error) {
            std::fprintf(stderr, "Keyboard activation diagnostic failed: %s\n", error.what());
        } catch (...) {
            std::fputs("Keyboard activation diagnostic failed; original failure retained.\n",
                       stderr);
        }
        std::rethrow_exception(failure);
    }
}
void keyboardWorkflow(Context &c) {
    const KeyboardNavigation navigation;
    auto &d = *c.driver;
    // Geometry fixture only; this does not qualify actual OS resize requests.
    c.window->resize(1000, 640);
    d.wait([&] { return c.window->width() == 1000 && c.window->height() == 640; },
           "minimum supported keyboard workflow viewport");
    d.frame();
    require(c.library.count() == 0 && !c.editor.hasPhoto(),
            "Keyboard workflow requires an empty owned catalog");
    const auto source = c.fixture + "/Photo-0000.png";
    const auto sourceSHA = digest(source);
    const auto destination = c.root + "/exports/keyboard";
    require(QDir().mkpath(destination), "Cannot create keyboard export destination");
    // The only input seam: the native file chooser's selected URL. Import itself,
    // selection, opening Develop, edits and export are all real keyboard UI input.
    require(
        d.object("library.importDialog")
            ->setProperty("sources", QVariant::fromValue(QList<QUrl>{QUrl::fromLocalFile(source)})),
        "Keyboard workflow chooser-result fixture rejected");
    PointerAudit audit(c.window);
    QJsonArray trace;
    QString phase = "import";
    const auto evidence = [&](const QString &status) {
        writeJson(c.root + "/keyboard-workflow.json",
                  {{"case_id", workflowCandidateID},
                   {"status", status},
                   {"phase", phase},
                   {"official_KEY015_qualified", false},
                   {"native_chooser_keyboard", false},
                   {"chooser_result_fixture_only", true},
                   {"viewport_width", c.window->width()},
                   {"viewport_height", c.window->height()},
                   {"pointer_events", audit.events},
                   {"window_key_presses", audit.keyEvents},
                   {"focus_trace", trace},
                   {"source_sha256", sourceSHA},
                   {"actual_source_sha256", digest(source)},
                   {"document", fullDocument(c.editor.currentSettings())}});
    };
    const auto key = [&](Qt::Key value, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        require(QGuiApplication::focusWindow() == c.window && c.window->isActive(),
                "Keyboard workflow lost its actual window; no activation fallback");
        d.key(value, modifiers);
        d.frame();
        trace.append(QJsonObject{{"phase", phase},
                                 {"key", static_cast<int>(value)},
                                 {"focus", itemState(c.window->activeFocusItem())}});
        require(audit.events.isEmpty(), "Pointer event crossed keyboard workflow boundary");
    };
    const auto tabTo = [&](const QString &id) {
        QPointer<QQuickItem> target = d.item(id);
        for (int i = 0; target && !target->hasActiveFocus() && i < 160; ++i)
            key(Qt::Key_Tab);
        require(target && target->hasActiveFocus() && centerVisible(target.data()),
                "Keyboard workflow focused an unreachable or clipped control: " + id);
    };
    const auto activate = [&](const QString &id) {
        tabTo(id);
        key(Qt::Key_Space);
    };
    const auto text = [&](const QString &id, const QString &value) {
        tabTo(id);
        require(c.window->activeFocusItem() == d.item(id),
                "Text entry must target the actual focused field");
        key(Qt::Key_A, Qt::ControlModifier);
        QInputMethodEvent input;
        input.setCommitString(value);
        d.log({{"action", "keyboard_workflow_ime_commit"}, {"id", id}, {"text", value}});
        QCoreApplication::sendEvent(c.window->activeFocusItem(), &input);
        key(Qt::Key_Tab); // Commit through the field's real editingFinished.
        require(d.object(id)->property("text").toString() == value,
                "Keyboard text entry changed the literal value");
    };
    try {
        activate("library.importButton");
        d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
               "keyboard Import opened");
        require(d.object("library.importMode")->property("currentIndex").toInt() == 0,
                "Fresh keyboard Import must use Add");
        activate("library.importStart");
        d.wait(
            [&] { return !c.library.working() && !c.library.loading() && c.library.count() == 1; },
            "keyboard Import completed", 30000);
        d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
               "keyboard Import closed");
        const auto ids = luma::Catalog(c.catalogFile.toStdString()).libraryIDs({});
        require(ids.size() == 1 && c.library.error().isEmpty(), "Keyboard Import identity failed");
        const auto photoID = QString::fromStdString(ids.front());
        phase = "select_and_develop";
        activate("library.selectAll");
        d.wait([&] { return !c.library.selecting() && c.library.selectedCount() == 1; },
               "keyboard selection completed");
        tabTo("library.card." + photoID);
        key(Qt::Key_Return);
        c.loaded();
        require(c.editor.photoID() == photoID && c.editor.sourcePath() == source &&
                    c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo(),
                "Keyboard opened the wrong asset or document");
        phase = "edit";
        tabTo("develop.exposureSlider");
        const auto step = d.object("develop.exposureSlider")->property("stepSize").toDouble();
        require(step == .01, "Exposure keyboard step differs from 0.01 EV");
        key(Qt::Key_Right);
        key(Qt::Key_Tab);
        d.wait([&] { return !c.editor.saving() && !c.editor.busy() && c.editor.canUndo(); },
               "keyboard edit durably saved");
        luma::EditDocument expected;
        expected.exposure = .01;
        require(c.editor.currentSettings() == expected, "Keyboard edit changed unrelated settings");
        phase = "export_settings";
        activate("exportButton");
        d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
               "keyboard Export opened");
        text("export.destination", destination);
        text("exportNaming", "Keyboard-output");
        tabTo("exportFormat");
        require(d.object("exportFormat")->property("currentIndex").toInt() == 0,
                "Fresh export must start in JPEG");
        key(Qt::Key_Down);
        key(Qt::Key_Tab);
        // Visit lower settings through actual Tab. The focused controls must be
        // visible under clipping, without wheel, pointer, focus or scroll setters.
        for (const auto *id : {"export.colorSpace", "export.resize", "export.outputMedium",
                               "export.metadata", "export.watermarkKind"})
            tabTo(id);
        const auto form = d.object("exportPanel")->property("form").toMap();
        require(form["destination"].toString() == destination &&
                    form["naming"].toString() == "Keyboard-output" && form["format"].toInt() == 1 &&
                    form["colorSpace"].toInt() == 0 && form["resize"].toInt() == 0 &&
                    form["outputMedium"].toInt() == 0 && form["watermarkKind"].toInt() == 0,
                "Keyboard export form differs from literal settings");
        phase = "export";
        activate("export.current");
        d.wait([&] { return !c.exporter.working() && c.exporter.completed() == 1; },
               "keyboard real PNG export completed", 30000);
        require(c.exporter.succeeded() == 1 && c.exporter.error().isEmpty() &&
                    c.exporter.failures().isEmpty(),
                "Keyboard export failed");
        const auto output = destination + "/Keyboard-output.png";
        const QImage image(output);
        const auto decoded = luma::PhotoDecoder().decode(source.toStdString());
        require(decoded.image.width <= 512 && decoded.image.height <= 512 &&
                    decoded.image.alpha.empty(),
                "Keyboard export oracle requires a small opaque fixture");
        // Export targets sRGB independently of the current display profile.
        // This checks the UI document/queue binding, not renderer color science.
        const auto rendered =
            luma::renderCPU(decoded.image, expected, decoded.metadata.orientation, {}, {},
                            decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
        const auto reference =
            QImage(rendered.rgba.data(), rendered.width, rendered.height,
                   static_cast<qsizetype>(rendered.width) * 4, QImage::Format_RGBA8888)
                .copy();
        require(reference.save(c.root + "/keyboard-expected-export.png"),
                "Cannot retain keyboard export pixel oracle");
        auto samples = image.convertToFormat(QImage::Format_RGBA8888);
        // Compare sample bytes, separately requiring a valid output color space.
        samples.setColorSpace({});
        samples.setDevicePixelRatio(1);
        require(!image.isNull() && image.colorSpace().isValid() && samples == reference,
                "Keyboard export pixels differ from the complete expected document");
        require(QDir(destination).entryList(QDir::Files | QDir::Hidden, QDir::Name) ==
                    QStringList{"Keyboard-output.png"},
                "Keyboard export created extra outputs");
        const auto asset = luma::Catalog(c.catalogFile.toStdString()).assetByID(ids.front());
        require(asset && asset->edits == expected && asset->checksum == sourceSHA.toStdString() &&
                    c.library.selectionIDs() == ids && c.editor.currentSettings() == expected &&
                    digest(source) == sourceSHA && audit.events.isEmpty(),
                "Keyboard workflow changed source, selection or durable edits");
        c.checkData();
        phase = "completed";
        evidence("PASS");
    } catch (...) {
        const auto failure = std::current_exception();
        try {
            evidence("FAIL");
        } catch (...) {
            std::fputs("Keyboard workflow evidence failed; retaining original failure.\n", stderr);
        }
        std::rethrow_exception(failure);
    }
}
} // namespace
bool keyboardActivationCandidate(Context &c, const QString &id) {
    if (id == workflowCandidateID) {
        keyboardWorkflow(c);
        return true;
    }
    if (id != candidateID && id != keypadCandidateID)
        return false;
    reproduce(c, id == keypadCandidateID);
    return true;
}
} // namespace ux
