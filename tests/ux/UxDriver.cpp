#include "tests/ux/UxDriver.h"
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QJsonDocument>
#include <QKeySequence>
#include <QTest>
#include <QWheelEvent>
#include <QtTest/qtestwheel.h>
#include <stdexcept>
namespace ux {
void require(bool condition, const QString &message) {
    if (!condition)
        throw std::runtime_error(message.toStdString());
}
Driver::Driver(QQuickWindow &window, Metrics &metrics, const QString &actions)
    : window_(window), metrics_(metrics), actions_(actions) {
    require(actions_.open(QIODevice::WriteOnly | QIODevice::Append), "Cannot open UX action trace");
}
QObject *Driver::find(const QString &id) const {
    if (window_.objectName() == id)
        return &window_;
    if (auto *object = window_.findChild<QObject *>(id))
        return object;
    std::function<QQuickItem *(QQuickItem *)> visit = [&](QQuickItem *parent) -> QQuickItem * {
        if (parent->objectName() == id)
            return parent;
        for (auto *child : parent->childItems())
            if (auto *found = visit(child))
                return found;
        return nullptr;
    };
    return visit(window_.contentItem());
}
QObject *Driver::object(const QString &id) const {
    auto *result = find(id);
    require(result, "Missing UI selector: " + id);
    return result;
}
QQuickItem *Driver::item(const QString &id) const {
    auto *result = qobject_cast<QQuickItem *>(object(id));
    require(result, "Selector is not a Quick item: " + id);
    return result;
}
void Driver::wait(const std::function<bool()> &condition, const QString &reason, int timeout) {
    require(QTest::qWaitFor(condition, timeout), "Timeout: " + reason);
}
void Driver::frame() {
    bool animated = false;
    QObject observer;
    QObject::connect(&window_, &QQuickWindow::afterAnimating, &observer, [&] { animated = true; });
    window_.update();
    wait([&] { return animated || !window_.isVisible(); }, "window polish cycle", 5000);
}
QPoint Driver::point(const QString &id, QPointF fraction) const {
    auto *target = item(id);
    return target->mapToScene({target->width() * fraction.x(), target->height() * fraction.y()})
        .toPoint();
}
QPointF Driver::sliderThumb(const QString &id) const {
    auto *slider = item(id);
    auto *handle = qvariant_cast<QObject *>(slider->property("handle"));
    require(handle && slider->property("orientation").toInt() == Qt::Horizontal,
            "Expected a horizontal Slider: " + id);
    const double handleWidth = handle->property("width").toDouble();
    const double travel = slider->property("availableWidth").toDouble() - handleWidth;
    require(slider->width() > 0 && travel > 0, "Slider has no handle travel: " + id);
    return {(slider->property("leftPadding").toDouble() + handleWidth / 2 +
             slider->property("visualPosition").toDouble() * travel) /
                slider->width(),
            .5};
}
void Driver::log(const QJsonObject &action) {
    const auto bytes = QJsonDocument(action).toJson(QJsonDocument::Compact) + '\n';
    require(actions_.write(bytes) == bytes.size(), "Cannot write UX action trace");
    actions_.flush();
}
void Driver::click(const QString &id, Qt::KeyboardModifiers modifiers) {
    frame();
    auto *target = item(id);
    require(target->isVisible() && target->isEnabled(), "Cannot click unavailable control: " + id);
    const auto p = point(id);
    require(window_.contentItem()->contains(p), "Control outside window: " + id);
    log({{"action", "click"}, {"id", id}, {"modifiers", static_cast<int>(modifiers)}});
    QElapsedTimer time;
    time.start();
    QTest::mouseClick(&window_, Qt::LeftButton, modifiers, p);
    QCoreApplication::processEvents();
    metrics_.input(static_cast<double>(time.nsecsElapsed()) / 1000000.0);
}
void Driver::doubleClick(const QString &id) {
    frame();
    log({{"action", "double_click"}, {"id", id}});
    QTest::mouseDClick(&window_, Qt::LeftButton, Qt::NoModifier, point(id));
    QCoreApplication::processEvents();
}
void Driver::press(const QString &id, QPointF fraction) {
    frame();
    log({{"action", "press"}, {"id", id}, {"x", fraction.x()}, {"y", fraction.y()}});
    QTest::mousePress(&window_, Qt::LeftButton, Qt::NoModifier, point(id, fraction));
}
void Driver::move(const QString &id, QPointF fraction) {
    log({{"action", "move"}, {"id", id}, {"x", fraction.x()}, {"y", fraction.y()}});
    QTest::mouseMove(&window_, point(id, fraction));
    QCoreApplication::processEvents();
}
void Driver::release(const QString &id, QPointF fraction) {
    log({{"action", "release"}, {"id", id}, {"x", fraction.x()}, {"y", fraction.y()}});
    QTest::mouseRelease(&window_, Qt::LeftButton, Qt::NoModifier, point(id, fraction));
    QCoreApplication::processEvents();
}
void Driver::drag(const QString &id, QPointF from, QPointF to, int steps) {
    press(id, from);
    for (int i = 1; i <= steps; ++i)
        move(id, from + (to - from) * (static_cast<double>(i) / steps));
    release(id, to);
}
void Driver::dragBetween(const QString &fromID, QPointF from, const QString &toID, QPointF to,
                         int steps) {
    frame();
    require(steps > 0, "Drag needs at least one move");
    const auto start = point(fromID, from);
    const auto finish = point(toID, to);
    log({{"action", "drag_between"},
         {"from_id", fromID},
         {"to_id", toID},
         {"from_x", from.x()},
         {"from_y", from.y()},
         {"to_x", to.x()},
         {"to_y", to.y()}});
    QTest::mousePress(&window_, Qt::LeftButton, Qt::NoModifier, start);
    for (int i = 1; i <= steps; ++i) {
        const auto p = QPointF(start) + QPointF(finish - start) * (static_cast<double>(i) / steps);
        QTest::mouseMove(&window_, p.toPoint());
        frame();
    }
    QTest::mouseRelease(&window_, Qt::LeftButton, Qt::NoModifier, finish);
    QCoreApplication::processEvents();
}
void Driver::wheel(const QString &id, int pixels, bool horizontal) {
    frame();
    log({{"action", "wheel"}, {"id", id}, {"pixels", pixels}, {"horizontal", horizontal}});
    const QPointF local = point(id);
    const QPoint delta = horizontal ? QPoint(pixels, 0) : QPoint(0, pixels);
    QTest::mouseMove(&window_, local.toPoint());
    QTest::wheelEvent(&window_, local, delta);
}
void Driver::key(Qt::Key key, Qt::KeyboardModifiers modifiers) {
    auto *receiver = QGuiApplication::focusWindow();
    if (!receiver)
        receiver = &window_;
    log({{"action", "key"},
         {"key", static_cast<int>(key)},
         {"modifiers", static_cast<int>(modifiers)},
         {"window_class", QString::fromUtf8(receiver->metaObject()->className())}});
    QTest::keyClick(receiver, key, modifiers);
    QCoreApplication::processEvents();
}
void Driver::quitShortcut() {
    const auto bindings = QKeySequence::keyBindings(QKeySequence::Quit);
    require(!bindings.isEmpty(), "Platform theme has no StandardKey.Quit binding");
    const bool native = QGuiApplication::platformName() == "cocoa";
    require(native || QGuiApplication::platformName() == "offscreen",
            "Quit qualification requires Cocoa or the explicit offscreen CI fixture");
    const QKeySequence commandQ(QKeyCombination(Qt::ControlModifier, Qt::Key_Q));
    if (native)
        require(bindings.contains(commandQ), "macOS Quit no longer declares Cmd+Q");
    const auto sequence = native ? commandQ : bindings.front();
    require(sequence.count() == 1, "Quit fixture requires a single key combination");
    log({{"action", "standard_quit_shortcut_input"},
         {"platform", QGuiApplication::platformName()},
         {"resolved_binding", sequence.toString(QKeySequence::PortableText)},
         {"native_command_q_exercised", native}});
    // Offscreen uses a Windows keyboard scheme (Quit=Exit). This still travels
    // through the actual QML StandardKey shortcut; never call root.close() here.
    key(sequence[0].key(), sequence[0].keyboardModifiers());
}
void Driver::keyDown(Qt::Key key) {
    log({{"action", "key_down"}, {"key", static_cast<int>(key)}});
    auto *receiver = QGuiApplication::focusWindow();
    QTest::keyPress(receiver ? receiver : &window_, key);
    QCoreApplication::processEvents();
}
void Driver::keyUp(Qt::Key key) {
    log({{"action", "key_up"}, {"key", static_cast<int>(key)}});
    auto *receiver = QGuiApplication::focusWindow();
    QTest::keyRelease(receiver ? receiver : &window_, key);
    QCoreApplication::processEvents();
}
void Driver::type(const QString &id, const QString &text) {
    click(id);
    key(Qt::Key_A, Qt::ControlModifier);
    key(Qt::Key_Backspace);
    QInputMethodEvent event;
    event.setCommitString(text);
    QCoreApplication::sendEvent(window_.activeFocusItem(), &event);
    QCoreApplication::processEvents();
    log({{"action", "text"}, {"id", id}, {"text", text}});
}
void Driver::choose(const QString &id, int index) {
    click(id);
    auto *popup = qvariant_cast<QObject *>(object(id)->property("popup"));
    require(popup, "ComboBox has no popup: " + id);
    wait([&] { return popup->property("opened").toBool(); }, "ComboBox popup ready: " + id);
    // A tall popup can open under the cursor. Move off its delegates before key navigation.
    QTest::mouseMove(&window_, {8, 8});
    frame();
    key(Qt::Key_Home);
    wait([&] { return object(id)->property("highlightedIndex").toInt() == 0; },
         "ComboBox first option acknowledged: " + id, 3000);
    for (int i = 0; i < index; ++i) {
        key(Qt::Key_Down);
        wait([&] { return object(id)->property("highlightedIndex").toInt() == i + 1; },
             "ComboBox navigation acknowledged: " + id, 3000);
    }
    log({{"action", "combo_navigation"},
         {"id", id},
         {"requested", index},
         {"highlighted", object(id)->property("highlightedIndex").toInt()},
         {"current_index", object(id)->property("currentIndex").toInt()},
         {"count", object(id)->property("count").toInt()}});
    key(Qt::Key_Return);
    wait([&] { return !popup->property("visible").toBool(); }, "ComboBox popup closed: " + id);
}
void Driver::resize(QSize size) {
    log({{"action", "resize"}, {"width", size.width()}, {"height", size.height()}});
    window_.resize(size);
    frame();
}
void Driver::screenshot(const QString &path) {
    frame();
    require(window_.grabWindow().save(path), "Cannot save screenshot: " + path);
}
} // namespace ux
