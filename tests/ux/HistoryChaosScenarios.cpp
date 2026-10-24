#include "core/render/DevelopPipeline.h"
#include "tests/ux/HistoryChaos.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QStyleHints>
#include <cmath>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace ux {
namespace {
struct KeyboardNavigation {
    Qt::TabFocusBehavior previous = QGuiApplication::styleHints()->tabFocusBehavior();
    KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    }
    ~KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(previous);
    }
};
QByteArray digest(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read generated chaos original");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "Cannot hash complete generated chaos original");
    return hash.result();
}
class Trace {
  public:
    explicit Trace(const QString &path) : file_(path) {
        require(file_.open(QIODevice::WriteOnly | QIODevice::NewOnly),
                "Cannot create a fresh chaos execution journal");
        const auto directory = QFile::encodeName(QFileInfo(path).absolutePath());
        const int descriptor = ::open(directory.constData(), O_RDONLY);
        if (descriptor < 0)
            throw std::runtime_error("Cannot open chaos execution directory for fsync");
        const int synced = ::fsync(descriptor);
        const int closed = ::close(descriptor);
        require(synced == 0 && closed == 0, "Cannot fsync/close chaos execution directory");
    }
    void append(const QJsonObject &row) {
        const auto bytes = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
        require(file_.write(bytes) == bytes.size() && file_.flush() && ::fsync(file_.handle()) == 0,
                "Cannot fsync chaos execution intent/result");
    }

