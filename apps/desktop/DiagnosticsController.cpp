#include "apps/desktop/DiagnosticsController.h"
#include "apps/desktop/DiagnosticSession.h"
#include "apps/desktop/EditorController.h"
#include "core/catalog/SqliteStatement.h"
#include "platform/macos/SystemDiagnostics.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSysInfo>
#include <QTemporaryFile>
#include <sqlite3.h>
#include <unistd.h>

namespace {
int schemaVersion(const std::filesystem::path &path) {
    sqlite3 *db = nullptr;
    const int opened =
        sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    struct Connection {
        sqlite3 *db;
        ~Connection() {
            if (db)
                sqlite3_close(db);
        }
    } connection{db};
    if (opened != SQLITE_OK)
        throw std::runtime_error("Cannot read catalog diagnostics.");
    sqlite3_busy_timeout(db, 1000);
    luma::detail::Statement query(db, "PRAGMA user_version");
    if (!query.row())
        throw std::runtime_error("Cannot read catalog diagnostics.");
    return static_cast<int>(query.integer(0));
}
QVariantMap cache(const luma::CacheStatistics &value) {
    return {{"usedBytes", QVariant::fromValue(static_cast<qulonglong>(value.usedBytes))},
            {"budgetBytes", QVariant::fromValue(static_cast<qulonglong>(value.budgetBytes))},
            {"entries", QVariant::fromValue(static_cast<qulonglong>(value.entries))}};
}
void writeReport(const QString &destination, const QVariantMap &report) {
    if (!destination.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        throw std::runtime_error("Choose a new JSON filename for the diagnostic report.");
    const QFileInfo target(destination);
    QTemporaryFile file(target.absolutePath() + QStringLiteral("/.luma-diagnostics-XXXXXX"));
    const auto bytes = QJsonDocument(QJsonObject::fromVariantMap(report)).toJson();
    if (bytes.size() > 65536 || !file.open() || file.write(bytes) != bytes.size() ||
        !file.flush() || fsync(file.handle()) != 0)
        throw std::runtime_error("Cannot save the diagnostic report.");
    // No replacement, including a file created after the save dialog closed.
    if (link(QFile::encodeName(file.fileName()).constData(),
             QFile::encodeName(target.absoluteFilePath()).constData()) != 0)
        throw std::runtime_error(
            "Cannot save the diagnostic report. Choose a new writable filename.");
}
} // namespace
DiagnosticsController::DiagnosticsController(std::filesystem::path catalog,
                                             EditorController &editor,
                                             luma::desktop::DiagnosticSession &session,
                                             QObject *parent)
    : QObject(parent), catalog_(std::move(catalog)), editor_(editor), session_(session) {}
DiagnosticsController::~DiagnosticsController() {
    jobs_.shutdown();
}
QUrl DiagnosticsController::localFolder() const {
    return session_.folder();
}
void DiagnosticsController::refresh() {
    collect({});
}
void DiagnosticsController::exportReport(const QUrl &file) {
    if (file.isLocalFile())
        collect(file.toLocalFile());
}
void DiagnosticsController::collect(const QString &destination) {
    if (busy_)
        return;
    busy_ = true;
    error_.clear();
    savedPath_.clear();
    emit changed();
    const auto caches = editor_.cacheStatistics();
    const auto version = QCoreApplication::applicationVersion();
    const bool metal = editor_.metalPreview();
    const bool denoiseConfigured = editor_.denoiseModelConfigured();
    const bool skyConfigured = editor_.skyModelConfigured();
    const QVariantMap localDiagnostics{
        {"loggingAvailable", session_.loggingAvailable()},
        {"crashCaptureEnabled", session_.crashCaptureEnabled()},
        {"droppedLogEvents",
         QVariant::fromValue(static_cast<qulonglong>(session_.droppedEvents()))}};
    const auto catalog = catalog_;
    const QPointer<DiagnosticsController> self(this);
    jobs_.submit(luma::JobPriority::background, [self, catalog, caches, version, metal,
                                                 denoiseConfigured, skyConfigured, localDiagnostics,
                                                 destination](const auto &) {
        QVariantMap report;
        QString error;
        try {
            const auto system = luma::macos::systemDiagnostics();
            report = {{"reportSchema", 1},
                      {"collectedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                      {"applicationVersion", version},
                      {"imageEngine", QStringLiteral("Luma scene-linear RGB")},
                      {"qtVersion", QString::fromLatin1(qVersion())},
                      {"sqliteVersion", QString::fromUtf8(sqlite3_libversion())},
                      {"catalogSchema", schemaVersion(catalog)},
                      {"os", QSysInfo::prettyProductName()},
                      {"architecture", QSysInfo::currentCpuArchitecture()},
                      {"cpu", QString::fromStdString(system.cpu)},
                      {"logicalCores", system.logicalCores},
                      {"gpu", QString::fromStdString(system.gpu)},
                      {"metalAvailable", system.metalAvailable},
                      {"metalPreviewRequested", metal},
                      {"physicalMemoryBytes",
                       QVariant::fromValue(static_cast<qulonglong>(system.physicalMemory))},
                      {"residentMemoryBytes",
                       QVariant::fromValue(static_cast<qulonglong>(system.residentMemory))},
                      {"decodedPreviewCache", cache(caches[0])},
                      {"fullResolutionCache", cache(caches[1])},
                      {"renderedPreviewCache", cache(caches[2])},
                      {"localDiagnostics", localDiagnostics},
                      {"denoiseModelConfigured", denoiseConfigured},
                      {"skyModelConfigured", skyConfigured}};
            if (!destination.isEmpty())
                writeReport(destination, report);
        } catch (const std::exception &failure) {
            error = QString::fromUtf8(failure.what());
        }
        if (self)
            QMetaObject::invokeMethod(
                self,
                [self, report, error, destination] {
                    if (!self)
                        return;
                    self->snapshot_ = report;
                    self->error_ = error;
                    self->savedPath_ = error.isEmpty() ? destination : QString();
                    self->busy_ = false;
                    emit self->changed();
                },
                Qt::QueuedConnection);
    });
}
