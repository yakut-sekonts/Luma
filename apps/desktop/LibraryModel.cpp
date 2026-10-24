#include "apps/desktop/LibraryModel.h"
#include "apps/shared/BackupService.h"
#include "apps/shared/MetadataService.h"
#include <QPointer>
#include <QSettings>
#include <algorithm>
#include <chrono>
#include <limits>

namespace {
QString importReport(const std::filesystem::path &catalog, const luma::ImportSummary &result) {
    QStringList lines;
    for (const auto &problem : luma::Catalog(catalog).importProblems(result.sessionID))
        lines.push_back(QString::fromStdString(problem));
    if (result.failed > 100)
        lines.push_back(QStringLiteral(
            "Showing the first 100 errors; the rest are saved in the catalog journal."));
    return lines.join(QStringLiteral("\n\n"));
}
LocalizedText summary(const luma::ImportSummary &result) {
    return LocalizedText(
               QStringLiteral("Imported: %1 · duplicates: %2 · errors: %3 · cancelled: %4"))
        .arg(result.added)
        .arg(result.duplicates)
        .arg(result.failed)
        .arg(result.cancelled);
}
} // namespace
LibraryModel::LibraryModel(std::filesystem::path catalog, QObject *parent)
    : QAbstractListModel(parent), catalog_(std::move(catalog)) {
    QSettings settings(QString::fromStdString(catalog_.parent_path().string()) + "/preview.ini",
                       QSettings::IniFormat);
    semanticEnabled_ = settings.value("semanticIndexEnabled", false).toBool();
    initializeMaintenance();
    filterTimer_.setSingleShot(true);
    filterTimer_.setInterval(160);
    connect(&filterTimer_, &QTimer::timeout, this, [this] { reload(true); });
    const QPointer<LibraryModel> self(this);
    const auto path = catalog_;
    const auto recoveryStop = maintenanceCancellation_.token();
    const bool clearSemantic = !semanticEnabled_;
    const auto backupPolicy = static_cast<luma::BackupSchedule>(backupSchedule_);
    const int backupKeep = backupKeep_;
    const bool writeXMP = autoWriteXMP_;
    writes_.submit(luma::JobPriority::background, [self, path, recoveryStop, clearSemantic,
                                                   backupPolicy, backupKeep,
                                                   writeXMP](const auto &) {
        QString error, report;
        std::vector<luma::ImportSummary> recovered;
        try {
            if (clearSemantic)
                luma::Catalog(path).clearSemanticIndex();
            recovered = luma::LibraryService(path).recover(recoveryStop);
            luma::BackupService(path).run(backupPolicy, backupKeep, false, recoveryStop);
            if (writeXMP)
                luma::MetadataService(path).writePendingSidecars(recoveryStop);
            if (!recovered.empty())
                report = ::importReport(path, recovered.back());
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        if (self)
            QMetaObject::invokeMethod(
                self,
                [self, error, recovered, report] {
                    if (!self)
                        return;
                    self->writing_ = false;
                    self->importReport_ = report;
                    self->error_ = error;
                    if (recovered.empty())
                        self->status_ = QStringLiteral("Library ready");
                    else
                        self->status_ =
                            LocalizedText(
                                QStringLiteral(
                                    "Import recovered · imported %1 · duplicates %2 · errors %3"))
                                .arg(recovered.back().added)
                                .arg(recovered.back().duplicates)
                                .arg(recovered.back().failed);
                    self->reload(false);
                    emit self->stateChanged();
                    self->checkClose();
                },
                Qt::QueuedConnection);
    });
}
LibraryModel::~LibraryModel() {
    semanticJob_.cancel();
    semanticJobs_.shutdown();
    importCancellation_.cancel();
    maintenanceCancellation_.cancel();
    imports_.shutdown();
    writes_.shutdown();
    reads_.shutdown();
}
int LibraryModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : count_;
}
QHash<int, QByteArray> LibraryModel::roleNames() const {
    return {{AssetID, "assetID"},        {Filename, "filename"},    {Path, "pathURL"},
            {Thumbnail, "thumbnailURL"}, {Rating, "rating"},        {Flag, "flagValue"},
            {Label, "colorLabel"},       {Camera, "cameraName"},    {Selected, "selected"},
            {Active, "activePhoto"},     {Missing, "sourceMissing"}};
}
QVariant LibraryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count_)
        return {};
    const int number = index.row() / pageSize;
    auto *self = const_cast<LibraryModel *>(this);
    const auto found = pages_.find(number);
    if (found == pages_.end()) {
        self->page(number);
        if (role == Rating || role == Flag)
            return 0;
        if (role == Selected || role == Active || role == Missing)
            return false;
        if (role == Path)
            return QUrl{};
        return QString{};
    }
    std::erase(self->lru_, number);
    self->lru_.push_back(number);
    const int offset = index.row() % pageSize;
    if (offset >= static_cast<int>(found->second.size()))
        return {};
    const auto &item = found->second[static_cast<std::size_t>(offset)];
    const auto id = QString::fromStdString(item.id);
    switch (role) {
    case AssetID:
        return id;
    case Filename:
        return QString::fromStdString(item.filename);
    case Path:
        return QUrl::fromLocalFile(QString::fromStdString(item.path.string()));
    case Thumbnail:
        return QStringLiteral("image://thumbnail/") + id + QStringLiteral("?generation=") +
               QString::number(thumbnailGeneration_);
    case Rating:
        return item.rating;
    case Flag:
        return item.flag;
    case Label:
        return QString::fromStdString(item.label);
    case Camera:
        return QString::fromStdString(item.camera);
    case Selected:
        return selected_.contains(id);
    case Active:
        return activeID_ == id;
    case Missing:
        return item.missing;
    default:
        return {};
    }
}
void LibraryModel::refreshThumbnails(const QString &id) {
    ++thumbnailGeneration_;
    for (const auto &[number, rows] : pages_)
        for (std::size_t offset = 0; offset < rows.size(); ++offset)
            if (id.isEmpty() || QString::fromStdString(rows[offset].id) == id) {
                const int row = number * pageSize + static_cast<int>(offset);
                emit dataChanged(index(row), index(row), {Thumbnail});
            }
}
void LibraryModel::page(int number) {
    if (closing_ || loading_ || pageJobs_.contains(number) || pageJobs_.size() >= maximumPages)
        return;
    const auto revision = revision_;
    const auto path = catalog_;
    const auto filter = filter_;
    const QPointer<LibraryModel> self(this);
    pageJobs_[number] = reads_.submit(
        luma::JobPriority::background, [self, path, filter, number, revision](const auto &context) {
            std::vector<luma::LibraryItem> rows;
            QString error;
            try {
                if (context.stopToken().isCancelled())
                    return;
                rows = luma::Catalog(path).libraryPage(filter, number * pageSize, pageSize);
                for (auto &item : rows) {
                    if (context.stopToken().isCancelled())
                        return;
                    std::error_code error;
                    item.missing = !std::filesystem::exists(item.path, error) && !error;
                }
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
            }
            if (self)
                QMetaObject::invokeMethod(
                    self,
                    [self, number, revision, rows = std::move(rows), error]() mutable {
                        if (!self || self->revision_ != revision)
                            return;
                        self->pageJobs_.erase(number);
                        if (self->pages_.size() >= maximumPages) {
                            self->pages_.erase(self->lru_.front());
                            self->lru_.pop_front();
                        }
                        self->pages_[number] = std::move(rows);
                        self->lru_.push_back(number);
                        self->watchVisibleFolders();
                        if (!error.isEmpty()) {
                            self->error_ = error;
                            emit self->stateChanged();
                        }
                        if (self->count_)
                            emit self->dataChanged(self->index(0), self->index(self->count_ - 1));
                    },
                    Qt::QueuedConnection);
        });
}
void LibraryModel::reload(bool clear) {
    if (closing_)
        return;
    emit reloadStarted(!clear);
    const auto viewportAnchor = clear ? std::string{} : viewportAnchor_.toStdString();
    ++revision_;
    ++selectionRevision_;
    selecting_ = false;
    countJob_.cancel();
    selectionJob_.cancel();
    for (const auto &[number, job] : pageJobs_) {
        (void)number;
        job.cancel();
    }
    pageJobs_.clear();
    beginResetModel();
    pages_.clear();
    lru_.clear();
    count_ = 0;
    endResetModel();
    if (clear) {
        selected_.clear();
        anchor_ = -1;
    }
    loading_ = true;
    emit stateChanged();
    const auto revision = revision_;
    const auto path = catalog_;
    const auto filter = filter_;
    const QPointer<LibraryModel> self(this);
    countJob_ = reads_.submit(luma::JobPriority::background, [self, path, filter, revision,
                                                              viewportAnchor](const auto &context) {
        std::int64_t count = 0;
        int anchorRow = -1;
        QVariantList collections;
        QString error;
        try {
            if (context.stopToken().isCancelled())
                return;
            luma::Catalog db(path);
            count = db.libraryCount(filter);
            if (count > std::numeric_limits<int>::max())
                throw std::runtime_error("Catalog exceeds the Qt model row limit.");
            if (const auto index = db.libraryIndex(filter, viewportAnchor);
                index && *index >= 0 && *index < count)
                anchorRow = static_cast<int>(*index);
            for (const auto &c : db.collections())
                collections.push_back(QVariantMap{{"id", QString::fromStdString(c.id)},
                                                  {"name", QString::fromStdString(c.name)},
                                                  {"smart", c.smart},
                                                  {"count", static_cast<qlonglong>(c.count)}});
        } catch (const std::exception &e) {
            count = 0;
            error = QString::fromUtf8(e.what());
        }
        if (self)
            QMetaObject::invokeMethod(
                self,
                [self, count, collections, error, revision, anchorRow] {
                    if (!self || self->revision_ != revision)
                        return;
                    self->beginResetModel();
                    self->count_ = static_cast<int>(count);
                    self->collections_ = collections;
                    // Views may request their first metadata page synchronously
                    // from modelReset. Publish readiness before that signal.
                    self->loading_ = false;
                    self->endResetModel();
                    if (!error.isEmpty())
                        self->error_ = error;
                    emit self->viewportRestored(error.isEmpty() ? anchorRow : -1);
                    emit self->stateChanged();
                },
                Qt::QueuedConnection);
    });
}
void LibraryModel::refresh() {
    reload(false);
}
void LibraryModel::filter(const QString &search, int rating, int flag, const QString &label,
                          const QString &collection) {
    luma::LibraryFilter next;
    next.search = search.toStdString();
    next.minimumRating = rating;
    next.flag = flag;
    if (label != QStringLiteral("any"))
        next.label = label.toStdString();
    next.collectionID = collection.toStdString();
    try {
        next.validate();
    } catch (const std::exception &e) {
        error_ = QString::fromUtf8(e.what());
        emit stateChanged();
        return;
    }
    filter_ = std::move(next);
    filterTimer_.start();
}
void LibraryModel::photoOpened(const QString &id) {
    activeID_ = id;
    selectionChanged();
    // A photo already present in a loaded page changes only its Active role.
    // Resetting the model here destroys delegates and the user's viewport.
    const auto known = std::any_of(pages_.begin(), pages_.end(), [&](const auto &page) {
        return std::any_of(page.second.begin(), page.second.end(),
                           [&](const auto &item) { return item.id == id.toStdString(); });
    });
    // Open-file / newly generated photos still need a fresh catalog count.
    if (!known)
        reload(false);
}
void LibraryModel::selectionChanged() {
    if (count_)
        emit dataChanged(index(0), index(count_ - 1), {Selected, Active});
    emit stateChanged();
}
void LibraryModel::clearSelection() {
    if (externalBusy_)
        return;
    selectionJob_.cancel();
    ++selectionRevision_;
    selecting_ = false;
    selected_.clear();
    anchor_ = -1;
    selectionChanged();
}
void LibraryModel::select(int row, bool toggle, bool range) {
    if (row < 0 || row >= count_ || closing_ || externalBusy_)
        return;
    if (range && anchor_ >= 0) {
        selectRange(std::min(row, anchor_), std::abs(row - anchor_) + 1, toggle);
        return;
    }
    ++selectionRevision_;
    selectionJob_.cancel();
    selecting_ = false;
    const auto id = data(index(row), AssetID).toString();
    if (id.isEmpty())
        return;
    if (!toggle)
        selected_.clear();
    if (toggle && selected_.contains(id))
        selected_.remove(id);
    else
        selected_.insert(id);
    anchor_ = row;
    selectionChanged();
}
void LibraryModel::selectRange(int offset, int length, bool toggle) {
    if (selecting_ || closing_ || externalBusy_)
        return;
    selecting_ = true;
    emit stateChanged();
    const auto revision = ++selectionRevision_;
    const auto path = catalog_;
    const auto filter = filter_;
    const QPointer<LibraryModel> self(this);
    selectionJob_ =
        reads_.submit(luma::JobPriority::background,
                      [self, path, filter, offset, length, toggle, revision](const auto &) {
                          std::vector<std::string> ids;
                          QString error;
                          try {
                              ids = luma::Catalog(path).libraryIDs(filter, offset, length);
                          } catch (const std::exception &e) {
                              error = QString::fromUtf8(e.what());
                          }
                          if (self)
                              QMetaObject::invokeMethod(
                                  self,
                                  [self, ids = std::move(ids), toggle, revision, error] {
                                      if (!self || self->selectionRevision_ != revision)
                                          return;
                                      self->selecting_ = false;
                                      if (!toggle)
                                          self->selected_.clear();
                                      for (const auto &id : ids)
                                          self->selected_.insert(QString::fromStdString(id));
                                      if (!error.isEmpty())
                                          self->error_ = error;
                                      self->selectionChanged();
                                  },
                                  Qt::QueuedConnection);
                      });
}
void LibraryModel::selectAll() {
    selectRange(0, count_, false);
}
void LibraryModel::activate(int row) {
    if (externalBusy_)
        return;
    const auto url = data(index(row), Path).toUrl();
    if (!url.isEmpty())
        emit openRequested(url);
}
std::vector<std::string> LibraryModel::selectionIDs() const {
    std::vector<std::string> ids;
    ids.reserve(selected_.size());
    for (const auto &id : selected_)
        ids.push_back(id.toStdString());
    std::sort(ids.begin(), ids.end());
    return ids;
}
void LibraryModel::setExternalBusy(bool busy) {
    externalBusy_ = busy;
    emit stateChanged();
}
void LibraryModel::mutate(std::function<void(luma::Catalog &)> work,
                          std::function<bool()> updateView, bool syncXMP) {
    if (working())
        return;
    writing_ = true;
    error_.clear();
    maintenanceStatus_ = {};
    emit stateChanged();
    const auto path = catalog_;
    const QPointer<LibraryModel> self(this);
    const auto operation = std::make_shared<std::function<void(luma::Catalog &)>>(std::move(work));
    const auto refresh = std::make_shared<std::function<bool()>>(std::move(updateView));
    const bool writeXMP = autoWriteXMP_ && syncXMP;
    const auto stop = maintenanceCancellation_.token();
    writes_.submit(luma::JobPriority::background,
                   [self, path, operation, refresh, writeXMP, stop](const auto &) {
                       QString error;
                       try {
                           luma::Catalog db(path);
                           (*operation)(db);
                           if (writeXMP)
                               luma::MetadataService(path).writePendingSidecars(stop);
                       } catch (const std::exception &e) {
                           error = QString::fromUtf8(e.what());
                       }
                       if (self)
                           QMetaObject::invokeMethod(
                               self,
                               [self, error, refresh] {
                                   if (!self)
                                       return;
                                   self->writing_ = false;
                                   self->error_ = error;
                                   if (!error.isEmpty() || !*refresh || !(*refresh)())
                                       self->reload(false);
                                   emit self->stateChanged();
                                   self->checkClose();
                               },
                               Qt::QueuedConnection);
                   });
}
bool LibraryModel::refreshMetadata(const std::vector<std::string> &ids, int role,
                                   const QVariant &value) {
    // Membership/order must remain stable. A pending filter/selection needs its
    // normal reload, and a matching metadata filter may remove changed assets.
    if (std::any_of(collections_.begin(), collections_.end(),
                    [](const QVariant &v) { return v.toMap().value("smart").toBool(); }) ||
        loading_ || selecting_ || filterTimer_.isActive() ||
        !std::is_sorted(ids.begin(), ids.end()) ||
        (role != Rating && role != Flag && role != Label) ||
        (role == Rating && filter_.minimumRating > 0) || (role == Flag && filter_.flag != -2) ||
        (role == Label && (filter_.label.has_value() || !filter_.search.empty())))
        return false;
    ++revision_;
    std::vector<int> pending;
    pending.reserve(pageJobs_.size());
    for (const auto &[number, job] : pageJobs_) {
        job.cancel();
        pending.push_back(number);
    }
    pageJobs_.clear();
    const auto label = role == Label ? value.toString().toStdString() : std::string{};
    for (auto &[number, rows] : pages_) {
        int first = -1, last = -1;
        for (std::size_t offset = 0; offset < rows.size(); ++offset) {
            auto &item = rows[offset];
            if (!std::binary_search(ids.begin(), ids.end(), item.id))
                continue;
            if (role == Rating)
                item.rating = value.toInt();
            else if (role == Flag)
                item.flag = value.toInt();
            else
                item.label = label;
            const int row = number * pageSize + static_cast<int>(offset);
            if (first < 0)
                first = row;
            last = row;
        }
        if (first >= 0)
            emit dataChanged(index(first), index(last), {role});
    }
    // Reissue in-flight reads after the commit; old callbacks carry the previous
    // revision and cannot overwrite freshly updated cached metadata.
    for (const auto number : pending)
        page(number);
    return true;
}
void LibraryModel::rate(int value) {
    const auto ids = std::make_shared<const std::vector<std::string>>(selectionIDs());
    if (!ids->empty())
        mutate([ids, value](auto &db) { db.setRating(*ids, value); },
               [this, ids, value] { return refreshMetadata(*ids, Rating, value); }, true);
}
void LibraryModel::flag(int value) {
    const auto ids = std::make_shared<const std::vector<std::string>>(selectionIDs());
    if (!ids->empty())
        mutate([ids, value](auto &db) { db.setFlag(*ids, value); },
               [this, ids, value] { return refreshMetadata(*ids, Flag, value); });
}
void LibraryModel::label(const QString &value) {
    const auto ids = std::make_shared<const std::vector<std::string>>(selectionIDs());
    if (!ids->empty())
        mutate([ids, value](auto &db) { db.setLabel(*ids, value.toStdString()); },
               [this, ids, value] { return refreshMetadata(*ids, Label, value); }, true);
}
void LibraryModel::createCollection(const QString &name) {
    mutate([name](auto &db) { db.createCollection(name.toStdString()); });
}
void LibraryModel::renameCollection(const QString &id, const QString &name) {
    mutate([id, name](auto &db) { db.renameCollection(id.toStdString(), name.toStdString()); });
}
void LibraryModel::deleteCollection(const QString &id) {
    if (filter_.collectionID == id.toStdString())
        filter_.collectionID.clear();
    mutate([id](auto &db) { db.deleteCollection(id.toStdString()); });
}
void LibraryModel::collectionMembership(const QString &id, bool add) {
    const auto ids = selectionIDs();
    if (!ids.empty())
        mutate([ids, id, add](auto &db) { db.collectionMembership(id.toStdString(), ids, add); });
}
void LibraryModel::importFiles(const QList<QUrl> &urls, bool copy, const QUrl &destination,
                               bool recursive, bool skipDuplicates) {
    if (working())
        return;
    std::vector<std::filesystem::path> sources;
    for (const auto &url : urls) {
        if (!url.isLocalFile()) {
            error_ = QStringLiteral("Import supports local files");
            emit stateChanged();
            return;
        }
        sources.emplace_back(url.toLocalFile().toStdString());
    }
    luma::ImportOptions options;
    options.mode = copy ? luma::ImportMode::copy : luma::ImportMode::add;
    options.destination = destination.toLocalFile().toStdString();
    options.recursive = recursive;
    options.skipDuplicates = skipDuplicates;
    startImport(std::move(sources), std::move(options));
}
bool LibraryModel::startImport(std::vector<std::filesystem::path> sources,
                               luma::ImportOptions options) {
    if (working())
        return false;
    importReport_.clear();
    importing_ = true;
    progress_ = 0;
    error_.clear();
    status_ = QStringLiteral("Finding photos and verifying checksums…");
    importCancellation_ = luma::CancellationSource{};
    const auto stop = importCancellation_.token();
    const auto path = catalog_;
    const QPointer<LibraryModel> self(this);
    emit stateChanged();
    try {
        imports_.submit(
            luma::JobPriority::background, [self, path, sources, options, stop](const auto &) {
                QString error, report;
                LocalizedText message;
                auto last = std::chrono::steady_clock::now();
                try {
                    const auto update = [self, &last](double value, const std::string &name) {
                        const auto now = std::chrono::steady_clock::now();
                        if (value < 1 && now - last < std::chrono::milliseconds(100))
                            return;
                        last = now;
                        if (self)
                            QMetaObject::invokeMethod(
                                self,
                                [self, value, name] {
                                    if (!self)
                                        return;
                                    self->progress_ = value;
                                    self->status_ = LocalizedText(QStringLiteral("Importing %1"))
                                                        .arg(QString::fromStdString(name));
                                    emit self->stateChanged();
                                },
                                Qt::QueuedConnection);
                    };
                    const auto result =
                        luma::LibraryService(path).import(sources, options, stop, update);
                    message = summary(result);
                    report = ::importReport(path, result);
                } catch (const std::exception &e) {
                    if (stop.isCancelled())
                        message = QStringLiteral("Import cancelled");
                    else
                        error = QString::fromUtf8(e.what());
                }
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self, error, message, report] {
                            if (!self)
                                return;
                            self->importing_ = false;
                            self->error_ = error;
                            self->status_ = message;
                            self->importReport_ = report;
                            self->reload(false);
                            emit self->stateChanged();
                            self->checkClose();
                        },
                        Qt::QueuedConnection);
            });
        return true;
    } catch (const std::exception &failure) {
        importing_ = false;
        error_ = QString::fromUtf8(failure.what());
        emit stateChanged();
        return false;
    }
}
void LibraryModel::cancelImport() {
    importCancellation_.cancel();
}
bool LibraryModel::requestClose() {
    closing_ = true;
    filterTimer_.stop();
    sourceWatchTimer_.stop();
    cancelImport();
    maintenanceCancellation_.cancel();
    cancelSemantic();
    return !importing_ && !writing_ && !semanticBusy_;
}
void LibraryModel::checkClose() {
    if (closing_ && !importing_ && !writing_ && !semanticBusy_)
        emit closeReady();
}
