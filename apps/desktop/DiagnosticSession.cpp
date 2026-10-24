#include "apps/desktop/DiagnosticSession.h"
#include "platform/macos/CrashCapture.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUuid>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace luma::desktop {
namespace {
constexpr qint64 logLimit = 256 * 1024;
constexpr std::size_t queueLimit = 512;
constexpr int retainedSessions = 12;
QString levelName(LogLevel level) {
    switch (level) {
    case LogLevel::trace:
        return QStringLiteral("trace");
    case LogLevel::debug:
        return QStringLiteral("debug");
    case LogLevel::info:
        return QStringLiteral("info");
    case LogLevel::warning:
        return QStringLiteral("warning");
    case LogLevel::error:
        return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}
QString eventName(LogEvent event) {
    switch (event) {
    case LogEvent::sessionStarted:
        return QStringLiteral("session.started");
    case LogEvent::sessionFinished:
        return QStringLiteral("session.finished");
    case LogEvent::sessionFailed:
        return QStringLiteral("session.failed");
    case LogEvent::catalogReady:
        return QStringLiteral("catalog.ready");
    case LogEvent::recoveryRequested:
        return QStringLiteral("catalog.recovery");
    case LogEvent::crashCaptureUnavailable:
        return QStringLiteral("crash_capture.unavailable");
    case LogEvent::historyUnavailable:
        return QStringLiteral("diagnostics.history_unavailable");
    case LogEvent::operationStarted:
        return QStringLiteral("operation.started");
    case LogEvent::operationFinished:
        return QStringLiteral("operation.finished");
    case LogEvent::operationFailed:
        return QStringLiteral("operation.failed");
    }
    return QStringLiteral("unknown");
}
QString workflowName(LoggedWorkflow workflow) {
    switch (workflow) {
    case LoggedWorkflow::application:
        return QStringLiteral("application");
    case LoggedWorkflow::library:
        return QStringLiteral("library");
    case LoggedWorkflow::editor:
        return QStringLiteral("editor");
    case LoggedWorkflow::exportQueue:
        return QStringLiteral("export");
    case LoggedWorkflow::count:
        break;
    }
    return QStringLiteral("unknown");
}
bool append(int fd, const QByteArray &bytes) {
    qsizetype written = 0;
    while (written < bytes.size()) {
        const auto count = ::write(fd, bytes.constData() + written,
                                   static_cast<std::size_t>(bytes.size() - written));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return false;
        written += static_cast<qsizetype>(count);
    }
    return true;
}
} // namespace
DiagnosticSession::DiagnosticSession(bool enabled) {
    if (!enabled)
        return;
    try {
        const auto base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (base.isEmpty())
            return;
        const auto root = base + QStringLiteral("/diagnostics");
        if (QFileInfo(root).isSymLink() || !QDir().mkpath(root))
            return;
        if (chmod(QFile::encodeName(root).constData(), 0700) != 0)
            return;
        const auto name =
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'hhmmsszzz'Z'-")) +
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto session = root + "/" + name;
        if (!QDir().mkdir(session) || chmod(QFile::encodeName(session).constData(), 0700) != 0)
            return;
        auto lock = std::make_unique<QLockFile>(session + QStringLiteral("/session.lock"));
        lock->setStaleLockTime(0);
        if (!lock->tryLock())
            return;
        QSaveFile owner(session + QStringLiteral("/session.json"));
        owner.setDirectWriteFallback(false);
        const auto description = QJsonDocument(QJsonObject{{"schema", 1}, {"session", name}})
                                     .toJson(QJsonDocument::Compact);
        if (!owner.open(QIODevice::WriteOnly) || owner.write(description) != description.size() ||
            !owner.commit())
            return;
        root_ = root;
        session_ = session;
        lock_ = std::move(lock);
        version_ = QCoreApplication::applicationVersion();
        enabled_ = true;
        writer_ = std::thread([this] { writeLoop(); });
        record(LogLevel::info, LogEvent::sessionStarted);
        crashCaptureEnabled_ =
            macos::enableCrashCapture(std::filesystem::path(session.toStdString()));
        if (!crashCaptureEnabled_)
            record(LogLevel::warning, LogEvent::crashCaptureUnavailable);
    } catch (...) {
        // Diagnostic setup must not prevent access to photographs or a catalog.
        enabled_ = false;
    }
}
DiagnosticSession::~DiagnosticSession() {
    record(cleanExit_ ? LogLevel::info : LogLevel::warning,
           cleanExit_ ? LogEvent::sessionFinished : LogEvent::sessionFailed);
    if (writer_.joinable()) {
        {
            std::lock_guard guard(mutex_);
            stopRequested_ = true;
        }
        condition_.notify_all();
        writer_.join();
    }
}
void DiagnosticSession::record(LogLevel level, LogEvent event, LoggedWorkflow workflow) noexcept {
    if (!enabled_)
        return;
    try {
        std::lock_guard guard(mutex_);
        if (!enabled_)
            return;
        if (pending_.size() >= queueLimit) {
            ++dropped_;
            return;
        }
        pending_.push_back({level, event, workflow, QDateTime::currentMSecsSinceEpoch()});
        condition_.notify_one();
    } catch (...) {
        ++dropped_;
    }
}
void DiagnosticSession::observe(LoggedWorkflow workflow, bool busy, bool error) noexcept {
    if (!enabled_ || workflow < LoggedWorkflow::application || workflow >= LoggedWorkflow::count)
        return;
    // Workflow observation is performed on the GUI thread; writing is separate.
    auto &state = states_[static_cast<std::size_t>(workflow)];
    if (busy != state[0])
        record(LogLevel::debug, busy ? LogEvent::operationStarted : LogEvent::operationFinished,
               workflow);
    if (error && !state[1])
        record(LogLevel::error, LogEvent::operationFailed, workflow);
    state = {busy, error};
}
void DiagnosticSession::maintainHistory() {
    const QRegularExpression owned(QStringLiteral("^[0-9]{8}T[0-9]{9}Z-[0-9a-f-]{36}$"));
    const auto entries = QDir(root_).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::Reversed);
    int retained = 0;
    for (const auto &entry : entries) {
        if (entry.absoluteFilePath() == session_ || !owned.match(entry.fileName()).hasMatch())
            continue;
        QLockFile lock(entry.absoluteFilePath() + QStringLiteral("/session.lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock())
            continue; // Other catalog windows still own their logs.
        const auto marker = entry.absoluteFilePath() + QStringLiteral("/session.json");
        const QFileInfo info(marker);
        if (!info.isFile() || info.isSymLink() || info.size() > 4096)
            continue;
        QFile file(marker);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const auto data = QJsonDocument::fromJson(file.read(4097)).object();
        if (data.value("schema").toInt() != 1 ||
            data.value("session").toString() != entry.fileName())
            continue;
        file.close();
        if (retained++ < retainedSessions) {
            (void)macos::formatCrashReport(
                std::filesystem::path(entry.absoluteFilePath().toStdString()));
        } else if (!QDir(entry.absoluteFilePath()).removeRecursively()) {
            record(LogLevel::warning, LogEvent::historyUnavailable);
        }
    }
}
void DiagnosticSession::writeLoop() noexcept {
    int fd = -1;
    bool inFlight = false;
    try {
        const auto path = session_ + QStringLiteral("/events.jsonl");
        constexpr int flags = O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_NOFOLLOW | O_CLOEXEC;
        fd = open(QFile::encodeName(path).constData(), flags, 0600);
        logAvailable_ = fd >= 0;
        qint64 size = 0;
        bool historyDone = false;
        while (fd >= 0) {
            Event value;
            {
                std::unique_lock guard(mutex_);
                condition_.wait(guard, [&] { return stopRequested_ || !pending_.empty(); });
                if (pending_.empty())
                    break;
                value = pending_.front();
                pending_.pop_front();
                inFlight = true;
            }
            const auto bytes =
                QJsonDocument(QJsonObject{{"schema", 1},
                                          {"version", version_},
                                          {"utc", QDateTime::fromMSecsSinceEpoch(value.milliseconds,
                                                                                 QTimeZone::UTC)
                                                      .toString(Qt::ISODateWithMs)},
                                          {"level", levelName(value.level)},
                                          {"event", eventName(value.event)},
                                          {"workflow", workflowName(value.workflow)}})
                    .toJson(QJsonDocument::Compact) +
                '\n';
            if (size + bytes.size() > logLimit) {
                if (fsync(fd) != 0)
                    break;
                close(fd);
                fd = -1;
                const auto oldest = path + QStringLiteral(".3");
                if (QFileInfo::exists(oldest) && !QFile::remove(oldest))
                    break;
                bool rotated = true;
                for (int i = 2; i >= 0; --i) {
                    const auto previous = i == 0 ? path : path + "." + QString::number(i);
                    if (QFileInfo::exists(previous) &&
                        !QFile::rename(previous, path + "." + QString::number(i + 1))) {
                        rotated = false;
                        break;
                    }
                }
                if (!rotated)
                    break;
                fd = open(QFile::encodeName(path).constData(), flags, 0600);
                if (fd < 0)
                    break;
                size = 0;
            }
            if (!append(fd, bytes))
                break;
            size += bytes.size();
            if ((value.level == LogLevel::warning || value.level == LogLevel::error ||
                 !historyDone) &&
                fsync(fd) != 0)
                break;
            inFlight = false;
            if (!historyDone) {
                historyDone = true;
                try {
                    maintainHistory();
                } catch (...) {
                    record(LogLevel::warning, LogEvent::historyUnavailable);
                }
            }
        }
    } catch (...) { /* Preserve existing files and allow the app to continue. */
    }
    if (fd >= 0) {
        (void)fsync(fd);
        close(fd);
    }
    logAvailable_ = false;
    std::lock_guard guard(mutex_);
    enabled_ = false;
    dropped_ += pending_.size() + (inFlight ? 1 : 0);
    pending_.clear();
}
} // namespace luma::desktop
