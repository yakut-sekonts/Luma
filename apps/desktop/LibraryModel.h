#pragma once
#include "apps/desktop/LocalizedText.h"
#include "apps/shared/LibraryService.h"
#include "core/jobs/JobManager.h"
#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <deque>
#include <map>

class LibraryModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QVariantList importMetadataPresets READ importMetadataPresets NOTIFY stateChanged)
    Q_PROPERTY(int count READ count NOTIFY stateChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY stateChanged)
    Q_PROPERTY(bool selecting READ selecting NOTIFY stateChanged)
    Q_PROPERTY(bool working READ working NOTIFY stateChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString maintenanceStatus READ maintenanceStatus NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString importReport READ importReport NOTIFY stateChanged)
    Q_PROPERTY(QVariantList collections READ collections NOTIFY stateChanged)
    Q_PROPERTY(bool semanticEnabled READ semanticEnabled NOTIFY stateChanged)
    Q_PROPERTY(bool semanticBusy READ semanticBusy NOTIFY stateChanged)
    Q_PROPERTY(double semanticProgress READ semanticProgress NOTIFY stateChanged)
    Q_PROPERTY(QString semanticStatus READ semanticStatus NOTIFY stateChanged)
    Q_PROPERTY(QString semanticError READ semanticError NOTIFY stateChanged)
    Q_PROPERTY(QVariantList semanticResults READ semanticResults NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap descriptiveMetadata READ descriptiveMetadata NOTIFY metadataChanged)
    Q_PROPERTY(int backupSchedule READ backupSchedule WRITE setBackupSchedule NOTIFY stateChanged)
    Q_PROPERTY(int backupKeep READ backupKeep WRITE setBackupKeep NOTIFY stateChanged)
    Q_PROPERTY(bool autoWriteXMP READ autoWriteXMP WRITE setAutoWriteXMP NOTIFY stateChanged)
    Q_PROPERTY(QUrl backupFolder READ backupFolder CONSTANT)
  public:
    enum Role {
        AssetID = Qt::UserRole + 1,
        Filename,
        Path,
        Thumbnail,
        Rating,
        Flag,
        Label,
        Camera,
        Selected,
        Active,
        Missing
    };
    static constexpr int pageSize = 128, maximumPages = 16;
    explicit LibraryModel(std::filesystem::path catalog, QObject *parent = nullptr);
    ~LibraryModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const {
        return count_;
    }
    int selectedCount() const {
        return selected_.size();
    }
    bool working() const {
        return importing_ || writing_ || closing_ || externalBusy_ || semanticBusy_;
    }
    bool selecting() const {
        return selecting_;
    }
    void setExternalBusy(bool busy);
    QVariantMap descriptiveMetadata() const {
        return descriptiveMetadata_;
    }
    int backupSchedule() const {
        return backupSchedule_;
    }
    int backupKeep() const {
        return backupKeep_;
    }
    bool autoWriteXMP() const {
        return autoWriteXMP_;
    }
    void setBackupSchedule(int value);
    void setBackupKeep(int value);
    void setAutoWriteXMP(bool value);
    QUrl backupFolder() const;
    Q_INVOKABLE void loadMetadata(const QString &id);
    Q_INVOKABLE void saveDescriptiveMetadata(const QVariantMap &values);
    Q_INVOKABLE void importDescriptiveMetadata();
    Q_INVOKABLE void writeMetadataSidecar();
    Q_INVOKABLE void syncSidecars();
    Q_INVOKABLE void backupNow();
    Q_INVOKABLE void restoreCatalog(const QUrl &backup, const QUrl &destination);
    Q_INVOKABLE void openCatalog(const QUrl &file, bool create = false);
    Q_INVOKABLE void locateOriginal(const QUrl &file);
    Q_INVOKABLE void locateFolder(const QUrl &folder, bool nearby);
    std::vector<std::string> selectionIDs() const;
    Q_INVOKABLE QStringList selectedAssetIDs() const {
        QStringList result;
        for (const auto &id : selectionIDs())
            result.append(QString::fromStdString(id));
        return result;
    }
    bool importing() const {
        return importing_;
    }
    bool loading() const {
        return loading_;
    }
    double progress() const {
        return progress_;
    }
    QString status() const {
        return status_.text();
    }
    QString maintenanceStatus() const {
        return maintenanceStatus_.text();
    }
    QString error() const {
        return error_;
    }
    QString importReport() const {
        return importReport_;
    }
    QVariantList collections() const {
        return collections_;
    }
    bool semanticEnabled() const {
        return semanticEnabled_;
    }
    bool semanticBusy() const {
        return semanticBusy_;
    }
    double semanticProgress() const {
        return semanticProgress_;
    }
    QString semanticStatus() const {
        return semanticStatus_.text();
    }
    QString semanticError() const {
        return semanticError_;
    }
    QVariantList semanticResults() const {
        return semanticResults_;
    }
    Q_INVOKABLE void buildSemanticIndex();
    Q_INVOKABLE void searchSemantic(const QString &query);
    Q_INVOKABLE void cancelSemantic();
    Q_INVOKABLE void disableSemantic();
    std::size_t cachedPages() const {
        return pages_.size();
    }
    void photoOpened(const QString &id);
    void refreshThumbnails(const QString &id = {});
    Q_INVOKABLE void setViewportAnchor(const QString &assetID) {
        viewportAnchor_ = assetID;
    }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void filter(const QString &search, int rating, int flag, const QString &label,
                            const QString &collection);
    Q_INVOKABLE void select(int row, bool toggle = false, bool range = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void rate(int value);
    Q_INVOKABLE void flag(int value);
    Q_INVOKABLE void label(const QString &value);
    Q_INVOKABLE void editSmartCollection(const QString &id, const QString &name);
    Q_INVOKABLE void saveSmartCollection(const QString &id, const QString &name,
                                         const QString &revision, const QVariantMap &rule);
    Q_INVOKABLE void createCollection(const QString &name);
    Q_INVOKABLE void renameCollection(const QString &id, const QString &name);
    Q_INVOKABLE void deleteCollection(const QString &id);
    Q_INVOKABLE void collectionMembership(const QString &id, bool add);
    QVariantList importMetadataPresets() const;
    Q_INVOKABLE bool saveImportMetadataPreset(const QString &name, const QVariantMap &values);
    Q_INVOKABLE bool removeImportMetadataPreset(const QString &name);
    Q_INVOKABLE bool importAdvanced(const QList<QUrl> &sources, const QVariantMap &settings,
                                    const QString &developPreset);
    Q_INVOKABLE void importFiles(const QList<QUrl> &sources, bool copy, const QUrl &destination,
                                 bool recursive, bool skipDuplicates);
    Q_INVOKABLE void cancelImport();
    Q_INVOKABLE bool requestClose();
  signals:
    void stateChanged();
    void reloadStarted(bool preserveViewport);
    void viewportRestored(int row);
    void openRequested(const QUrl &url);
    void closeReady();
    void metadataChanged();
    void smartCollectionReady(const QString &id, const QString &name, const QString &revision,
                              const QVariantMap &rule);

  private:
    bool startImport(std::vector<std::filesystem::path> sources, luma::ImportOptions options);
    std::filesystem::path catalog_;
    luma::JobManager reads_, writes_, imports_;
    luma::JobManager semanticJobs_;
    luma::JobHandle semanticJob_;
    std::uint64_t semanticRevision_ = 0;
    bool semanticEnabled_ = false, semanticBusy_ = false;
    double semanticProgress_ = 0;
    LocalizedText semanticStatus_;
    QString semanticError_;
    QVariantList semanticResults_;
    luma::CancellationSource importCancellation_;
    // Import cancellation must not cancel later backup, metadata or relink work.
    luma::CancellationSource maintenanceCancellation_;
    luma::JobHandle countJob_, selectionJob_;
    std::map<int, luma::JobHandle> pageJobs_;
    std::map<int, std::vector<luma::LibraryItem>> pages_;
    std::deque<int> lru_;
    QSet<QString> selected_;
    QString activeID_, viewportAnchor_;
    luma::LibraryFilter filter_;
    QTimer filterTimer_;
    QVariantList collections_;
    int count_ = 0, anchor_ = -1;
    std::uint64_t revision_ = 0, selectionRevision_ = 0, thumbnailGeneration_ = 0;
    bool externalBusy_ = false;
    bool importing_ = false, writing_ = true, closing_ = false, loading_ = false,
         selecting_ = false;
    double progress_ = 0;
    LocalizedText status_ = QStringLiteral("Opening catalog…");
    LocalizedText maintenanceStatus_;
    QString error_, importReport_;
    QVariantMap descriptiveMetadata_;
    std::uint64_t metadataRevision_ = 0;
    int backupSchedule_ = 2, backupKeep_ = 10;
    bool autoWriteXMP_ = false;
    QFileSystemWatcher sourceWatcher_;
    QTimer sourceWatchTimer_;
    luma::JobHandle sourceAvailabilityJob_;
    void initializeMaintenance();
    void watchVisibleFolders();
    void refreshSourceAvailability();
    void page(int number);
    void reload(bool clear);
    void selectionChanged();
    void selectRange(int offset, int length, bool toggle);
    void mutate(std::function<void(luma::Catalog &)> work, std::function<bool()> updateView = {},
                bool syncXMP = false);
    bool refreshMetadata(const std::vector<std::string> &ids, int role, const QVariant &value);
    void checkClose();
};
