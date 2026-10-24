#pragma once
#include "core/jobs/JobManager.h"
#include <QObject>
#include <QString>
#include <QUrl>
#include <memory>

class QLockFile;

// Owns the desktop lock until all catalog workers have been destroyed. The
// startup window is temporary and is gone before the editor is constructed.
class CatalogStartup final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString catalogPath READ catalogPath NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool recoveryPending READ recoveryPending NOTIFY changed)
    Q_PROPERTY(bool quitting READ quitting NOTIFY changed)
    Q_PROPERTY(QUrl backupFolder READ backupFolder NOTIFY changed)
  public:
    explicit CatalogStartup(QObject *parent = nullptr);
    ~CatalogStartup() override;
    bool prepare(const QString &path, bool allowCreate, bool interactive);
    QString catalogPath() const {
        return catalogPath_;
    }
    QString error() const {
        return error_;
    }
    bool busy() const {
        return busy_;
    }
    bool recoveryPending() const {
        return recoveryPending_;
    }
    bool quitting() const {
        return quitting_;
    }
    bool restorePhoto() const {
        return restorePhoto_;
    }
    QUrl backupFolder() const;
    void markCleanExit() {
        cleanExit_ = true;
    }
    static QString rememberedCatalog();

    Q_INVOKABLE void openCatalog(const QUrl &path);
    Q_INVOKABLE void createCatalog(const QUrl &path);
    Q_INVOKABLE void restoreBackup(const QUrl &source, const QUrl &destination);
    Q_INVOKABLE void continueSession(bool restorePhoto);
    Q_INVOKABLE void quit();
  signals:
    void changed();
    void finished(bool accepted);

  private:
    enum class Operation { open, create, restore, initial };
    QString catalogPath_, error_, markerPath_;
    std::unique_ptr<QLockFile> lock_;
    luma::JobManager jobs_;
    luma::JobHandle job_;
    luma::CancellationSource cancellation_;
    bool busy_ = false, recoveryPending_ = false, quitting_ = false;
    bool restorePhoto_ = true, cleanExit_ = false, ownsMarker_ = false;
    void acquire(const QString &path);
    void start(const QString &path, Operation operation, const QString &backup = {});
    void complete(const QString &error, bool interrupted);
};
