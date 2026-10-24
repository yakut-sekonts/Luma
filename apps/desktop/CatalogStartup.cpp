#include "apps/desktop/CatalogStartup.h"
#include "apps/desktop/LocalizationController.h"
#include "core/catalog/Catalog.h"
#include "core/image/Bayer.h"
#include "platform/macos/ApplicationAlert.h"
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace {
QString preferencesPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
           QStringLiteral("/startup.ini");
}
void createEmpty(const std::filesystem::path &path) {
    const int file = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (file < 0)
        throw std::runtime_error("Cannot create catalog: choose a new writable filename.");
    close(file);
    // Failed initialization preserves the file for diagnosis, never replaces it.
}
} // namespace
CatalogStartup::CatalogStartup(QObject *parent) : QObject(parent) {}
CatalogStartup::~CatalogStartup() {
    cancellation_.cancel();
    job_.cancel();
    jobs_.shutdown();
    // This object precedes the editor/library in main, so their destructors have
    // already joined their workers before a successful run loses its marker.
    if (cleanExit_ && ownsMarker_)
        QFile::remove(markerPath_);
}
QString CatalogStartup::rememberedCatalog() {
    return QSettings(preferencesPath(), QSettings::IniFormat).value("lastCatalog").toString();
}
QUrl CatalogStartup::backupFolder() const {
    return QUrl::fromLocalFile(catalogPath_ + QStringLiteral(".luma-backups"));
}
void CatalogStartup::acquire(const QString &path) {
    if (path.isEmpty())
        throw std::runtime_error("Choose a catalog file.");
    const QFileInfo file(path);
    catalogPath_ = file.absoluteFilePath();
    if (!QDir().mkpath(file.absolutePath()))
        throw std::runtime_error("Cannot create the catalog folder.");
    if (file.isSymLink() && !file.exists())
        throw std::runtime_error("The catalog link points to a missing file.");
    const auto canonical = file.exists()
                               ? file.canonicalFilePath()
                               : file.absoluteDir().canonicalPath() + "/" + file.fileName();
    if (canonical.isEmpty() || (file.exists() && !file.isFile()))
        throw std::runtime_error("Choose a catalog file.");
    auto next = std::make_unique<QLockFile>(canonical + QStringLiteral(".desktop.lock"));
    next->setStaleLockTime(0);
    if (!next->tryLock())
        throw std::runtime_error(
            "This catalog is already open in another Luma window, or its folder is not writable.");
    catalogPath_ = canonical;
    markerPath_ = canonical + QStringLiteral(".running.json");
    lock_ = std::move(next);
}
bool CatalogStartup::prepare(const QString &path, bool allowCreate, bool interactive) {
    if (!interactive) {
        // Explicit acceptance/CLI-style launches must never wait for a dialog,
        // change the user's default catalog, or create a session marker.
        try {
            acquire(path);
            luma::Catalog catalog(std::filesystem::path(catalogPath_.toStdString()));
            return true;
        } catch (const std::exception &failure) {
            std::cerr << "Cannot open catalog: " << failure.what() << '\n';
            return false;
        }
    }
    auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    const bool quitOnLastWindow = application->quitOnLastWindowClosed();
    application->setQuitOnLastWindowClosed(false);
    struct RestoreQuitPolicy {
        QGuiApplication *app;
        bool value;
        ~RestoreQuitPolicy() {
            app->setQuitOnLastWindowClosed(value);
        }
    } policy{application, quitOnLastWindow};
    std::unique_ptr<LocalizationController> localization;
    QQmlApplicationEngine engine;
    localization = std::make_unique<LocalizationController>(
        QFileInfo(path).absolutePath() + QStringLiteral("/language.json"), engine);
    engine.rootContext()->setContextProperty(QStringLiteral("startup"), this);
    engine.loadFromModule(QStringLiteral("Luma.Desktop"), QStringLiteral("CatalogStartup"));
    if (engine.rootObjects().isEmpty()) {
        luma::macos::showApplicationError("Luma", "Cannot load the catalog recovery window.");
        return false;
    }
    QEventLoop loop;
    bool accepted = false;
    const auto connection = connect(this, &CatalogStartup::finished, &loop, [&](bool value) {
        accepted = value;
        loop.quit();
    });
    QTimer::singleShot(0, this, [this, path, allowCreate] {
        start(path, allowCreate ? Operation::initial : Operation::open);
    });
    loop.exec();
    disconnect(connection);
    return accepted;
}
void CatalogStartup::start(const QString &path, Operation operation, const QString &backup) {
    if (busy_ || quitting_)
        return;
    recoveryPending_ = false;
    error_.clear();
    lock_.reset();
    try {
        acquire(path);
        const auto target = std::filesystem::path(catalogPath_.toStdString());
        const auto source = std::filesystem::path(backup.toStdString());
        const bool interrupted = QFileInfo::exists(markerPath_);
        cancellation_ = luma::CancellationSource{};
        const auto stop = cancellation_.token();
        busy_ = true;
        emit changed();
        const QPointer<CatalogStartup> self(this);
        job_ = jobs_.submit(luma::JobPriority::interactive, [self, target, source, operation,
                                                             interrupted,
                                                             stop](const luma::JobContext &) {
            QString error;
            try {
                luma::checkCancellation(stop);
                if (operation == Operation::restore) {
                    luma::Catalog::restoreBackupTo(source, target, stop);
                } else if (operation == Operation::create ||
                           (operation == Operation::initial && !std::filesystem::exists(target))) {
                    createEmpty(target);
                } else if (!std::filesystem::is_regular_file(target)) {
                    throw std::runtime_error(
                        "The catalog is missing. Reconnect its disk or choose another catalog.");
                }
                // After an interrupted session, inspect the existing SQLite
                // snapshot before opening a writable connection/migration.
                if (interrupted && operation != Operation::create)
                    luma::Catalog::verifyBackup(target, stop);
                luma::checkCancellation(stop);
                luma::Catalog catalog(target, luma::CatalogOpenMode::existingOnly);
                (void)catalog.activeAsset();
                luma::checkCancellation(stop);
            } catch (const std::exception &failure) {
                error = QString::fromUtf8(failure.what());
            }
            if (self)
                QMetaObject::invokeMethod(
                    self,
                    [self, error, interrupted] {
                        if (self)
                            self->complete(error, interrupted);
                    },
                    Qt::QueuedConnection);
        });
    } catch (const std::exception &failure) {
        complete(QString::fromUtf8(failure.what()), false);
    }
}
void CatalogStartup::complete(const QString &error, bool interrupted) {
    busy_ = false;
    error_ = error;
    if (quitting_) {
        emit changed();
        emit finished(false);
        return;
    }
    if (!error_.isEmpty()) {
        lock_.reset();
        emit changed();
        return;
    }
    recoveryPending_ = interrupted;
    emit changed();
    if (!interrupted)
        continueSession(true);
}
void CatalogStartup::continueSession(bool restorePhoto) {
    if (busy_ || quitting_ || !error_.isEmpty() || !lock_)
        return;
    const auto bytes =
        QJsonDocument(
            QJsonObject{{"schema", 1},
                        {"version", QCoreApplication::applicationVersion()},
                        {"pid", QCoreApplication::applicationPid()},
                        {"startedAtUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}})
            .toJson();
    QSaveFile marker(markerPath_);
    marker.setDirectWriteFallback(false);
    if (!marker.open(QIODevice::WriteOnly) || marker.write(bytes) != bytes.size() ||
        !marker.commit()) {
        error_ = QStringLiteral("Cannot record the catalog session. Check available disk space and "
                                "folder permissions.");
        emit changed();
        return;
    }
    ownsMarker_ = true;
    restorePhoto_ = restorePhoto;
    recoveryPending_ = false;
    QSettings settings(preferencesPath(), QSettings::IniFormat);
    settings.setValue("lastCatalog", catalogPath_);
    settings.sync();
    emit changed();
    emit finished(true);
}
void CatalogStartup::openCatalog(const QUrl &path) {
    if (path.isLocalFile())
        start(path.toLocalFile(), Operation::open);
}
void CatalogStartup::createCatalog(const QUrl &path) {
    if (path.isLocalFile())
        start(path.toLocalFile(), Operation::create);
}
void CatalogStartup::restoreBackup(const QUrl &source, const QUrl &destination) {
    if (source.isLocalFile() && destination.isLocalFile())
        start(destination.toLocalFile(), Operation::restore, source.toLocalFile());
}
void CatalogStartup::quit() {
    quitting_ = true;
    // Keep queued work runnable so that it can post the completion. Cancelling
    // a JobHandle before it starts would skip the callback and strand the UI.
    cancellation_.cancel();
    emit changed();
    if (!busy_)
        emit finished(false);
}
