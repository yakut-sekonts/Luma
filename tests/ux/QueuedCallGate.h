#pragma once
#include <QEvent>
#include <QObject>
#include <exception>
#include <functional>

namespace ux {
// Hold one queued delivery at a public QObject boundary while real UI events run.
// Never propagate a test exception through Qt event dispatch.
class QueuedCallGate final : public QObject {
  public:
    QueuedCallGate(QObject &target, std::function<bool()> predicate, std::function<void()> action)
        : target_(target), predicate_(std::move(predicate)), action_(std::move(action)) {
        target_.installEventFilter(this);
    }
    ~QueuedCallGate() override {
        target_.removeEventFilter(this);
    }
    bool caught = false;
    std::exception_ptr failure;

  private:
    QObject &target_;
    std::function<bool()> predicate_;
    std::function<void()> action_;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == &target_ && event->type() == QEvent::MetaCall && !caught && predicate_()) {
            caught = true;
            try {
                action_();
            } catch (...) {
                failure = std::current_exception();
            }
        }
        return false;
    }
};
} // namespace ux
