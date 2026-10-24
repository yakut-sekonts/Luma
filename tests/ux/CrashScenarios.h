#pragma once
#include "tests/ux/UxContext.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
namespace ux {
inline QJsonObject crashExpectation(Context &c, const QString &file) {
    QFile input(c.stateRoot + '/' + file);
    require(input.open(QIODevice::ReadOnly), "Missing crash expectation");
    const auto value = QJsonDocument::fromJson(input.readAll()).object();
    require(!value.isEmpty() && value["prepare_pid"].toInteger() > 0 &&
                value["prepare_pid"].toInteger() != QCoreApplication::applicationPid(),
            "Crash recovery must use a distinct application process");
    return value;
}
[[noreturn]] inline void killCheckpoint(Context &c, const QString &id, const QString &stage,
                                        const QString &checkpoint) {
    writeJson(c.root + "/metrics.json", c.metrics.json());
    writeJson(c.root + "/kill-ready.json", {{"id", id},
                                            {"stage", stage},
                                            {"pid", QCoreApplication::applicationPid()},
                                            {"utc_ms", QDateTime::currentMSecsSinceEpoch()},
                                            {"checkpoint", checkpoint}});
    c.driver->wait([] { return false; }, "Parent SIGKILL was not delivered", 10000);
    throw std::runtime_error("Kill checkpoint unexpectedly returned");
}
} // namespace ux
