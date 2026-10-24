#pragma once
#include "core/jobs/JobManager.h"
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <filesystem>

class EditorController;
namespace luma::desktop {
class DiagnosticSession;
}
class DiagnosticsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString savedPath READ savedPath NOTIFY changed)
    Q_PROPERTY(QUrl localFolder READ localFolder CONSTANT)
  public:
    DiagnosticsController(std::filesystem::path catalog, EditorController &editor,
                          luma::desktop::DiagnosticSession &session, QObject *parent = nullptr);
    ~DiagnosticsController() override;
    QVariantMap snapshot() const {
        return snapshot_;
    }
    bool busy() const {
        return busy_;
    }
    QString error() const {
        return error_;
    }
    QString savedPath() const {
        return savedPath_;
    }
    QUrl localFolder() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void exportReport(const QUrl &file);
  signals:
    void changed();

  private:
    std::filesystem::path catalog_;
    EditorController &editor_;
    luma::desktop::DiagnosticSession &session_;
    luma::JobManager jobs_;
    QVariantMap snapshot_;
    QString error_, savedPath_;
    bool busy_ = false;
    void collect(const QString &destination);
};
