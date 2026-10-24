#include "apps/desktop/LibraryModel.h"
#include "apps/shared/PhotoDecoder.h"
#include "core/image/Bayer.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/FileHasher.h"
#include "platform/macos/SemanticEmbedding.h"
#include <QPointer>
#include <QSettings>

void LibraryModel::cancelSemantic() {
    // Deleting a disabled index must reach its commit point before close/update.
    if (!semanticEnabled_ && semanticBusy_)
        return;
    semanticJob_.cancel();
    ++semanticRevision_;
    semanticBusy_ = false;
    emit stateChanged();
}
void LibraryModel::buildSemanticIndex() {
    if (working())
        return;
    cancelSemantic();
    semanticEnabled_ = true;
    QSettings settings(QString::fromStdString(catalog_.parent_path().string()) + "/preview.ini",
                       QSettings::IniFormat);
    settings.setValue("semanticIndexEnabled", true);
    semanticBusy_ = true;
    semanticProgress_ = 0;
    semanticError_.clear();
    semanticStatus_ = QStringLiteral("Indexing photos locally…");
    const auto revision = ++semanticRevision_;
    const auto path = catalog_;
    const QPointer<LibraryModel> self(this);
    emit stateChanged();
    try {
        semanticJob_ = semanticJobs_.submit(
            luma::JobPriority::background, [self, path, revision](const luma::JobContext &context) {
                QString error;
                std::size_t indexed = 0, failed = 0;
                try {
                    const auto stop = context.stopToken();
                    const auto provider = luma::macos::makeSemanticEmbedding();
                    luma::Catalog catalog(path);
                    const auto total = std::max<std::int64_t>(1, catalog.libraryCount({}));
                    std::string after;
                    for (;;) {
                        luma::checkCancellation(stop);
                        const auto ids =
                            catalog.pendingSemanticImages(provider->identifier(), after);
                        if (ids.empty())
                            break;
                        for (const auto &id : ids) {
                            luma::checkCancellation(stop);
                            after = id;
                            try {
                                const auto asset = catalog.assetByID(id);
                                if (!asset)
                                    continue;
                                if (luma::macos::sha256File(asset->path, stop) != asset->checksum)
                                    throw std::runtime_error(
                                        "Original changed; semantic indexing skipped.");
                                auto decoded = luma::PhotoDecoder().decode(asset->path, stop);
                                const auto preview =
                                    luma::standardPreview(decoded.image, 512, stop);
                                luma::EditDocument neutral;
                                neutral.enabledCorrections = 0;
                                const auto display =
                                    luma::renderCPU(preview, neutral, asset->metadata.orientation,
                                                    {}, stop, decoded.calibration);
                                const auto vector = provider->imageEmbedding(
                                    {display.width, display.height, display.rgba}, stop);
                                if (luma::macos::sha256File(asset->path, stop) != asset->checksum)
                                    throw std::runtime_error(
                                        "Original changed during semantic indexing.");
                                luma::checkCancellation(stop);
                                catalog.saveSemanticEmbedding(id, asset->checksum,
                                                              provider->identifier(), vector);
                                ++indexed;
                            } catch (const std::exception &failure) {
                                luma::checkCancellation(stop);
                                ++failed;
                                error = QString::fromUtf8(failure.what());
                            }
                            if (self)
                                QMetaObject::invokeMethod(
                                    self,
                                    [self, revision, indexed, failed, total] {
                                        if (!self || self->semanticRevision_ != revision)
                                            return;
                                        self->semanticProgress_ = std::min(
                                            1.0, static_cast<double>(indexed + failed) / total);
                                        self->semanticStatus_ =
                                            LocalizedText(
                                                QStringLiteral("Indexed: %1 · errors: %2"))
                                                .arg(static_cast<qulonglong>(indexed))
                                                .arg(static_cast<qulonglong>(failed));
                                        emit self->stateChanged();
                                    },
                                    Qt::QueuedConnection);
                        }
                    }
                } catch (const std::exception &failure) {
                    if (!context.stopToken().isCancelled())
                        error = QString::fromUtf8(failure.what());
                }
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self, revision, indexed, failed, error] {
                            if (!self || self->semanticRevision_ != revision)
                                return;
                            self->semanticBusy_ = false;
                            self->semanticProgress_ = 1;
                            self->semanticError_ = error;
                            self->semanticStatus_ =
                                LocalizedText(QStringLiteral("Indexed: %1 · errors: %2"))
                                    .arg(static_cast<qulonglong>(indexed))
                                    .arg(static_cast<qulonglong>(failed));
                            emit self->stateChanged();
                        },
                        Qt::QueuedConnection);
            });
    } catch (const std::exception &failure) {
        semanticBusy_ = false;
        semanticError_ = QString::fromUtf8(failure.what());
        emit stateChanged();
    }
}
void LibraryModel::searchSemantic(const QString &text) {
    if (!semanticEnabled_ || working() || text.trimmed().isEmpty())
        return;
    cancelSemantic();
    semanticBusy_ = true;
    semanticResults_.clear();
    semanticError_.clear();
    semanticStatus_ = QStringLiteral("Searching local index…");
    const auto revision = ++semanticRevision_;
    const auto path = catalog_;
    const auto query = text.trimmed().toStdString();
    const QPointer<LibraryModel> self(this);
    emit stateChanged();
    try {
        semanticJob_ = semanticJobs_.submit(
            luma::JobPriority::background,
            [self, path, query, revision](const luma::JobContext &context) {
                QVariantList results;
                QString error;
                try {
                    const auto stop = context.stopToken();
                    const auto provider = luma::macos::makeSemanticEmbedding();
                    const auto vector = provider->textEmbedding(query, stop);
                    luma::Catalog catalog(path);
                    for (const auto &match :
                         catalog.semanticSearch(provider->identifier(), vector, 100, stop)) {
                        luma::checkCancellation(stop);
                        const auto asset = catalog.assetByID(match.id);
                        if (!asset)
                            continue;
                        const auto id = QString::fromStdString(match.id);
                        results.append(QVariantMap{
                            {"id", id},
                            {"filename", QString::fromStdString(asset->path.filename().string())},
                            {"url",
                             QUrl::fromLocalFile(QString::fromStdString(asset->path.string()))},
                            {"thumbnail", QStringLiteral("image://thumbnail/") + id},
                            {"similarity", match.similarity}});
                    }
                } catch (const std::exception &failure) {
                    if (!context.stopToken().isCancelled())
                        error = QString::fromUtf8(failure.what());
                }
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self, revision, results, error] {
                            if (!self || self->semanticRevision_ != revision)
                                return;
                            self->semanticBusy_ = false;
                            self->semanticResults_ = results;
                            self->semanticError_ = error;
                            self->semanticStatus_ =
                                LocalizedText(QStringLiteral("Matches: %1")).arg(results.size());
                            emit self->stateChanged();
                        },
                        Qt::QueuedConnection);
            });
    } catch (const std::exception &failure) {
        semanticBusy_ = false;
        semanticError_ = QString::fromUtf8(failure.what());
        emit stateChanged();
    }
}
void LibraryModel::disableSemantic() {
    if (closing_ || (!semanticEnabled_ && semanticBusy_))
        return;
    cancelSemantic();
    semanticEnabled_ = false;
    semanticResults_.clear();
    semanticBusy_ = true;
    semanticError_.clear();
    QSettings settings(QString::fromStdString(catalog_.parent_path().string()) + "/preview.ini",
                       QSettings::IniFormat);
    settings.setValue("semanticIndexEnabled", false);
    const auto revision = ++semanticRevision_;
    const auto path = catalog_;
    const QPointer<LibraryModel> self(this);
    emit stateChanged();
    try {
        // Same serial queue as indexing: any cancelled in-flight write completes
        // before deletion. It cannot repopulate the index after this operation.
        semanticJob_ = semanticJobs_.submit(
            luma::JobPriority::background, [self, path, revision](const luma::JobContext &) {
                QString error;
                try {
                    luma::Catalog(path).clearSemanticIndex();
                } catch (const std::exception &failure) {
                    error = QString::fromUtf8(failure.what());
                }
                if (self)
                    QMetaObject::invokeMethod(
                        self,
                        [self, revision, error] {
                            if (!self || self->semanticRevision_ != revision)
                                return;
                            self->semanticBusy_ = false;
                            self->semanticError_ = error;
                            self->semanticStatus_ = QStringLiteral("Semantic search disabled");
                            emit self->stateChanged();
                            self->checkClose();
                        },
                        Qt::QueuedConnection);
            });
    } catch (const std::exception &failure) {
        semanticBusy_ = false;
        semanticError_ = QString::fromUtf8(failure.what());
        emit stateChanged();
        checkClose();
    }
}
