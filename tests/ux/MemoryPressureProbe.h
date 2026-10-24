#pragma once
#ifndef LUMA_UX_MEMORY_PRESSURE_PROBE
#error "Memory pressure injection is restricted to the UX executable"
#endif
#include "apps/desktop/EditorController.h"
#include <QCoreApplication>
#include <QEvent>
#include <QThread>
#include <stdexcept>

namespace ux {
class MemoryPressureProbe final {
  public:
    static void useControlledNotifications(EditorController &editor) {
        if (QThread::currentThread() != editor.thread() || editor.hasPhoto())
            throw std::logic_error("Controlled pressure requires an empty owned test controller");
        // Stop and drain OS callbacks before starting this bounded injection
        // scenario. Host pressure must not interleave with its prescribed input.
        editor.memoryPressure_.reset();
        QCoreApplication::sendPostedEvents(&editor, QEvent::MetaCall);
        editor.handleMemoryPressure(luma::macos::MemoryPressure::normal);
    }
    static luma::macos::MemoryPressure current(const EditorController &editor) {
        return editor.pressure_;
    }
    // Deliver at the same GUI handler boundary as the platform callback. This
    // exercises application recovery, not OS pressure generation or RSS limits.
    static void deliver(EditorController &editor, luma::macos::MemoryPressure pressure) {
        if (QThread::currentThread() != editor.thread())
            throw std::logic_error("Memory pressure must reach the editor GUI thread");
        editor.handleMemoryPressure(pressure);
    }
};
} // namespace ux
