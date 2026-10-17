#pragma once
#include "tests/ux/UxMetrics.h"
#include <QFile>
#include <QQuickItem>
#include <functional>
namespace ux {
void require(bool condition, const QString &message);
class Driver {
  public:
    Driver(QQuickWindow &window, Metrics &metrics, const QString &actions);
    QObject *find(const QString &id) const;
    QObject *object(const QString &id) const;
    QQuickItem *item(const QString &id) const;
    void wait(const std::function<bool()> &condition, const QString &reason, int timeout = 30000);
    void frame();
    void click(const QString &id, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void doubleClick(const QString &id);
    void press(const QString &id, QPointF fraction = {.5, .5});
    void move(const QString &id, QPointF fraction);
    void release(const QString &id, QPointF fraction = {.5, .5});
    void drag(const QString &id, QPointF from, QPointF to, int steps = 12);
    void dragBetween(const QString &fromID, QPointF from, const QString &toID, QPointF to,
                     int steps = 12);
    void wheel(const QString &id, int pixels, bool horizontal = false);
    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void quitShortcut();
    void keyDown(Qt::Key key);
    void keyUp(Qt::Key key);
    void type(const QString &id, const QString &text);
    void choose(const QString &id, int index);
    void resize(QSize size);
    void screenshot(const QString &path);
    void log(const QJsonObject &action);
    QPoint point(const QString &id, QPointF fraction = {.5, .5}) const;
    QPointF sliderThumb(const QString &id) const;
    QQuickWindow &window() {
        return window_;
    }

  private:
    QQuickWindow &window_;
    Metrics &metrics_;
    QFile actions_;
};
} // namespace ux
