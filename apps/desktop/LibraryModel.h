#pragma once
#include "apps/desktop/LocalizedText.h"
#include "apps/shared/LibraryService.h"
#include "core/jobs/JobManager.h"
#include <QAbstractListModel>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <deque>
#include <map>

class LibraryModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY stateChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY stateChanged)
    Q_PROPERTY(bool selecting READ selecting NOTIFY stateChanged)
    Q_PROPERTY(bool working READ working NOTIFY stateChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString importReport READ importReport NOTIFY stateChanged)
    Q_PROPERTY(QVariantList collections READ collections NOTIFY stateChanged)
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
        Active
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
        return importing_ || writing_ || closing_ || externalBusy_;
    }
    bool selecting() const {
        return selecting_;
    }
    void setExternalBusy(bool busy);
    std::vector<std::string> selectionIDs() const;
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
    QString error() const {
        return error_;
    }
    QString importReport() const {
        return importReport_;
    }
    QVariantList collections() const {
        return collections_;
    }
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
    Q_INVOKABLE void createCollection(const QString &name);
    Q_INVOKABLE void renameCollection(const QString &id, const QString &name);
    Q_INVOKABLE void deleteCollection(const QString &id);
    Q_INVOKABLE void collectionMembership(const QString &id, bool add);
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

  private:
    std::filesystem::path catalog_;
    luma::JobManager reads_, writes_, imports_;
    luma::CancellationSource importCancellation_;
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
    QString error_, importReport_;
    void page(int number);
    void reload(bool clear);
    void selectionChanged();
    void selectRange(int offset, int length, bool toggle);
    void mutate(std::function<void(luma::Catalog &)> work, std::function<bool()> updateView = {});
    bool refreshMetadata(const std::vector<std::string> &ids, int role, const QVariant &value);
    void checkClose();
};