  private:
    QFile file_;
};
struct Original {
    QString path;
    QByteArray hash;
    luma::EditDocument document;
};
struct Baseline {
    QString source, asset;
    std::vector<std::string> members, selected;
    QMap<QString, Original> originals;
};
Baseline baseline(Context &c, const HistoryChaosPlan &plan) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    Baseline before{c.editor.sourcePath(),
                    c.editor.photoID(),
                    catalog.libraryIDs({}),
                    c.library.selectionIDs(),
                    {}};
    require(before.members.size() == 10 && !before.asset.isEmpty() &&
                c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo() &&
                !c.editor.canRedo() && !c.editor.original(),
            "Chaos fixture requires ten generated assets and a clean full document/history");
    const auto expectedHashes = plan.header.value("fixture_sha256").toArray();
    for (const auto &id : before.members) {
        const auto asset = catalog.assetByID(id);
        if (!asset || asset->edits != luma::EditDocument{})
            throw std::runtime_error("Chaos baseline asset is missing/edited");
        const auto path = QString::fromStdString(asset->path.string());
        const auto hash = digest(path);
        const auto index = QFileInfo(path).completeBaseName().remove("Photo-").toInt();
        require(index >= 0 && index < 10 &&
                    path == c.fixture + QString("/Photo-%1.png").arg(index, 4, 10, QChar('0')) &&
                    QString::fromLatin1(hash.toHex()) == expectedHashes[index].toString() &&
                    asset->checksum == QString::fromLatin1(hash.toHex()).toStdString(),
                "Chaos active catalog does not match the prevalidated generated fixture set");
        before.originals.insert(QString::fromStdString(id), {path, hash, asset->edits});
    }
    return before;
}
void focus(Context &c, const QString &control) {
    const auto name = "develop." + control + "Slider";
    auto *slider = c.driver->item(name);
    const auto before = c.editor.currentSettings();
    if (!slider->hasActiveFocus()) {
        c.reveal(name);
        for (int tab = 0; tab < 240 && !slider->hasActiveFocus(); ++tab)
            c.driver->key(Qt::Key_Tab);
        c.reveal(name);
    }
    require(slider->hasActiveFocus() && c.window->activeFocusItem() == slider &&
                QGuiApplication::focusWindow() == c.window && slider->isVisible() &&
                slider->isEnabled() && slider->property("stepSize").toDouble() == 1 &&
                slider->property("from").toDouble() == -100 &&
                slider->property("to").toDouble() == 100 && !slider->property("pressed").toBool() &&
                c.editor.currentSettings() == before,
            "Real chaos Tab navigation missed its integer Slider or changed the document");
}
void invariant(Context &c, const Baseline &before, const HistoryChaosModel &model) {
    const auto &expected = model.current();
    require(c.editor.currentSettings() == expected &&
                historyDocumentHash(c.editor.currentSettings()) == historyDocumentHash(expected) &&
                c.editor.canUndo() == (model.undoDepth() > 0) &&
                c.editor.canRedo() == (model.redoDepth() > 0) &&
                c.editor.photoID() == before.asset && c.editor.sourcePath() == before.source &&
                c.editor.hasPhoto() && !c.editor.original() &&
                c.editor.detailDiagnostic().isEmpty() &&
                c.driver->object("main.filename")->property("text").toString() ==
                    QFileInfo(before.source).fileName() &&
                !c.editor.busy() && !c.editor.editingLocked() && c.editor.error().isEmpty() &&
                !c.window->property("libraryMode").toBool() && c.window->isVisible() &&
                !c.library.working() && !c.library.importing() && !c.library.selecting() &&
                c.library.error().isEmpty() && c.library.count() == 10 &&
                c.library.selectionIDs() == before.selected &&
                c.library.selectedCount() == static_cast<int>(before.selected.size()) &&
                !c.batch.working() && !c.batch.canUndo() && !c.batch.canRedo() &&
                !c.exporter.working() && !c.metrics.json().value("hard_hang").toBool(),
            "Chaos full document/hash/history/identity/model/busy invariant diverged");
    const QMap<QString, double> displayed{{"contrast", expected.contrast},
                                          {"shadows", expected.shadows},
                                          {"vibrance", expected.vibrance}};
    for (auto it = displayed.cbegin(); it != displayed.cend(); ++it) {
        auto *slider = c.driver->object("develop." + it.key() + "Slider");
        require(!slider->property("pressed").toBool() && slider->property("value").isValid() &&
                    std::isfinite(slider->property("value").toDouble()) &&
                    slider->property("value").toDouble() == it.value(),
                "Chaos left a gesture pressed or a stale/non-finite displayed value");
    }
    for (const auto *dialog :
         {"preferencesPanel", "library.importDialog", "exportPanel", "restart.wait", "batch.wait"})
        require(!c.driver->object(dialog)->property("visible").toBool(),
                "Scoped history chaos opened an unexpected modal/popup");
    const luma::Catalog catalog(c.catalogFile.toStdString());
    const auto active = catalog.activeAsset();
    require(catalog.libraryIDs({}) == before.members && active &&
                active->id == before.asset.toStdString(),
            "Chaos changed durable membership or active asset identity");
    for (auto it = before.originals.cbegin(); it != before.originals.cend(); ++it) {
        const auto asset = catalog.assetByID(it.key().toStdString());
        require(asset && QString::fromStdString(asset->path.string()) == it->path &&
                    digest(it->path) == it->hash &&
                    asset->checksum == QString::fromLatin1(it->hash.toHex()).toStdString() &&
                    (it.key() == before.asset || asset->edits == it->document),
                "Chaos changed an original SHA/path or another asset's full durable document");
    }
}
void checkpoint(Context &c, const Baseline &before, const HistoryChaosModel &model) {
    const auto &expected = model.current();
    // No saved()/endEdit(): a stale pressed gesture may not be repaired by the oracle.
    c.driver->wait(
        [&] {
            const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
            return !c.editor.saving() && active && active->id == before.asset.toStdString() &&
                   active->edits == expected;
        },
        "chaos natural autosave reached the exact full expected document");
    const auto image = fixtureReference(c, expected, before.source);
    const bool metalEligible =
        c.editor.metalPreview() && luma::DevelopPipeline(expected).gpuCompatible();
    try {
        c.driver->wait(
            [&] {
                auto *preview = c.driver->item("develop.preview");
                return preview->isVisible() && preview->property("status").toInt() == 1 &&
                       preview->property("source").toUrl().toString() == c.editor.previewUrl() &&
                       previewPixelsAgree(c.provider->requestImage({}, nullptr, {}), image,
                                          metalEligible);
            },
            "chaos Ready preview reached the independent full document within its renderer budget");
    } catch (...) {
        recordPreviewDifference(c, image, "History chaos durable checkpoint");
        throw;
    }
    invariant(c, before, model);
    c.checkData();
}
} // namespace
void runHistoryChaos(Context &c, const HistoryChaosPlan &plan) {
    checkPreviewPixelOracle();
    std::cerr << "CHAOS seed=" << plan.seed << " actions=" << plan.actions << '\n';
    require(static_cast<int>(plan.rows.size()) == plan.actions && plan.actions >= 1,
            "Chaos plan/action count mismatch before fixture input");
    const KeyboardNavigation navigation;
    // Context::openFirst is established explicit generated chooser-result setup;
    // no OS chooser, private catalog or user photographs are used/claimed.
    c.driver->log({{"action", "chaos_generated_fixture_setup"},
                   {"semantic_action_count_includes_setup", false}});
    c.openFirst();
    c.driver->wait([&] { return !c.library.loading() && c.library.count() == 10; },
                   "chaos post-photoOpened metadata settled");
    const auto before = baseline(c, plan);
    HistoryChaosModel model;
    checkpoint(c, before, model);
    Trace trace(c.root + "/chaos-execution.jsonl");
    trace.append({{"kind", "start"},
                  {"seed", QString::number(plan.seed)},
                  {"actions", plan.actions},
                  {"schedule_sha256", plan.scheduleSHA256}});
    int completed = 0, noops = 0, activeIndex = 0;
    QString stage = "before_action";
    QJsonArray checkpoints;
    try {
        for (const auto &row : plan.rows) {
            const int index = row.value("index").toInt();
            activeIndex = index;
            stage = "before_action";
            require(index == completed + 1 &&
                        row.value("before").toString() == historyDocumentHash(model.current()),
                    "Chaos execution schedule/index/pre-state mismatch");
            invariant(c, before, model);
            trace.append({{"kind", "intent"},
                          {"seed", QString::number(plan.seed)},
                          {"index", index},
                          {"action", row}});
            const auto op = row.value("op").toString();
            const auto control = row.value("control").toString();
            const int delta = row.value("delta").toInt();
            stage = "ui_input";
            if (op == "edit") {
                focus(c, control);
                c.driver->key(delta == 1 ? Qt::Key_Right : Qt::Key_Left);
            } else {
                require(op == "undo" || op == "redo", "Unknown scoped chaos op");
                c.driver->key(Qt::Key_Z, op == "redo" ? Qt::ControlModifier | Qt::ShiftModifier
                                                      : Qt::ControlModifier);
            }
            model.apply(op, control, delta);
            stage = "live_oracle";
            require(row.value("after").toString() == historyDocumentHash(model.current()) &&
                        row.value("undo").toInt() == model.undoDepth() &&
                        row.value("redo").toInt() == model.redoDepth(),
                    "Chaos independent model no longer matches the persisted schedule");
            c.driver->frame(); // actual own-window animation/GUI loop progress after every action
            invariant(c, before, model);
            if (!row.value("changed").toBool())
                ++noops;
            completed = index;
            trace.append(
                {{"kind", "observed"},
                 {"index", index},
                 {"actual_document_sha256", historyDocumentHash(c.editor.currentSettings())},
                 {"expected_document_sha256", row.value("after")},
                 {"can_undo", c.editor.canUndo()},
                 {"can_redo", c.editor.canRedo()}});
            if (index % 100 == 0 || index == plan.actions) {
                stage = "durable_checkpoint";
                checkpoint(c, before, model);
                checkpoints.append(
                    QJsonObject{{"action", index},
                                {"document_sha256", historyDocumentHash(model.current())},
                                {"undo_depth", model.undoDepth()},
                                {"redo_depth", model.redoDepth()}});
                trace.append({{"kind", "durable_checkpoint"},
                              {"index", index},
                              {"document_sha256", historyDocumentHash(model.current())}});
            }
        }
        require(completed == plan.actions, "Chaos executed fewer than the configured actions");
        stage = "final_checkpoint";
        checkpoint(c, before, model);
        trace.append({{"kind", "complete"},
                      {"actions", completed},
                      {"final_document_sha256", historyDocumentHash(model.current())}});
        writeJson(c.root + "/measurements.json",
                  {{"scenario", "UX-UND-017"},
                   {"seed", QString::number(plan.seed)},
                   {"requested_actions", plan.actions},
                   {"completed_actions", completed},
                   {"intentional_noop_actions", noops},
                   {"schedule_sha256", plan.scheduleSHA256},
                   {"final_document_sha256", historyDocumentHash(model.current())},
                   {"checkpoint_interval_actions", 100},
                   {"checkpoints", checkpoints},
                   {"history_cap_preamble_complete", plan.actions >= 611},
                   {"final_history_sweep_complete", plan.actions >= 1011},
                   {"default_10000_action_baseline_satisfied", plan.actions >= 10000},
                   {"scope", "three integer Basic sliders, discrete keys, Undo/Redo only"},
                   {"action_count_definition",
                    "semantic schedule rows; excludes fixture/Tab/scroll/frame/checkpoint inputs"},
                   {"pointer_gesture_coalescing_exercised", false},
                   {"general_chaos_monkey_or_phase06_claimed", false}});
    } catch (const std::exception &error) {
        const auto message = "CHAOS seed=" + QString::number(plan.seed) +
                             " action=" + QString::number(activeIndex) + " stage=" + stage + ": " +
                             error.what();
        std::cerr << message.toStdString() << '\n';
        try {
            trace.append({{"kind", "failure"},
                          {"seed", QString::number(plan.seed)},
                          {"last_completed", completed},
                          {"message", message}});
        } catch (...) {
            std::cerr << "CHAOS failure trace could not append; fsynced seed/schedule remain\n";
        }
        throw std::runtime_error(message.toStdString());
    }
}
} // namespace ux
