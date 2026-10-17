#include "apps/shared/PhotoDecoder.h"
#include "core/render/CpuRenderer.h"
#include "tests/ux/QueuedCallGate.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QColorSpace>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QSet>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTest>
#include <algorithm>

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
QImage samples(QImage image) {
    image = image.convertToFormat(QImage::Format_RGBA8888);
    image.setColorSpace({});
    image.setDevicePixelRatio(1);
    return image;
}
QImage exportReference(Context &c, const luma::EditDocument &document) {
    const auto decoded = luma::PhotoDecoder().decode(c.editor.sourcePath().toStdString());
    require(decoded.image.width <= 512 && decoded.image.height <= 512 &&
                decoded.image.alpha.empty(),
            "PNG snapshot oracle requires a small opaque public fixture");
    const auto pixels =
        luma::renderCPU(decoded.image, document, decoded.metadata.orientation, {}, {},
                        decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
    return QImage(pixels.rgba.data(), pixels.width, pixels.height, pixels.width * 4,
                  QImage::Format_RGBA8888)
        .copy();
}
void rendered(Context &c) {
    const auto expected = fixtureReference(c);
    c.driver->wait(
        [&] {
            auto *image = c.driver->item("develop.preview");
            return !c.editor.busy() && image->isVisible() &&
                   image->property("status").toInt() == 1 &&
                   image->property("source").toUrl().toString() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == expected;
        },
        "preview matches final asset and exact EditDocument");
    c.driver->frame();
}
void chooseTarget(Context &c, bool hsl) {
    if (hsl) {
        c.reveal("hsl.targetMode");
        c.driver->choose("hsl.targetMode", 1);
    }
    const auto selector = hsl ? QString("hsl.target") : QString("curves.target");
    c.reveal(selector);
    if (c.editor.targetedTool() != (hsl ? "hsl.saturation" : "curve.rgb"))
        c.driver->click(selector);
    require(c.editor.targetedTool() == (hsl ? "hsl.saturation" : "curve.rgb"),
            "Targeted tool selector did not choose the requested tool");
}
void drop(Context &c, const QString &path) {
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    const QPoint position(c.window->width() / 2, c.window->height() / 2);
    QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(c.window, &enter);
    require(enter.isAccepted(), "Targeted photo-switch drag-enter was not accepted");
    QDragMoveEvent move(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(c.window, &move);
    require(move.isAccepted(), "Targeted photo-switch drag-move was not accepted");
    QDropEvent event(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(c.window, &event);
    require(event.isAccepted(), "Targeted photo-switch single URL drop was not accepted");
    c.driver->log({{"action", "qt_window_url_drop"}, {"path", path}});
}
QString visibleFilmstripCard(Context &c, int row) {
    auto &d = *c.driver;
    d.wait(
        [&] {
            return !c.library.data(c.library.index(row), LibraryModel::AssetID)
                        .toString()
                        .isEmpty();
        },
        "filmstrip asset page available");
    const auto selector =
        "filmstrip.card." + c.library.data(c.library.index(row), LibraryModel::AssetID).toString();
    auto *view = d.item("develop.filmstrip");
    d.wait([&] { return view->property("contentWidth").toDouble() > 0; },
           "filmstrip content geometry ready");
    for (int attempt = 0; attempt < 5; ++attempt) {
        d.frame();
        if (auto *card = qobject_cast<QQuickItem *>(d.find(selector))) {
            const auto center = card->mapToItem(view, {card->width() / 2, card->height() / 2});
            if (card->isVisible() && center.x() >= 2 && center.x() < view->width() - 2 &&
                center.y() >= 2 && center.y() < view->height() - 2)
                return selector;
        }
        const auto content = view->property("contentWidth").toDouble();
        const auto spacing = view->property("spacing").toDouble();
        const auto count = view->property("count").toInt();
        require(count > row && content > view->width(), "Filmstrip has no valid scroll range");
        const auto stride = (content + spacing) / count;
        const auto desired =
            std::clamp((row + .5) * stride - view->width() / 2, 0., content - view->width());
        auto *bar = d.object("develop.filmstripBar");
        const auto size = bar->property("size").toDouble();
        const auto position = bar->property("position").toDouble();
        d.drag("develop.filmstripBar", {position + size / 2, .5},
               {desired / content + size / 2, .5}, 8);
        d.wait([&] { return !view->property("moving").toBool(); }, "filmstrip scrollbar settled");
        d.log({{"action", "filmstrip_scroll_to_asset"},
               {"row", row},
               {"content_x", view->property("contentX").toDouble()},
               {"desired", desired}});
    }
    require(false, "Could not scroll the requested filmstrip card into the actual viewport");
    return {};
}
void exportCompleted(Context &c) {
    c.driver->wait([&] { return !c.exporter.working(); }, "targeted export completed", 60000);
    require(c.exporter.error().isEmpty() && c.exporter.failures().isEmpty() &&
                c.exporter.total() == 1 && c.exporter.completed() == 1 &&
                c.exporter.succeeded() == 1 && c.exporter.skipped() == 0,
            "Targeted snapshot export failed: " + c.exporter.error() +
                c.exporter.failures().join(';'));
}
void targetedExport(Context &c, bool hsl) {
    auto &d = *c.driver;
    const auto name = hsl ? QString("hsl-snapshot") : QString("curve-snapshot");
    // A real initial export persists the exact UI configuration. Opening a new
    // Export dialog otherwise resets its draft form from exporter.settings.
    const auto before = c.editor.currentSettings();
    const auto baseline = exportReference(c, before);
    const auto initial = c.exportPNG(name);
    require(samples(QImage(initial)) == baseline,
            "PNG snapshot positive control does not match its pre-edit document");
    require(c.exporter.settings().value("format").toInt() == 1 &&
                c.exporter.settings().value("colorSpace").toInt() == 0 &&
                c.exporter.settings().value("resize").toInt() == 0 &&
                c.exporter.settings().value("outputMedium").toInt() == 0 &&
                c.exporter.settings().value("watermarkKind").toInt() == 0 &&
                c.exporter.settings().value("conflict").toInt() == 1,
            "PNG snapshot oracle configuration has unexpected processing or collision policy");
    chooseTarget(c, hsl);
    c.saved();
    QSignalSpy saves(&c.editor, &EditorController::editsSaved);
    QElapsedTimer debounceWindow;
    debounceWindow.start();
    d.drag("develop.targetedArea", {.5, .5}, {.5, .35});
    const auto after = c.editor.currentSettings();
    auto expected = before;
    if (hsl)
        expected.hsl = after.hsl;
    else
        expected.curves[0] = after.curves[0];
    require(after != before && after == expected,
            "Immediate-export gesture did not change exactly its targeted group");
    require(c.editor.saving() && saves.isEmpty(),
            "Immediate-export fixture missed the pending-save boundary");
    d.click("exportButton");
    d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
           "immediate targeted export settings opened");
    require(c.editor.saving() && saves.isEmpty(),
            "Immediate-export fixture saved before the start action");
    QObject observer;
    bool pendingAtStart = false;
    qint64 startDelay = -1;
    QObject::connect(&c.exporter, &ExportController::changed, &observer, [&] {
        if (c.exporter.waitingForSave() && startDelay < 0) {
            startDelay = debounceWindow.elapsed();
            // Conservative proof: even the FIRST gesture edit is younger than
            // EditorController's 180 ms timer, and every later edit restarts it.
            // This is a fixture-boundary assertion, not a UI latency budget.
            pendingAtStart =
                pendingAtStart || (c.editor.saving() && saves.isEmpty() && startDelay < 180);
        }
    });
    d.click("export.current");
    require(pendingAtStart, "Export did not enter its wait-for-pending-edits boundary");
    exportCompleted(c);
    const auto finalPixels = exportReference(c, after);
    require(finalPixels != baseline,
            "Targeted PNG fixture does not visually distinguish final and stale snapshots");
    const auto output = c.root + "/exports/" + name + "-2.png";
    const auto actual = samples(QImage(output));
    require(!actual.isNull() && actual == finalPixels && actual != baseline,
            "Immediate targeted export used stale or partial edits");
    const auto stored =
        luma::Catalog(c.catalogFile.toStdString()).assetByID(c.editor.photoID().toStdString());
    require(stored && stored->edits == after && c.editor.currentSettings() == after,
            "Export snapshot and final saved editor document disagree");
    d.log({{"action", "targeted_pending_save_export_verified"},
           {"target", hsl ? "hsl.saturation" : "curve.rgb"},
           {"pending_at_start", pendingAtStart},
           {"gesture_start_to_export_wait_ms", startDelay},
           {"snapshot_file", output}});
}
void curveChannelWhileHeld(Context &c) {
    auto &d = *c.driver;
    const KeyboardNavigation navigation;
    c.openFirst();
    c.reveal("curves.graph");
    const auto initial = c.editor.currentSettings();
    d.press("curves.gesture", {.45, .55});
    d.move("curves.gesture", {.6, .3});
    const auto partial = c.editor.currentSettings();
    require(partial != initial && d.object("curves.gesture")->property("dragging").toBool(),
            "Channel-switch fixture did not hold a real Curve drag");
    auto *combo = d.item("curves.channel");
    // A second mouse press while Left is held would be an invalid input stream.
    // Reach the real ComboBox by keyboard and switch with its native key handler.
    for (int tab = 0; !combo->hasActiveFocus() && tab < 100; ++tab)
        d.key(Qt::Key_Tab, Qt::ShiftModifier);
    require(combo->hasActiveFocus(), "Cannot reach Curve channel selector by real Tab input");
    int index = combo->property("currentIndex").toInt();
    require(index == 0, "Curve channel fixture did not begin at RGB");
    for (int cycle = 0; cycle < 20; ++cycle) {
        for (int step = 0; step < 6; ++step) {
            const bool forward = step < 3;
            d.key(forward ? Qt::Key_Down : Qt::Key_Up);
            index += forward ? 1 : -1;
            require(combo->property("currentIndex").toInt() == index,
                    "Closed Curve ComboBox did not acknowledge an actual arrow key");
            const auto channel =
                QString::fromUtf8(luma::curveChannels[static_cast<std::size_t>(index)].data(),
                                  static_cast<qsizetype>(
                                      luma::curveChannels[static_cast<std::size_t>(index)].size()));
            require(d.object("curves.panel")->property("channel").toString() == channel &&
                        !d.object("curves.gesture")->property("dragging").toBool() &&
                        d.object("curves.panel")->property("selected").toInt() == -1,
                    "Channel switch retained a stale drag, selection or binding");
            d.move("curves.gesture", {.7, .2});
            require(c.editor.currentSettings() == partial,
                    "A cancelled Curve drag mutated a newly selected channel");
        }
    }
    d.release("curves.gesture", {.7, .2});
    require(c.editor.currentSettings() == partial, "Curve release applied stale channel data");
    d.click("main.undoButton");
    require(c.editor.currentSettings() == initial && !c.editor.canUndo(),
            "Channel changes polluted the one-gesture history entry");
    for (int channel = 0; channel < 4; ++channel) {
        c.reveal("curves.channel");
        d.choose("curves.channel", channel);
        c.reveal("curves.graph");
        d.drag("curves.gesture", {.4, .6}, {.55, .3});
        const auto changed = c.editor.currentSettings();
        auto expected = initial;
        expected.curves[static_cast<std::size_t>(channel)] =
            changed.curves[static_cast<std::size_t>(channel)];
        require(changed != initial && changed == expected,
                "Fresh Curve drag after rapid switching edited a stale channel");
        d.click("main.undoButton");
        require(c.editor.currentSettings() == initial && !c.editor.canUndo(),
                "Fresh channel gesture did not retain independent Undo history");
    }
    rendered(c);
}
void targetedPhotoSwitch(Context &c) {
    auto &d = *c.driver;
    c.openFirst();
    c.saved();
    const auto first = c.editor.sourcePath();
    const auto probe = c.library.data(c.library.index(2), LibraryModel::Path).toUrl().toLocalFile();
    QSignalSpy loads(&c.editor, &EditorController::photoLoaded);
    drop(c, probe);
    d.wait([&] { return !c.editor.busy() && c.editor.sourcePath() == probe; },
           "URL-drop transport positive control loaded");
    c.loaded();
    require(loads.count() == 1, "One positive-control drop did not activate exactly one photo");
    d.click(c.card(0, true));
    d.wait([&] { return !c.editor.busy() && c.editor.sourcePath() == first; },
           "targeted fixture returned to its original photo");
    c.loaded();
    for (int cycle = 0; cycle < 2; ++cycle) {
        const bool hsl = cycle == 1;
        chooseTarget(c, hsl);
        c.saved();
        const auto before = c.editor.currentSettings();
        const auto oldID = c.editor.photoID();
        const int row = cycle + 1;
        d.wait(
            [&] {
                return !c.library.loading() &&
                       !c.library.data(c.library.index(row), LibraryModel::AssetID)
                            .toString()
                            .isEmpty() &&
                       !c.library.data(c.library.index(row), LibraryModel::Path).toUrl().isEmpty();
            },
            "next targeted asset's current model page ready");
        const auto nextID = c.library.data(c.library.index(row), LibraryModel::AssetID).toString();
        const auto nextPath =
            c.library.data(c.library.index(row), LibraryModel::Path).toUrl().toLocalFile();
        const auto nextAsset =
            luma::Catalog(c.catalogFile.toStdString()).assetByID(nextID.toStdString());
        require(nextAsset && !nextPath.isEmpty() && nextID != oldID,
                "Targeted switch needs a distinct known next-asset document");
        d.press("develop.targetedArea", {.5, .5});
        d.move("develop.targetedArea", {.5, .4});
        const auto partial = c.editor.currentSettings();
        require(partial != before &&
                    d.object("develop.targetedArea")->property("adjusting").toBool(),
                "Targeted switch fixture did not hold an accepted adjusting pointer");
        drop(c, nextPath);
        require(!c.editor.busy() && c.editor.sourcePath() != nextPath && c.editor.saving() &&
                    d.object("develop.targetedArea")->property("adjusting").toBool(),
                "URL-drop fixture cancelled the pointer before the deferred photo switch");
        d.wait([&] { return !c.editor.busy() && c.editor.sourcePath() == nextPath; },
               "URL drop switched photos while the targeted pointer remained held");
        c.loaded();
        require(!d.object("develop.targetedArea")->property("adjusting").toBool(),
                "Photo switch retained a targeted MouseArea gesture");
        d.move("develop.targetedArea", {.5, .2});
        d.release("develop.targetedArea", {.5, .2});
        require(c.editor.photoID() == nextID && c.editor.currentSettings() == nextAsset->edits &&
                    !c.editor.canUndo(),
                "Continuing the old pointer gesture mutated the new photo/history");
        const auto previous =
            luma::Catalog(c.catalogFile.toStdString()).assetByID(oldID.toStdString());
        require(previous && previous->edits == partial,
                "Targeted switch failed to save the old photo's final accepted state");
        rendered(c);
    }
}
void cancelCompletionBoundary(Context &c) {
    auto &d = *c.driver;
    c.openFirst();
    c.saved();
    rendered(c);
    const auto next = c.card(1, true);
    const auto path = c.library.data(c.library.index(1), LibraryModel::Path).toUrl().toLocalFile();
    const auto nextID = c.library.data(c.library.index(1), LibraryModel::AssetID).toString();
    QSignalSpy loads(&c.editor, &EditorController::photoLoaded);
    QSignalSpy clicked(d.object("editor.cancelLoad"), SIGNAL(clicked()));
    require(clicked.isValid(), "Decode Cancel button has no observable clicked signal");
    // For opaque PNG decoding RasterDecoder queues exactly one final progress(1)
    // before returning to SessionService. With saves and renders already idle,
    // the following Editor MetaCall is its real successful load completion.
    QueuedCallGate gate(
        c.editor, [&] { return c.editor.busy() && c.editor.progress() == 1; },
        [&] {
            require(c.editor.sourcePath() != path && loads.isEmpty(),
                    "Completion gate ran after the new photo had already been installed");
            d.frame();
            require(d.item("editor.cancelLoad")->isVisible() &&
                        d.item("editor.cancelLoad")->isEnabled(),
                    "Late decode Cancel is not an available actual UI action");
            d.log({{"action", "cancel_with_successful_load_completion_held"}, {"next", path}});
            QTest::mouseClick(c.window, Qt::LeftButton, Qt::NoModifier,
                              d.point("editor.cancelLoad"));
            require(clicked.count() == 1, "Actual Cancel input was not acknowledged exactly once");
        });
    d.click(next);
    d.wait([&] { return gate.caught; }, "decode progress-to-completion scheduler boundary");
    if (gate.failure)
        std::rethrow_exception(gate.failure);
    c.loaded();
    require(c.editor.sourcePath() == path && c.editor.photoID() == nextID && loads.count() == 1 &&
                c.editor.error().isEmpty() && !c.editor.saving() && !c.editor.canUndo(),
            "Late Cancel and completed load left inconsistent metadata/history or duplicate "
            "activation");
    const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
    require(active && active->id == nextID.toStdString() &&
                active->edits == c.editor.currentSettings(),
            "Cancel/completion race left active catalog state inconsistent with the editor");
    rendered(c);
}
void hundredFilmstripActivations(Context &c) {
    auto &d = *c.driver;
    c.import(100);
    QSignalSpy loads(&c.editor, &EditorController::photoLoaded);
    d.doubleClick(c.card(0));
    c.loaded();
    QSet<QString> activated{c.editor.photoID()};
    QString expectedFinalID;
    QElapsedTimer elapsed;
    elapsed.start();
    for (int row = 1; row < 100; ++row) {
        const auto card = visibleFilmstripCard(c, row);
        d.wait(
            [&] {
                return !c.library.loading() &&
                       !c.library.data(c.library.index(row), LibraryModel::AssetID)
                            .toString()
                            .isEmpty() &&
                       !c.library.data(c.library.index(row), LibraryModel::Path).toUrl().isEmpty();
            },
            "next targeted asset's current model page ready");
        const auto nextID = c.library.data(c.library.index(row), LibraryModel::AssetID).toString();
        const auto nextPath =
            c.library.data(c.library.index(row), LibraryModel::Path).toUrl().toLocalFile();
        const auto previousLoads = loads.count();
        d.click(card);
        // Await only decode admission/completion, not the old/new preview render.
        // The busy guard intentionally refuses further activations during decode.
        d.wait([&] { return !c.editor.busy() && c.editor.photoID() == nextID; },
               "filmstrip photo activation " + QString::number(row + 1));
        require(loads.count() == previousLoads + 1,
                "One filmstrip input did not activate exactly one asset at row " +
                    QString::number(row));
        expectedFinalID = nextID;
        require(c.editor.sourcePath() == nextPath &&
                    c.editor.filename() == QFileInfo(nextPath).fileName() &&
                    c.editor.currentSettings() == luma::EditDocument{} && !c.editor.canUndo(),
                "Rapid filmstrip activation crossed metadata or asset history");
        require(!activated.contains(nextID), "Filmstrip did not activate 100 distinct assets");
        activated.insert(nextID);
    }
    c.loaded();
    rendered(c);
    // photoOpened reloads the asynchronous Library page even after the editor is
    // ready. Resolve that current page before reading its final row identity.
    d.wait(
        [&] {
            return !c.library.loading() &&
                   !c.library.data(c.library.index(99), LibraryModel::AssetID).toString().isEmpty();
        },
        "final filmstrip model page reloaded after activation");
    const auto finalID = c.library.data(c.library.index(99), LibraryModel::AssetID).toString();
    const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
    d.log({{"action", "filmstrip_final_identity"},
           {"loads", loads.count()},
           {"distinct_assets", static_cast<qint64>(activated.size())},
           {"expected_id", expectedFinalID},
           {"model_id", finalID},
           {"editor_id", c.editor.photoID()},
           {"active_id", active ? QString::fromStdString(active->id) : QString{}},
           {"error", c.editor.error()}});
    require(activated.size() == 100 && loads.count() == 100 && active &&
                finalID == expectedFinalID && active->id == finalID.toStdString() &&
                c.editor.photoID() == finalID && c.editor.error().isEmpty(),
            "Rapid filmstrip activation did not finish at exactly its 100th asset");
    d.log({{"action", "hundred_real_filmstrip_activations_verified"},
           {"activations", loads.count()},
           {"distinct_assets", static_cast<qint64>(activated.size())},
           {"elapsed_ms", elapsed.elapsed()},
           {"waited_for_intermediate_render", false},
           {"input", "mouse and actual horizontal scrollbar; no trackpad claim"}});
}
void filterWhilePagePending(Context &c) {
    auto &d = *c.driver;
    c.import(256);
    d.frame();
    d.wait([&] { return c.library.cachedPages() == 1; }, "initial Library page cache populated");
    const auto expected = [&] {
        luma::LibraryFilter filter;
        filter.search = "Photo-0255";
        return luma::Catalog(c.catalogFile.toStdString()).libraryIDs(filter);
    }();
    require(expected.size() == 1, "Late-page filter fixture needs one independently known result");
    const auto expectedID = QString::fromStdString(expected.front());
    auto *grid = d.item("library.grid");
    QSignalSpy resets(&c.library, &QAbstractItemModel::modelReset);
    QueuedCallGate gate(
        c.library,
        [&] {
            return !c.library.loading() && !c.library.working() && !c.library.selecting() &&
                   c.library.cachedPages() == 1;
        },
        [&] {
            d.log({{"action", "old_library_page_callback_held"},
                   {"cached_pages", static_cast<int>(c.library.cachedPages())}});
            const auto before = resets.count();
            d.type("library.search", "Photo-0255");
            d.wait(
                [&] {
                    return resets.count() > before && !c.library.loading() &&
                           c.library.count() == 1 &&
                           c.library.data(c.library.index(0), LibraryModel::AssetID).toString() ==
                               expectedID;
                },
                "new filtered page ready before old page completion returns");
            require(c.library.cachedPages() == 1, "New filter populated an unexpected extra page");
        });
    d.wheel("library.grid", -20000);
    d.wait([&] { return gate.caught; }, "uncached Library page completion boundary");
    if (gate.failure)
        std::rethrow_exception(gate.failure);
    d.wait([&] { return !grid->property("moving").toBool(); }, "filter grid settled");
    d.frame();
    require(!c.library.loading() && c.library.count() == 1 && c.library.cachedPages() == 1 &&
                c.library.data(c.library.index(0), LibraryModel::AssetID).toString() ==
                    expectedID &&
                c.library.data(c.library.index(0), LibraryModel::Filename).toString() ==
                    "Photo-0255.png" &&
                c.library.error().isEmpty(),
            "Old pending Library page overwrote or polluted the new filtered model");
    require(d.find(c.card(0)) != nullptr, "Final filtered Library card was never visible");
}
} // namespace
bool previewExtraScenario(Context &c, const QString &id) {
    if (id == "UX-ADV-003") {
        curveChannelWhileHeld(c);
    } else if (id == "UX-ADV-009") {
        targetedPhotoSwitch(c);
    } else if (id == "UX-ADV-015" || id == "UX-RACE-006") {
        c.openFirst();
        targetedExport(c, false);
        targetedExport(c, true);
    } else if (id == "UX-PRV-007") {
        cancelCompletionBoundary(c);
    } else if (id == "UX-PRV-008") {
        hundredFilmstripActivations(c);
    } else if (id == "UX-RACE-008") {
        filterWhilePagePending(c);
    } else {
        return false;
    }
    return true;
}
} // namespace ux
