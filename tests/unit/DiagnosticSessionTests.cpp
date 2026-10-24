#include "apps/desktop/DiagnosticSession.h"
#include "platform/macos/CrashCapture.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

// Exercise the real asynchronous log writer without installing process-wide
// crash handlers. PLCrashReporter capture/formatting requires separate tests.
namespace luma::macos {
bool enableCrashCapture(const std::filesystem::path &) noexcept {
    return false;
}
bool formatCrashReport(const std::filesystem::path &) noexcept {
    return false;
}
} // namespace luma::macos

namespace {
using namespace luma::desktop;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
class OwnedLocation {
  public:
    OwnedLocation() {
        QCoreApplication::setApplicationName(QStringLiteral("Luma-DiagnosticSessionTests-") +
                                             QUuid::createUuid().toString(QUuid::WithoutBraces));
        path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        require(!path.isEmpty() && !QFileInfo::exists(path), "Test location must be new.");
    }
    ~OwnedLocation() {
        QDir(path).removeRecursively();
    }
    OwnedLocation(const OwnedLocation &) = delete;
    OwnedLocation &operator=(const OwnedLocation &) = delete;
    QString path;
};
std::vector<QJsonObject> events(const QString &root) {
    const auto sessions = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    require(sessions.size() == 1, "Expected exactly one owned diagnostic session.");
    QFile file(root + "/" + sessions.front() + QStringLiteral("/events.jsonl"));
    require(file.open(QIODevice::ReadOnly), "Session log was not written.");
    std::vector<QJsonObject> result;
    while (!file.atEnd()) {
        const auto line = file.readLine();
        require(line.endsWith('\n'), "Shutdown left a partial JSON line.");
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(line, &error);
        require(error.error == QJsonParseError::NoError && document.isObject(),
                "Invalid diagnostic JSON record.");
        result.push_back(document.object());
    }
    return result;
}
void disabledAndRejected() {
    const OwnedLocation location;
    {
        DiagnosticSession session(false);
        session.record(LogLevel::error, LogEvent::operationFailed);
        session.markCleanExit();
        require(session.folder().isEmpty() && !session.loggingAvailable(),
                "Disabled diagnostics became available.");
    }
    require(!QFileInfo::exists(location.path), "Disabled diagnostics created files.");
    QTemporaryDir target;
    require(target.isValid() && QDir().mkpath(location.path), "Cannot prepare rejection test.");
    QFile sentinel(target.filePath(QStringLiteral("sentinel")));
    require(sentinel.open(QIODevice::WriteOnly) && sentinel.write("untouched") == 9,
            "Cannot write sentinel.");
    sentinel.close();
    require(QFile::link(target.path(), location.path + QStringLiteral("/diagnostics")),
            "Cannot prepare diagnostic root symlink.");
    {
        DiagnosticSession session(true);
        require(session.folder().isEmpty() && !session.loggingAvailable(),
                "Diagnostic root symlink was accepted.");
    }
    require(QDir(target.path()).entryList(QDir::Files) == QStringList{QStringLiteral("sentinel")},
            "Rejected diagnostics modified the symlink destination.");
    require(sentinel.open(QIODevice::ReadOnly) && sentinel.readAll() == "untouched",
            "Rejected diagnostics changed the sentinel.");
}
void queuedCleanShutdown() {
    const OwnedLocation location;
    QString root;
    constexpr int pairs = 64;
    {
        DiagnosticSession session(true);
        root = session.folder().toLocalFile();
        require(!root.isEmpty(), "Cannot start diagnostic writer.");
        for (int i = 0; i < pairs; ++i) {
            session.record(LogLevel::debug, LogEvent::operationStarted, LoggedWorkflow::editor);
            session.record(LogLevel::info, LogEvent::operationFinished, LoggedWorkflow::editor);
        }
        require(session.droppedEvents() == 0,
                "Bounded diagnostic queue unexpectedly dropped data.");
        session.markCleanExit();
        // Deliberately do not wait for the writer; destruction must drain it.
    }
    const auto records = events(root);
    require(records.size() == pairs * 2 + 3, "Shutdown lost queued diagnostic events.");
    require(records.front().value("event") == "session.started", "Session start missing.");
    require(records[1].value("event") == "crash_capture.unavailable", "Crash status missing.");
    for (int i = 0; i < pairs * 2; ++i) {
        const auto &record = records[static_cast<std::size_t>(i) + 2];
        require(record.value("event") == (i % 2 ? "operation.finished" : "operation.started"),
                "Queued diagnostic events changed order.");
        require(record.value("workflow") == "editor" && record.value("version") == "test-version",
                "Diagnostic workflow/version was lost.");
    }
    require(records.back().value("event") == "session.finished" &&
                records.back().value("level") == "info",
            "Clean shutdown marker missing.");
}
void idleUncleanShutdown() {
    const OwnedLocation location;
    QString root;
    {
        DiagnosticSession session(true);
        root = session.folder().toLocalFile();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!session.loggingAvailable() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(session.loggingAvailable(), "Diagnostic writer did not become available.");
    }
    const auto records = events(root);
    require(records.size() == 3 && records.back().value("event") == "session.failed" &&
                records.back().value("level") == "warning",
            "Idle/unclean shutdown lost its final marker.");
}
} // namespace

int main(int argc, char **argv) {
    const QCoreApplication application(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationVersion(QStringLiteral("test-version"));
    try {
        disabledAndRejected();
        queuedCleanShutdown();
        idleUncleanShutdown();
        std::cout << "Diagnostic shutdown, queue order and unavailable paths passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
