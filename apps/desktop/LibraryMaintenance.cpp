#include "apps/desktop/LibraryModel.h"
#include "apps/shared/BackupService.h"
#include "apps/shared/MetadataService.h"
#include "apps/shared/RelinkService.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

namespace {
QVariantMap map(const luma::MetadataSnapshot &snapshot) {
    QStringList keywords, creators;
    for (const auto &word : snapshot.values.keywords)
        keywords.append(QString::fromStdString(word));
    for (const auto &name : snapshot.values.creators)
        creators.append(QString::fromStdString(name));
    return {{"id", QString::fromStdString(snapshot.id)},
            {"revision", QString::number(snapshot.revision)},
            {"path", QString::fromStdString(snapshot.path.string())},
            {"title", QString::fromStdString(snapshot.values.title)},
            {"caption", QString::fromStdString(snapshot.values.caption)},
            {"copyright", QString::fromStdString(snapshot.values.copyright)},
            {"creators", creators},
            {"location", QString::fromStdString(snapshot.values.location)},
            {"keywords", keywords},
            {"rating", snapshot.values.rating},
            {"flag", snapshot.values.flag},
            {"label", QString::fromStdString(snapshot.values.label)},
            {"pendingXMP", snapshot.revision > snapshot.writtenRevision},
            {"readError", QString::fromStdString(snapshot.readError)}};
}
QString preferences(const std::filesystem::path &catalog) {
    return QString::fromStdString(catalog.parent_path().string()) + QStringLiteral("/preview.ini");
}
} // namespace
void LibraryModel::initializeMaintenance() {
    QSettings settings(preferences(catalog_), QSettings::IniFormat);
    backupSchedule_ = std::clamp(settings.value("backupSchedule", 2).toInt(), 0, 3);
    backupKeep_ = std::clamp(settings.value("backupKeep", 10).toInt(), 1, 50);
    autoWriteXMP_ = settings.value("autoWriteXMP", false).toBool();
    sourceWatchTimer_.setSingleShot(true);
    sourceWatchTimer_.setInterval(500);
    connect(&sourceWatcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        // Coalesce a burst without postponing the first refresh indefinitely
        // when other files keep changing in an observed directory.
        if (!closing_ && !sourceWatchTimer_.isActive())
            sourceWatchTimer_.start();
    });
    connect(&sourceWatchTimer_, &QTimer::timeout, this, [this] {
        if (working() || loading_) {
            if (!closing_)
                sourceWatchTimer_.start();
            return;
        }
        // Changes are observed, never used to replace an original or import XMP automatically.
        status_ = QStringLiteral(
            "Source folder changed. Reload metadata explicitly to import external XMP changes.");
        watchVisibleFolders();
        refreshSourceAvailability();
        emit stateChanged();
    });
}
void LibraryModel::refreshSourceAvailability() {
    sourceAvailabilityJob_.cancel();
    const auto revision = revision_;
    std::map<std::string, std::filesystem::path> sources;
    for (const auto &[page, rows] : pages_) {
        (void)page;
        for (const auto &row : rows)
            sources.emplace(row.id, row.path);
    }
    const QPointer<LibraryModel> self(this);
    sourceAvailabilityJob_ = reads_.submit(
        luma::JobPriority::background,
        [self, revision, sources = std::move(sources)](const auto &context) {
            const auto stop = context.stopToken();
            std::map<std::string, bool> missing;
            for (const auto &[id, path] : sources) {
                if (stop.isCancelled())
                    return;
                std::error_code error;
                const bool exists = std::filesystem::exists(path, error);
                if (!error)
                    missing.emplace(id, !exists);
            }
            if (self)
                QMetaObject::invokeMethod(
                    self,
                    [self, revision, stop, missing = std::move(missing)] {
                        if (!self || self->closing_ || stop.isCancelled())
                            return;
                        if (self->revision_ != revision) {
                            self->sourceWatchTimer_.start();
                            return;
                        }
                        // Directory notifications never change catalog membership.
                        // Keep delegates, selection and viewport intact while
                        // updating availability on the current cached pages only.
                        for (auto &[page, rows] : self->pages_)
                            for (std::size_t offset = 0; offset < rows.size(); ++offset) {
                                auto &item = rows[offset];
                                const auto found = missing.find(item.id);
                                if (found == missing.end() || found->second == item.missing)
                                    continue;
                                item.missing = found->second;
                                const auto row = page * pageSize + static_cast<int>(offset);
                                emit self->dataChanged(self->index(row), self->index(row),
                                                       {Missing});
                            }
                    },
                    Qt::QueuedConnection);
        });
}
void LibraryModel::watchVisibleFolders() {
    QSet<QString> wanted;
    for (const auto &[page, rows] : pages_) {
        (void)page;
        for (const auto &row : rows) {
            if (wanted.size() >= 512)
                break;
            // Watching a directory's contents does not reliably report a rename
            // of that directory (or one of its ancestors). Watch existing parents
            // too, so moved folders and their restoration refresh availability.
            for (auto path = row.path.parent_path(); !path.empty() && wanted.size() < 512;) {
                const auto name = QString::fromStdString(path.string());
                if (wanted.contains(name))
                    break; // This ancestor chain has already been considered.
                if (QFileInfo(name).isDir())
                    wanted.insert(name);
                const auto parent = path.parent_path();
                if (parent == path)
                    break;
                path = parent;
            }
        }
    }
    const auto current = sourceWatcher_.directories();
    QStringList remove, add;
    for (const auto &path : current)
        if (!wanted.contains(path))
            remove.append(path);
    for (const auto &path : wanted)
        if (!current.contains(path))
            add.append(path);
    if (!remove.isEmpty())
        sourceWatcher_.removePaths(remove);
    if (!add.isEmpty())
        sourceWatcher_.addPaths(add);
}
QUrl LibraryModel::backupFolder() const {
    return QUrl::fromLocalFile(
        QString::fromStdString((std::filesystem::absolute(catalog_).parent_path() /
                                (catalog_.filename().string() + ".luma-backups"))
                                   .string()));
}
void LibraryModel::setBackupSchedule(int value) {
    if (value < 0 || value > 3)
        return;
    backupSchedule_ = value;
    QSettings(preferences(catalog_), QSettings::IniFormat).setValue("backupSchedule", value);
    emit stateChanged();
}
void LibraryModel::setBackupKeep(int value) {
    if (value < 1 || value > 50)
        return;
    backupKeep_ = value;
    QSettings(preferences(catalog_), QSettings::IniFormat).setValue("backupKeep", value);
    emit stateChanged();
}
void LibraryModel::setAutoWriteXMP(bool value) {
    autoWriteXMP_ = value;
    QSettings(preferences(catalog_), QSettings::IniFormat).setValue("autoWriteXMP", value);
    emit stateChanged();
    if (value)
        syncSidecars();
}
void LibraryModel::backupNow() {
    const auto path = catalog_;
    const auto keep = backupKeep_;
    const auto stop = maintenanceCancellation_.token();
    const auto result = std::make_shared<std::filesystem::path>();
    mutate(
        [path, keep, stop, result](auto &) {
            *result =
                *luma::BackupService(path).run(luma::BackupSchedule::manual, keep, true, stop);
        },
        [this, result] {
            maintenanceStatus_ = LocalizedText(QStringLiteral("Catalog backup saved: %1"))
                                     .arg(QString::fromStdString(result->string()));
            status_ = maintenanceStatus_;
            return true;
        });
}
void LibraryModel::restoreCatalog(const QUrl &backup, const QUrl &destination) {
    if (!backup.isLocalFile() || !destination.isLocalFile())
        return;
    const auto source = std::filesystem::path(backup.toLocalFile().toStdString());
    const auto target = std::filesystem::path(destination.toLocalFile().toStdString());
    const auto stop = maintenanceCancellation_.token();
    mutate([source, target, stop](auto &) { luma::Catalog::restoreBackupTo(source, target, stop); },
           [this, target] {
               maintenanceStatus_ =
                   LocalizedText(QStringLiteral("Catalog restored to a new file: %1"))
                       .arg(QString::fromStdString(target.string()));
               status_ = maintenanceStatus_;
               return true;
           });
}
void LibraryModel::openCatalog(const QUrl &file, bool create) {
    if (!file.isLocalFile() || working())
        return;
    const auto target = std::filesystem::absolute(file.toLocalFile().toStdString());
    const auto stop = maintenanceCancellation_.token();
    mutate(
        [target, create, stop](auto &) {
            if (create) {
                // Reserve the requested path without replacing an existing catalog.
                const int descriptor = ::open(target.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
                if (descriptor < 0)
                    throw std::runtime_error(
                        "Cannot create catalog: choose a new writable filename.");
                close(descriptor);
                luma::Catalog database(target);
            } else {
                luma::Catalog::verifyBackup(target, stop);
            }
        },
        [this, target] {
            const auto path = QString::fromStdString(target.string());
            if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                         {QStringLiteral("--catalog"), path}))
                error_ = QStringLiteral("Cannot open another catalog window.");
            return true;
        });
}
void LibraryModel::loadMetadata(const QString &id) {
    if (closing_ || id.isEmpty())
        return;
    const auto revision = ++metadataRevision_;
    descriptiveMetadata_.clear();
    emit metadataChanged();
    const auto path = catalog_;
    const QPointer<LibraryModel> self(this);
    reads_.submit(luma::JobPriority::interactive, [self, path, id, revision](const auto &) {
        QVariantMap values;
        QString error;
        try {
            values = map(luma::MetadataService(path).read(id.toStdString()));
        } catch (const std::exception &failure) {
            error = QString::fromUtf8(failure.what());
        }
        if (self)
            QMetaObject::invokeMethod(
                self,
                [self, values, error, revision] {
                    if (!self || self->metadataRevision_ != revision || self->closing_)
                        return;
                    self->descriptiveMetadata_ = values;
                    self->error_ = error;
                    emit self->metadataChanged();
                    emit self->stateChanged();
                },
                Qt::QueuedConnection);
    });
}
void LibraryModel::saveDescriptiveMetadata(const QVariantMap &values) {
    if (descriptiveMetadata_.isEmpty() || working())
        return;
    const auto id = descriptiveMetadata_.value("id").toString();
    const auto revision = descriptiveMetadata_.value("revision").toString().toLongLong();
    luma::DescriptiveMetadata metadata;
    metadata.title = values.value("title").toString().toStdString();
    metadata.caption = values.value("caption").toString().toStdString();
    metadata.copyright = values.value("copyright").toString().toStdString();
    for (const auto &name : values.value("creators").toStringList())
        if (!name.isEmpty())
            metadata.creators.push_back(name.toStdString());
    metadata.location = values.value("location").toString().toStdString();
    metadata.rating = values.value("rating").toInt();
    metadata.label = values.value("label").toString().toStdString();
    metadata.flag = values.value("flag").toInt();
    for (const auto &word : values.value("keywords").toStringList()) {
        const auto text = word.trimmed().toStdString();
        if (!text.empty() && std::find(metadata.keywords.begin(), metadata.keywords.end(), text) ==
                                 metadata.keywords.end())
            metadata.keywords.push_back(text);
    }
    mutate([id, revision, metadata](
               auto &catalog) { catalog.saveMetadata(id.toStdString(), revision, metadata); },
           [this, id] {
               loadMetadata(id);
               return false;
           },
           true);
}
void LibraryModel::importDescriptiveMetadata() {
    if (descriptiveMetadata_.isEmpty())
        return;
    const auto id = descriptiveMetadata_.value("id").toString();
    const auto revision = descriptiveMetadata_.value("revision").toString().toLongLong();
    const auto path = catalog_;
    const auto stop = maintenanceCancellation_.token();
    mutate(
        [path, id, revision, stop](auto &) {
            luma::MetadataService(path).importFromOriginal(id.toStdString(), revision, stop);
        },
        [this, id] {
            loadMetadata(id);
            return false;
        });
}
void LibraryModel::writeMetadataSidecar() {
    if (descriptiveMetadata_.isEmpty())
        return;
    const auto id = descriptiveMetadata_.value("id").toString();
    const auto path = catalog_;
    const auto stop = maintenanceCancellation_.token();
    mutate([path, id,
            stop](auto &) { luma::MetadataService(path).writeSidecar(id.toStdString(), stop); },
           [this, id] {
               loadMetadata(id);
               return true;
           });
}
void LibraryModel::syncSidecars() {
    const auto path = catalog_;
    const auto stop = maintenanceCancellation_.token();
    mutate([path, stop](auto &) { luma::MetadataService(path).writePendingSidecars(stop); });
}
void LibraryModel::locateOriginal(const QUrl &file) {
    if (!file.isLocalFile() || descriptiveMetadata_.isEmpty())
        return;
    const auto id = descriptiveMetadata_.value("id").toString();
    const auto path = catalog_;
    const auto stop = maintenanceCancellation_.token();
    const auto source = std::filesystem::path(file.toLocalFile().toStdString());
    mutate([path, id, source,
            stop](auto &) { luma::RelinkService(path).locate(id.toStdString(), source, stop); },
           [this, id] {
               loadMetadata(id);
               return false;
           });
}
void LibraryModel::locateFolder(const QUrl &folder, bool nearby) {
    if (!folder.isLocalFile() || descriptiveMetadata_.isEmpty())
        return;
    const auto old =
        std::filesystem::path(descriptiveMetadata_.value("path").toString().toStdString())
            .parent_path();
    const auto destination = std::filesystem::path(folder.toLocalFile().toStdString());
    const auto path = catalog_;
    const auto stop = maintenanceCancellation_.token();
    const auto id = descriptiveMetadata_.value("id").toString();
    const auto result = std::make_shared<luma::RelinkSummary>();
    mutate(
        [path, old, destination, nearby, stop, result](auto &) {
            *result = luma::RelinkService(path).locateFolder(old, destination, nearby, stop);
        },
        [this, result, id] {
            status_ = LocalizedText(QStringLiteral("Relinked: %1 · unresolved: %2"))
                          .arg(result->relinked)
                          .arg(result->unresolved);
            loadMetadata(id);
            return false;
        });
}
