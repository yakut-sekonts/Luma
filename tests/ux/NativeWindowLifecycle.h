#pragma once
#include <QJsonObject>
#include <memory>
class QQuickWindow;
namespace ux {
// Passive failure evidence; never activates a window or changes OS settings.
QJsonObject nativeActivationSnapshot(QQuickWindow &window);
// Test-only AppKit window actions and independently observed native notifications.
class NativeWindowLifecycle final {
  public:
    explicit NativeWindowLifecycle(QQuickWindow &window);
    ~NativeWindowLifecycle();
    NativeWindowLifecycle(const NativeWindowLifecycle &) = delete;
    NativeWindowLifecycle &operator=(const NativeWindowLifecycle &) = delete;
    QJsonObject state() const;
    void minimize();
    void restore();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ux
