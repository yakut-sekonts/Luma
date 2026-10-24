#pragma once
#include "apps/desktop/LocalizedText.h"
#include "apps/shared/PresetStore.h"
#include <QObject>
#include <QUrl>
#include <QVariantList>

class EditorController;
class PresetController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList builtinItems READ builtinItems CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString importNotice READ importNotice NOTIFY stateChanged)
    Q_PROPERTY(QStringList importUnsupported READ importUnsupported NOTIFY stateChanged)
    Q_PROPERTY(QString importedId READ importedId NOTIFY stateChanged)
    Q_PROPERTY(bool hasCopiedSettings READ hasCopiedSettings NOTIFY stateChanged)
  public:
    PresetController(QString path, EditorController &editor, QObject *parent = nullptr);
    QVariantList items() const;
    QVariantList builtinItems() const;
    QString error() const {
        return error_;
    }
    QString importNotice() const {
        return importNotice_.text();
    }
    QStringList importUnsupported() const {
        return importUnsupported_;
    }
    QString importedId() const {
        return importedId_;
    }
    bool hasCopiedSettings() const {
        return copied_.has_value();
    }
    luma::DevelopPreset snapshot(const QString &id) const {
        return find(id);
    }
    std::optional<luma::DevelopPreset> copiedSettings() const {
        return copied_;
    }
    Q_INVOKABLE QString importSnapshot(const QString &id);
    Q_INVOKABLE bool refresh();
    Q_INVOKABLE bool save(const QString &id, const QString &name, const QString &folder, int groups,
                          bool captureCurrent);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool importPreset(const QUrl &url);
    Q_INVOKABLE bool exportPreset(const QString &id, const QUrl &url);
    Q_INVOKABLE void apply(const QString &id);
    Q_INVOKABLE void preview(const QString &id);
    Q_INVOKABLE void clearPreview();
    Q_INVOKABLE void copySettings(int groups);
    Q_INVOKABLE void pasteSettings();
  signals:
    void itemsChanged();
    void stateChanged();

  private:
    QString path_;
    luma::PresetStore store_;
    EditorController &editor_;
    std::vector<luma::StoredPreset> presets_;
    std::optional<luma::DevelopPreset> copied_;
    QString error_;
    LocalizedText importNotice_;
    QStringList importUnsupported_;
    QString importedId_;
    const luma::DevelopPreset &find(const QString &id) const;
    bool failure(const std::exception &error);
};
