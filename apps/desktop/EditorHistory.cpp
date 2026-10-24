#include "apps/desktop/EditorController.h"
#include <QCoreApplication>
#include <QLocale>
#include <QStringList>

namespace {
QString describe(const luma::EditHistoryChange &change) {
    auto label = QCoreApplication::translate("Luma", change.label.c_str());
    if (change.value) {
        const bool exposure = change.label == "Exposure";
        const bool temperature = change.label == "Temperature";
        auto number = QLocale().toString(*change.value, exposure ? 'f' : 'g', exposure ? 2 : 6);
        if (exposure && *change.value > 0)
            number.prepend(QLatin1Char('+'));
        label += QLatin1Char(' ') + number;
        if (temperature)
            label += QStringLiteral(" K");
    }
    if (!change.subject.empty())
        label = QString::fromStdString(change.subject) + QStringLiteral(" — ") + label;
    return label;
}
} // namespace

QString EditorController::historyHandle(std::uint64_t id) const {
    return QString::number(historyGeneration_) + QLatin1Char(':') + QString::number(id);
}
QString EditorController::historyRevision() const {
    return historyHandle(history_.revision());
}
std::optional<std::uint64_t> EditorController::resolveHistoryHandle(const QString &handle) const {
    const auto prefix = QString::number(historyGeneration_) + QLatin1Char(':');
    if (!handle.startsWith(prefix))
        return std::nullopt;
    bool valid = false;
    const auto id = handle.mid(prefix.size()).toULongLong(&valid);
    if (!valid || historyHandle(id) != handle || !history_.document(id))
        return std::nullopt;
    return id;
}
QVariantList EditorController::processingHistory() const {
    QVariantList rows;
    if (!session_)
        return rows;
    for (const auto &entry : history_.items()) {
        QStringList changes;
        for (const auto &change : entry.changes)
            changes.append(describe(change));
        const bool singleChange = entry.action.empty() && changes.size() == 1;
        const auto title =
            singleChange
                ? changes.front()
                : QCoreApplication::translate("Luma", entry.action.empty() ? "Processing settings"
                                                                           : entry.action.c_str());
        rows.append(
            QVariantMap{{"id", historyHandle(entry.id)},
                        {"title", title},
                        {"details", singleChange ? QString() : changes.join(QStringLiteral(" · "))},
                        {"current", entry.current},
                        {"redo", entry.redo}});
    }
    return rows;
}
void EditorController::finishHistoryTools() {
    cancelAI();
    setRetouchMode(QStringLiteral("off"));
    setLocalMode(false);
    closeTransformTool();
    setCropMode(false);
    endEdit();
}
void EditorController::prepareHistory() {
    if (!editable() || exporting_ || !deferredOpen_.isEmpty())
        return;
    finishHistoryTools();
    historyError_.clear();
    versionsError_.clear();
    versionsStatus_ = QString();
    emit stateChanged();
}
void EditorController::jumpToHistory(const QString &handle) {
    if (!editable() || exporting_ || !deferredOpen_.isEmpty())
        return;
    finishHistoryTools();
    historyError_.clear();
    const auto id = resolveHistoryHandle(handle);
    if (!id) {
        historyError_ =
            QStringLiteral("This history state is no longer available. Select a current row.");
        emit stateChanged();
        return;
    }
    try {
        validateGeometry(*history_.document(*id));
        const auto revision = history_.revision();
        const bool wasOriginal = original_;
        history_.seek(*id);
        original_ = false;
        if (history_.revision() != revision)
            editsChanged();
        else if (wasOriginal)
            render();
    } catch (const std::exception &failure) {
        historyError_ = QString::fromUtf8(failure.what());
    }
    emit stateChanged();
}
void EditorController::clearHistory(const QString &revision) {
    if (!editable() || exporting_ || !deferredOpen_.isEmpty())
        return;
    finishHistoryTools();
    historyError_.clear();
    if (revision != historyRevision()) {
        historyError_ =
            QStringLiteral("History changed while confirmation was open. Review it and try again.");
    } else {
        // Only the session journal changes; current edits, autosave, named
        // versions and the independent multi-photo batch journal stay intact.
        history_.clear();
    }
    emit stateChanged();
}
void EditorController::saveHistoryVersion(const QString &handle, const QString &name) {
    if (!editable() || exporting_ || !deferredOpen_.isEmpty())
        return;
    finishHistoryTools();
    historyError_.clear();
    const auto id = resolveHistoryHandle(handle);
    if (!id) {
        historyError_ =
            QStringLiteral("This history state is no longer available. Select a current row.");
        emit stateChanged();
        return;
    }
    // Capture the selected document before handing it to the existing catalog
    // worker. This does not seek, change the active edit or discard Redo.
    runVersionAction(VersionAction::create, {}, {}, name, *history_.document(*id));
}
