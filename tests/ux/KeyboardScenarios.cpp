#include "tests/ux/UxContext.h"
#include <QGuiApplication>
#include <QJsonArray>
#include <QKeyEvent>
#include <QSet>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTest>
#include <stdexcept>

namespace ux {
namespace {
struct KeyboardNavigation {
    Qt::TabFocusBehavior previous = QGuiApplication::styleHints()->tabFocusBehavior();
    KeyboardNavigation() {
        // Process-local equivalent of macOS full keyboard navigation. Do not change OS settings.
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    }
    ~KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(previous);
    }
};
QString identity(QQuickItem *item) {
    if (!item)
        return "<none>";
    return item->objectName() + ":" + QString::fromUtf8(item->metaObject()->className());
}
void tabTo(Context &c, const QString &name) {
    auto *target = c.driver->item(name);
    for (int step = 0; step < 160; ++step) {
        c.driver->key(Qt::Key_Tab);
        if (target->hasActiveFocus())
            return;
    }
    throw std::runtime_error(("Tab cannot reach " + name).toStdString());
}
QVector<QQuickItem *> cycle(Context &c) {
    auto &d = *c.driver;
    d.click("main.libraryButton");
    tabTo(c, "main.libraryButton");
    QVector<QQuickItem *> sequence;
    QJsonArray trace;
    auto *start = c.window->activeFocusItem();
    require(start, "Tab fixture has no starting focus");
    for (int step = 0; step < 160; ++step) {
        d.key(Qt::Key_Tab);
        auto *focused = c.window->activeFocusItem();
        require(focused && focused->isVisible() && focused->isEnabled(),
                "Tab focused a hidden, disabled or absent control");
        sequence.append(focused);
        trace.append(identity(focused));
        if (focused == start)
            break;
    }
    d.log({{"action", "tab_cycle"}, {"controls", trace}});
    require(!sequence.isEmpty() && sequence.back() == start, "Tab did not close a focus cycle");
    return sequence;
}
QImage pixels(Context &c, QQuickItem *item) {
    c.driver->frame();
    const auto image = c.window->grabWindow();
    require(!image.isNull(), "Focus indication screenshot is empty");
    const auto box = item->mapRectToScene(item->boundingRect());
    const double scale = static_cast<double>(image.width()) / c.window->width();
    return image.copy(
        QRectF(box.x() * scale, box.y() * scale, box.width() * scale, box.height() * scale)
            .toAlignedRect());
}
struct ReleaseWrites {
    SqliteFault &fault;
    ~ReleaseWrites() {
        fault.holdWrites(false);
    }
};
} // namespace
bool keyboardScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-KEY-007", "UX-KEY-008", "UX-KEY-009", "UX-KEY-010",
                                         "UX-KEY-011", "UX-KEY-016", "UX-KEY-017"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    const KeyboardNavigation navigation;
    if (id == "UX-KEY-007") {
        c.openFirst();
        const auto document = c.editor.currentSettings();
        const auto source = c.editor.sourcePath();
        for (int repeat = 0; repeat < 3; ++repeat) {
            d.key(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
            d.wait([&] { return c.window->visibility() == QWindow::FullScreen; },
                   "fullscreen shortcut entered fullscreen");
            d.key(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
            d.wait([&] { return c.window->visibility() == QWindow::Windowed; },
                   "fullscreen shortcut returned to windowed mode");
        }
        require(c.editor.currentSettings() == document && c.editor.sourcePath() == source,
                "Fullscreen navigation changed the current photo or edits");
        return true;
    }
    if (id == "UX-KEY-011") {
        c.openFirst();
        c.saved();
        d.click("settingsButton");
        d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
               "cancellable preferences opened");
        d.key(Qt::Key_Escape);
        d.wait([&] { return !d.object("preferencesPanel")->property("visible").toBool(); },
               "Escape closed cancellable preferences");
        c.reveal("develop.exposureSlider");
        const ReleaseWrites release{c.storageFault};
        c.storageFault.holdWrites(true);
        d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
        d.wait([&] { return c.storageFault.writeBlocked(); }, "actual autosave WAL write held");
        auto calls = std::make_shared<int>(0);
        // Public native updater callback boundary; no download or installation occurs.
        c.updates.postponeInstallation([calls] { ++*calls; });
        d.wait([&] { return d.object("restart.wait")->property("opened").toBool(); },
               "mandatory save wait opened");
        for (int repeat = 0; repeat < 3; ++repeat)
            d.key(Qt::Key_Escape);
        const auto samples = c.metrics.json()["rss_samples"].toInteger();
        d.wait([&] { return c.metrics.json()["rss_samples"].toInteger() >= samples + 2; },
               "main loop live during mandatory save wait");
        require(d.object("restart.wait")->property("visible").toBool() &&
                    c.updates.restartPending() && c.editor.saving() && *calls == 0,
                "Escape dismissed mandatory save wait or allowed premature installation");
        c.storageFault.holdWrites(false);
        d.wait([&] { return *calls == 1 && !c.editor.saving(); }, "save released installer once");
        const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
        require(active && active->edits == c.editor.currentSettings(),
                "Installer callback preceded durable edits");
        return true;
    }
    if (id == "UX-KEY-016") {
        c.openFirst();
        c.reveal("develop.exposureSlider");
        tabTo(c, "develop.exposureSlider");
        auto *scroll = d.item("develop.adjustmentsScroll");
        auto inside = [&](QQuickItem *item) {
            for (auto *parent = item; parent; parent = parent->parentItem())
                if (parent == scroll)
                    return true;
            return false;
        };
        require(inside(c.window->activeFocusItem()), "Keyboard fixture did not focus adjustments");
        for (const auto modifiers : {Qt::NoModifier, Qt::ShiftModifier}) {
            bool escaped = false;
            for (int step = 0; step < 160; ++step) {
                d.key(Qt::Key_Tab, modifiers);
                auto *focused = c.window->activeFocusItem();
                require(focused && focused->isVisible() && focused->isEnabled(),
                        "ScrollView traversal lost usable focus");
                if (!inside(focused)) {
                    escaped = true;
                    break;
                }
            }
            require(escaped, "Tab/Shift+Tab trapped focus inside the adjustments ScrollView");
            tabTo(c, "develop.exposureSlider");
        }
        return true;
    }
    c.import(10);
    if (id == "UX-KEY-008") {
        const auto first = cycle(c);
        const auto second = cycle(c);
        require(first == second, "Repeated Tab navigation changed order without a state change");
        for (const auto *name : {"main.libraryButton", "main.developButton", "settingsButton",
                                 "library.importButton", "library.search", "library.selectAll"})
            require(first.contains(d.item(name)),
                    "Primary control is absent from Tab order: " + QString(name));
        return true;
    }
    if (id == "UX-KEY-009") {
        d.click("main.libraryButton");
        auto *button = d.item("settingsButton");
        const auto before = pixels(c, button);
        tabTo(c, "settingsButton");
        require(button->property("visualFocus").toBool(),
                "Keyboard-focused button has no visualFocus state");
        const auto after = pixels(c, button);
        require(before.size() == after.size() && before != after,
                "Keyboard focus did not change the button's rendered appearance");
        before.save(c.root + "/focus-unfocused.png");
        after.save(c.root + "/focus-focused.png");
        return true;
    }
    if (id == "UX-KEY-017") {
        tabTo(c, "settingsButton");
        d.key(Qt::Key_Space);
        d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
               "focus return popup opened");
        tabTo(c, "languageChoice");
        d.key(Qt::Key_Escape);
        d.wait([&] { return !d.object("preferencesPanel")->property("visible").toBool(); },
               "focus return popup closed");
        require(d.item("settingsButton")->hasActiveFocus(),
                "Closing preferences did not restore focus to its trigger");
        d.key(Qt::Key_Space);
        d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
               "restored trigger remains keyboard operable");
        d.key(Qt::Key_Escape);
        return true;
    }
    d.click("main.libraryButton");
    tabTo(c, "library.selectAll");
    auto *button = d.object("library.selectAll");
    QSignalSpy clicked(button, SIGNAL(clicked()));
    require(clicked.isValid(), "Button click signal is unavailable");
    bool activated = true;
    for (const auto key : {Qt::Key_Space, Qt::Key_Return, Qt::Key_Enter}) {
        const auto before = clicked.count();
        d.key(key);
        d.wait([&] { return !c.library.selecting(); }, "keyboard select-all settled");
        d.log({{"action", "button_key_activation"},
               {"key", static_cast<int>(key)},
               {"clicked_before", before},
               {"clicked_after", clicked.count()},
               {"selected_count", c.library.selectedCount()},
               {"active_focus", identity(c.window->activeFocusItem())}});
        activated = activated && clicked.count() == before + 1 && c.library.selectedCount() == 10;
    }
    require(activated, "Enter/Space did not activate the focused button exactly once");
    const auto beforeRepeat = clicked.count();
    d.log({{"action", "button_enter_auto_repeat"}});
    QTest::keyPress(c.window, Qt::Key_Return);
    for (int repeat = 0; repeat < 5; ++repeat) {
        QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, "\r", true, 1);
        QCoreApplication::sendEvent(c.window, &event);
    }
    QTest::keyRelease(c.window, Qt::Key_Return);
    d.wait([&] { return !c.library.selecting(); }, "held Return selection settled");
    require(clicked.count() == beforeRepeat + 1, "Held Enter generated duplicate button actions");
    const auto beforeOtherControls = clicked.count();
    d.type("library.search", "Photo-0000");
    d.wait([&] { return !c.library.loading() && c.library.count() == 1; },
           "text input focus fixture filtered");
    d.key(Qt::Key_Return);
    require(clicked.count() == beforeOtherControls &&
                d.object("library.search")->property("text").toString() == "Photo-0000",
            "Enter in a text field activated an unrelated button");
    d.choose("library.ratingFilter", 1);
    d.wait([&] { return !c.library.loading() && c.library.count() == 0; },
           "ComboBox Enter selected its own rating filter");
    require(clicked.count() == beforeOtherControls &&
                d.object("library.ratingFilter")->property("currentIndex").toInt() == 1,
            "Enter shortcut stole ComboBox popup input");
    d.click("library.newCollection");
    d.wait([&] { return d.object("library.collectionDialog")->property("opened").toBool(); },
           "modal focused-button fixture opened");
    tabTo(c, "library.collectionCancel");
    QSignalSpy cancelled(d.object("library.collectionCancel"), SIGNAL(clicked()));
    require(cancelled.isValid(), "Dialog Cancel click signal is unavailable");
    d.log({{"action", "modal_button_key_fixture"},
           {"root_active", c.window->isActive()},
           {"focus", identity(c.window->activeFocusItem())},
           {"cancel_focus", d.item("library.collectionCancel")->hasActiveFocus()}});
    d.key(Qt::Key_Return);
    d.wait([&] { return !d.object("library.collectionDialog")->property("visible").toBool(); },
           "Return activated focused modal Cancel");
    require(cancelled.count() == 1 && clicked.count() == beforeOtherControls &&
                luma::Catalog(c.catalogFile.toStdString()).collections().empty(),
            "Return activated the wrong modal action or repeated its action");
    return true;
}
} // namespace ux
