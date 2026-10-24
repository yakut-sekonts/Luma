#include "apps/desktop/EditorController.h"
#include <QPointer>

void EditorController::refreshVersions() {
    runVersionAction(VersionAction::list);
}
void EditorController::saveVersion(const QString &name) {
    runVersionAction(VersionAction::create, {}, {}, name);
}
void EditorController::restoreVersion(const QString &id, const QString &revision) {
    runVersionAction(VersionAction::restore, id, revision);
}
void EditorController::renameVersion(const QString &id, const QString &revision,
                                     const QString &name) {
    runVersionAction(VersionAction::rename, id, revision, name);
}
void EditorController::deleteVersion(const QString &id, const QString &revision) {
    runVersionAction(VersionAction::remove, id, revision);
}
void EditorController::runVersionAction(VersionAction action, const QString &id,
                                        const QString &revision, const QString &name,
                                        std::optional<luma::EditDocument> snapshot) {
    if (!session_ || busy() || exporting_ || closing_ || editingLocked_ || !deferredOpen_.isEmpty())
        return;
    std::int64_t expectedRevision = 0;
    if (action == VersionAction::restore || action == VersionAction::rename ||
        action == VersionAction::remove) {
        bool valid = false;
        expectedRevision = revision.toLongLong(&valid);
        if (id.isEmpty() || !valid || expectedRevision < 0) {
            versionsError_ = QStringLiteral("Refresh the version list and select a version.");
            emit stateChanged();
            return;
        }
    }
    // Complete the current gesture and cancel transient tools before capturing
    // edits. Preset hover and Before/Original are never saved as a version.
    cancelAI();
    setRetouchMode(QStringLiteral("off"));
    setLocalMode(false);
    closeTransformTool();
    setCropMode(false);
    endEdit();
    const auto assetID = session_->asset.id;
    const auto sourceGeneration = loadRevision_;
    const auto editGeneration = editRevision_;
    if (action == VersionAction::create && !snapshot)
        snapshot = history_.current();
    const auto catalog = catalog_;
    const auto trimmedName = name.trimmed().toStdString();
    versionsBusy_ = true;
    versionsError_.clear();
    versionsStatus_ = QStringLiteral("Working with versions…");
    emit stateChanged();
    const QPointer<EditorController> self(this);
    try {
        ioJobs_.submit(luma::JobPriority::background, [self, catalog, assetID, sourceGeneration,
                                                       editGeneration, action, id, expectedRevision,
                                                       trimmedName, snapshot](const auto &) {
            QVariantList rows;
            QString error, selected = id;
            LocalizedText status;
            std::optional<luma::EditDocument> restored;
            bool listLoaded = false;
            try {
                luma::Catalog storage(catalog, luma::CatalogOpenMode::existingOnly);
                switch (action) {
                case VersionAction::list:
                    selected.clear();
                    break;
                case VersionAction::create:
                    selected = QString::fromStdString(
                        storage.createEditVersion(assetID, trimmedName, *snapshot));
                    status = QStringLiteral("Version saved.");
                    break;
                case VersionAction::restore:
                    restored = storage.editVersion(assetID, id.toStdString(), expectedRevision);
                    break;
                case VersionAction::rename:
                    storage.renameEditVersion(assetID, id.toStdString(), expectedRevision,
                                              trimmedName);
                    status = QStringLiteral("Version renamed.");
                    break;
                case VersionAction::remove:
                    storage.deleteEditVersion(assetID, id.toStdString(), expectedRevision);
                    selected.clear();
                    status = QStringLiteral("Version deleted.");
                    break;
                }
                for (const auto &entry : storage.editVersions(assetID)) {
                    rows.append(QVariantMap{{"id", QString::fromStdString(entry.id)},
                                            {"name", QString::fromStdString(entry.name)},
                                            {"createdAt", QString::fromStdString(entry.createdAt)},
                                            {"revision", QString::number(entry.revision)}});
                }
                listLoaded = true;
            } catch (const std::exception &failure) {
                error = QString::fromUtf8(failure.what());
            }
            if (self)
                QMetaObject::invokeMethod(
                    self,
                    [self, assetID, sourceGeneration, editGeneration, rows, error, status, selected,
                     listLoaded, restored = std::move(restored)]() mutable {
                        if (!self)
                            return;
                        self->versionsBusy_ = false;
                        self->versionsError_ = error;
                        self->versionsStatus_ = status;
                        if (self->session_ && self->session_->asset.id == assetID &&
                            self->loadRevision_ == sourceGeneration) {
                            if (listLoaded)
                                self->processingVersions_ = rows;
                            self->selectedVersionID_ = selected;
                            if (restored && error.isEmpty() && !self->closing_) {
                                try {
                                    if (!self->editable() || self->editRevision_ != editGeneration)
                                        throw std::runtime_error("The photo changed while loading "
                                                                 "the version. Try again.");
                                    self->validateGeometry(*restored);
                                    self->original_ = false;
                                    if (*restored != self->history_.current()) {
                                        self->history_.apply(std::move(*restored),
                                                             "Restore version");
                                        self->editsChanged();
                                    } else {
                                        self->render();
                                    }
                                    self->versionsStatus_ =
                                        QStringLiteral("Version restored. Use Undo to return to "
                                                       "the previous edit.");
                                } catch (const std::exception &failure) {
                                    self->versionsError_ = QString::fromUtf8(failure.what());
                                }
                            }
                        }
                        emit self->stateChanged();
                        // A file picked during version I/O waits for this action
                        // and any autosave triggered by restoring the document.
                        if (!self->closing_ && !self->saving() && !self->deferredOpen_.isEmpty()) {
                            const auto url = self->deferredOpen_;
                            self->deferredOpen_ = QUrl();
                            self->openFile(url);
                        }
                        self->checkClose();
                    },
                    Qt::QueuedConnection);
        });
    } catch (const std::exception &failure) {
        versionsBusy_ = false;
        versionsStatus_ = QString();
        versionsError_ = QString::fromUtf8(failure.what());
        emit stateChanged();
    }
}
