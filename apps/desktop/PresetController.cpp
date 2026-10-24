#include "apps/desktop/PresetController.h"
#include "apps/desktop/EditorController.h"
#include "apps/shared/BuiltinPresets.h"
#include <QFileInfo>
#include <algorithm>
#include <filesystem>

PresetController::PresetController(QString path, EditorController &editor, QObject *parent)
    : QObject(parent), path_(std::move(path)), store_(path_), editor_(editor) {
    refresh();
}
QVariantList PresetController::items() const {
    QVariantList result;
    for (const auto &entry : presets_)
        result.append(QVariantMap{{"id", entry.id},
                                  {"name", entry.preset.name},
                                  {"folder", entry.preset.folder},
                                  {"groups", static_cast<int>(entry.preset.groups)}});
    return result;
}
QVariantList PresetController::builtinItems() const {
    QVariantList result;
    for (const auto &entry : luma::builtinPresets())
        result.append(QVariantMap{{"id", entry.id},
                                  {"name", entry.preset.name},
                                  {"groups", static_cast<int>(entry.preset.groups)}});
    return result;
}
const luma::DevelopPreset &PresetController::find(const QString &id) const {
    if (luma::builtinPresetID(id)) {
        for (const auto &entry : luma::builtinPresets())
            if (entry.id == id)
                return entry.preset;
        throw std::runtime_error("Unknown built-in filter.");
    }
    const auto found =
        std::find_if(presets_.begin(), presets_.end(), [&](const auto &p) { return p.id == id; });
    if (found == presets_.end())
        throw std::runtime_error("Preset no longer exists. Refresh the collection.");
    return found->preset;
}
bool PresetController::failure(const std::exception &error) {
    error_ = QString::fromUtf8(error.what());
    emit stateChanged();
    return false;
}
bool PresetController::refresh() {
    try {
        auto next = store_.load();
        std::stable_sort(next.begin(), next.end(), [](const auto &a, const auto &b) {
            const auto folder =
                QString::compare(a.preset.folder, b.preset.folder, Qt::CaseInsensitive);
            return folder != 0
                       ? folder < 0
                       : QString::compare(a.preset.name, b.preset.name, Qt::CaseInsensitive) < 0;
        });
        editor_.clearPreviewSettings();
        presets_ = std::move(next);
        error_.clear();
        emit itemsChanged();
        emit stateChanged();
        return true;
    } catch (const std::exception &error) {
        return failure(error);
    }
}
bool PresetController::save(const QString &id, const QString &name, const QString &folder,
                            int groups, bool captureCurrent) {
    try {
        if (luma::builtinPresetID(id))
            throw std::runtime_error("Built-in filters are read-only. Save a new user preset.");
        if (captureCurrent && (!editor_.hasPhoto() || editor_.busy()))
            throw std::runtime_error("Open a photo before capturing its settings.");
        luma::DevelopPreset preset;
        if (!id.isEmpty())
            preset = find(id);
        if (id.isEmpty() && !captureCurrent)
            throw std::runtime_error("A new preset must capture current settings.");
        // Adding groups to an existing preset requires recapturing, so neutral
        // defaults never silently replace groups absent from its saved subset.
        if (!captureCurrent && (static_cast<std::uint32_t>(groups) & ~preset.groups))
            throw std::runtime_error("Capture current settings to add groups to this preset.");
        preset.name = name.trimmed();
        preset.folder = folder.trimmed();
        preset.groups = static_cast<std::uint32_t>(groups);
        if (captureCurrent) {
            preset.settings = editor_.currentSettings();
            preset.parameters.clear();
        } else {
            std::erase_if(preset.parameters, [&](const auto &name) {
                return !(luma::settingsParameterGroup(name) & preset.groups);
            });
        }
        store_.put(preset, id);
        return refresh();
    } catch (const std::exception &error) {
        return failure(error);
    }
}
bool PresetController::remove(const QString &id) {
    try {
        if (luma::builtinPresetID(id))
            throw std::runtime_error("Built-in filters cannot be deleted.");
        store_.remove(id);
        return refresh();
    } catch (const std::exception &error) {
        return failure(error);
    }
}
bool PresetController::importPreset(const QUrl &url) {
    importNotice_ = {};
    importUnsupported_.clear();
    importedId_.clear();
    try {
        if (!url.isLocalFile())
            throw std::runtime_error("Select a local preset JSON or XMP file.");
        const auto imported = luma::PresetStore::inspectImport(url.toLocalFile());
        const auto id = store_.put(imported.preset);
        if (!refresh())
            return false;
        importedId_ = id;
        if (imported.xmp) {
            importNotice_ =
                LocalizedText(QStringLiteral("XMP imported. Luma rendering may differ from "
                                             "Lightroom. Absent settings are preserved."));
            if (!imported.unsupported.isEmpty())
                importNotice_ =
                    LocalizedText(
                        QStringLiteral("XMP imported with partial compatibility. Luma rendering "
                                       "may differ from Lightroom. Unsupported settings: %1"))
                        .arg(imported.unsupported.size());
            importUnsupported_ = imported.unsupported;
        }
        emit stateChanged();
        return true;
    } catch (const std::exception &error) {
        return failure(error);
    }
}
bool PresetController::exportPreset(const QString &id, const QUrl &url) {
    try {
        if (!url.isLocalFile())
            throw std::runtime_error("Select a local destination for the preset JSON.");
        const auto destination = url.toLocalFile();
        for (const auto &protectedPath :
             {path_, editor_.catalogPath(), editor_.catalogPath() + QStringLiteral("-wal"),
              editor_.catalogPath() + QStringLiteral("-shm"), editor_.sourcePath()}) {
            if (protectedPath.isEmpty())
                continue;
            std::error_code error;
            const bool equivalent = std::filesystem::equivalent(destination.toStdString(),
                                                                protectedPath.toStdString(), error);
            if (QFileInfo(destination).absoluteFilePath() ==
                    QFileInfo(protectedPath).absoluteFilePath() ||
                (!error && equivalent))
                throw std::runtime_error(
                    "Preset export cannot replace a RAW, catalog or preset collection.");
        }
        luma::PresetStore::exportFile(destination, find(id));
        error_.clear();
        emit stateChanged();
        return true;
    } catch (const std::exception &error) {
        return failure(error);
    }
}
void PresetController::apply(const QString &id) {
    try {
        const auto &preset = find(id);
        editor_.applySettings(preset.settings, preset.groups, preset.parameters);
    } catch (const std::exception &error) {
        failure(error);
    }
}
void PresetController::preview(const QString &id) {
    try {
        const auto &preset = find(id);
        editor_.previewSettings(preset.settings, preset.groups, preset.parameters);
    } catch (const std::exception &error) {
        failure(error);
    }
}
void PresetController::clearPreview() {
    editor_.clearPreviewSettings();
}
void PresetController::copySettings(int groups) {
    try {
        if (!editor_.hasPhoto() || editor_.busy())
            return;
        luma::DevelopPreset value{
            "Copied settings", {}, editor_.currentSettings(), static_cast<std::uint32_t>(groups)};
        value.validate();
        copied_ = std::move(value);
        error_.clear();
        emit stateChanged();
    } catch (const std::exception &error) {
        failure(error);
    }
}
void PresetController::pasteSettings() {
    if (copied_)
        editor_.applySettings(copied_->settings, copied_->groups);
}

QString PresetController::importSnapshot(const QString &id) {
    try {
        return QString::fromUtf8(luma::encodePreset(find(id)));
    } catch (const std::exception &error) {
        failure(error);
        return {};
    }
}
