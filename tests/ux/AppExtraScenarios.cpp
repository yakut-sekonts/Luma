#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>
#include <QMap>
#include <QMimeData>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <vector>

namespace ux {
namespace {
QByteArray readFile(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read isolated lifecycle oracle: " + path);
    return file.readAll();
}
QString sha256(const QString &path) {
    return QString::fromLatin1(
        QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha256).toHex());
}
void drop(Context &c, const QString &path) {
    // Acceptance input enters the production DropArea through the QWindow drag/drop
    // boundary. No call to openFile(), and no claim of native Finder drag coverage.
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(path)});
    const QPoint point(c.window->width() / 2, c.window->height() / 2);
    QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(c.window, &enter);
    require(enter.isAccepted(), "Lifecycle URL drag-enter was not accepted");
    QDragMoveEvent move(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(c.window, &move);
    require(move.isAccepted(), "Lifecycle URL drag-move was not accepted");
    QDropEvent dropped(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(c.window, &dropped);
    require(dropped.isAccepted(), "Lifecycle URL drop was not accepted");
    c.driver->log({{"action", "qt_window_url_drop"}, {"source", path}});
}
void quit(Context &c) {
    c.driver->quitShortcut();
    c.driver->wait(
        [&] {
            return !c.window->isVisible() && !c.editor.busy() && !c.editor.saving() &&
                   !c.library.importing() && !c.exporter.working() && !c.batch.working();
        },
        "lifecycle coordinated process shutdown");
    c.checkData();
}
void edit(Context &c) {
    auto &d = *c.driver;
    const auto before = c.editor.currentSettings();
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.58, .5});
    c.reveal("develop.contrastSlider");
    d.drag("develop.contrastSlider", d.sliderThumb("develop.contrastSlider"), {.61, .5});
    const auto after = c.editor.currentSettings();
    auto expected = before;
    expected.exposure = after.exposure;
    expected.contrast = after.contrast;
    require(after == expected && after.exposure > .2 && after.exposure < 2 && after.contrast > 5 &&
                after.contrast < 45,
            "Lifecycle fixture must change only Exposure and Contrast through UI input");
    c.saved();
}
QQuickItem *adjustmentFlickable(Context &c) {
    auto *object = qvariant_cast<QObject *>(
        c.driver->object("develop.adjustmentsScroll")->property("contentItem"));
    auto *result = qobject_cast<QQuickItem *>(object);
    require(result, "Adjustment ScrollView lacks its actual Flickable");
    return result;
}
struct Scroll {
    double offset = 0;
    double origin = 0;
    double content = 0;
    double viewport = 0;
    QJsonObject json() const {
        return {
            {"offset", offset}, {"origin", origin}, {"content", content}, {"viewport", viewport}};
    }
};
Scroll scroll(QQuickItem &item, bool horizontal) {
    for (const auto *property :
         {horizontal ? "contentX" : "contentY", horizontal ? "originX" : "originY",
          horizontal ? "contentWidth" : "contentHeight", "moving"})
        require(item.property(property).isValid(),
                "Lifecycle scroll oracle was given a non-Flickable item");
    const Scroll value{item.property(horizontal ? "contentX" : "contentY").toDouble(),
                       item.property(horizontal ? "originX" : "originY").toDouble(),
                       item.property(horizontal ? "contentWidth" : "contentHeight").toDouble(),
                       horizontal ? item.width() : item.height()};
    require(std::isfinite(value.offset) && std::isfinite(value.origin) &&
                std::isfinite(value.content) && std::isfinite(value.viewport) &&
                value.content >= 0 && value.viewport > 0,
            "Lifecycle scroll geometry became non-finite or empty");
    const double maximum = value.origin + std::max(0.0, value.content - value.viewport);
    require(value.offset >= value.origin - 1 && value.offset <= maximum + 1,
            "Lifecycle scroll offset escaped the current valid range");
    require(!item.property("moving").toBool(), "Lifecycle scroll remains in motion");
    return value;
}
void dragScrollbar(Context &c, const QString &selector, QQuickItem &viewport, bool horizontal,
                   double fraction) {
    auto &d = *c.driver;
    auto *bar = d.item(selector);
    auto *thumb =
        qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(bar->property("contentItem")));
    require(thumb && bar->isVisible() && bar->width() > 0 && bar->height() > 0,
            "Lifecycle scroll input requires a visible actual scrollbar thumb");
    const QPointF center = thumb->mapToItem(bar, {thumb->width() / 2, thumb->height() / 2});
    const double extent = horizontal ? thumb->width() : thumb->height();
    const double available =
        bar->property(horizontal ? "availableWidth" : "availableHeight").toDouble();
    const double padding = bar->property(horizontal ? "leftPadding" : "topPadding").toDouble();
    const double travel = available - extent;
    require(travel > 0, "Lifecycle fixture has no scrollbar thumb travel");
    const double target = padding + travel * fraction + extent / 2;
    const QPointF from(center.x() / bar->width(), center.y() / bar->height());
    const QPointF to =
        horizontal ? QPointF(target / bar->width(), .5) : QPointF(.5, target / bar->height());
    d.drag(selector, from, to);
    d.wait([&] { return !viewport.property("moving").toBool(); },
           "lifecycle scrollbar input settled");
    d.frame();
    const auto observed = scroll(viewport, horizontal);
    const double range = observed.content - observed.viewport;
    require(range > 0 && std::abs((observed.offset - observed.origin) / range - fraction) <=
                             2 / travel + 1e-3,
            "Lifecycle scrollbar did not acknowledge the requested nonzero offset");
}
QRectF rectangle(Context &c, const QString &selector) {
    auto *item = c.driver->item(selector);
    require(item->isVisible() && item->width() > 0 && item->height() > 0,
            "Lifecycle layout has an invisible or empty item: " + selector);
    const QRectF result(item->mapToScene({0, 0}), QSizeF(item->width(), item->height()));
    require(std::isfinite(result.x()) && std::isfinite(result.y()) &&
                std::isfinite(result.width()) && std::isfinite(result.height()),
            "Lifecycle layout has non-finite item geometry: " + selector);
    return result;
}
void contained(Context &c, const QString &selector, const QRectF &parent) {
    require(parent.adjusted(-1, -1, 1, 1).contains(rectangle(c, selector)),
            "Lifecycle item is clipped or outside its parent: " + selector);
}
using Layout = QMap<QString, QRectF>;
Layout layout(Context &c) {
    const QRectF window(0, 0, c.window->width(), c.window->height());
    const QStringList toolbar{"main.libraryButton", "main.developButton", "settingsButton",
                              "updates.bell",       "main.undoButton",    "main.redoButton",
                              "exportButton",       "openPhotoButton"};
    Layout values;
    for (const auto &selector : toolbar) {
        contained(c, selector, window);
        const auto geometry = rectangle(c, selector);
        require(geometry.width() >= 20 && geometry.height() >= 20,
                "Lifecycle primary action collapsed below a usable hit area: " + selector);
        values.insert(selector, geometry);
    }
    for (int first = 0; first < toolbar.size(); ++first)
        for (int second = first + 1; second < toolbar.size(); ++second)
            require(!values[toolbar[first]].intersects(values[toolbar[second]]),
                    "Lifecycle primary actions overlap: " + toolbar[first] + "/" + toolbar[second]);
    contained(c, "main.footer", window);
    values.insert("main.footer", rectangle(c, "main.footer"));
    if (c.window->property("libraryMode").toBool()) {
        for (const auto *selector :
             {"library.pane", "library.grid", "library.search", "library.ratingFilter",
              "libraryFlagFilter", "libraryLabelFilter", "library.importButton",
              "library.metadataActions", "library.labelChoice", "libraryAddToCollection",
              "libraryRemoveFromCollection"}) {
            contained(c, selector, window);
            values.insert(selector, rectangle(c, selector));
        }
        auto *grid = c.driver->item("library.grid");
        const double cellWidth = grid->property("cellWidth").toDouble();
        const double cellHeight = grid->property("cellHeight").toDouble();
        require(std::isfinite(cellWidth) && std::isfinite(cellHeight) && cellWidth > 0 &&
                    cellWidth <= grid->width() + 1 && cellHeight == 210,
                "Library resize produced invalid cell geometry");
        const auto actions = values["library.metadataActions"];
        std::function<void(QQuickItem *)> inspect = [&](QQuickItem *parent) {
            for (auto *child : parent->childItems()) {
                if (child->isVisible() && (child->inherits("QQuickAbstractButton") ||
                                           child->inherits("QQuickComboBox"))) {
                    const QRectF geometry(child->mapToScene({0, 0}),
                                          QSizeF(child->width(), child->height()));
                    require(geometry.width() > 0 && geometry.height() > 0 &&
                                actions.adjusted(-1, -1, 1, 1).contains(geometry),
                            "Responsive metadata action is empty or overflows its GridLayout");
                }
                inspect(child);
            }
        };
        inspect(c.driver->item("library.metadataActions"));
        scroll(*grid, false);
    } else {
        for (const auto *selector :
             {"develop.panels", "develop.presetsPane", "develop.canvasPane",
              "develop.adjustmentsPane", "develop.adjustmentsScroll", "develop.preview",
              "develop.originalButton", "develop.processedButton", "develop.filmstrip"}) {
            contained(c, selector, window);
            values.insert(selector, rectangle(c, selector));
        }
        const auto left = values["develop.presetsPane"];
        const auto center = values["develop.canvasPane"];
        const auto right = values["develop.adjustmentsPane"];
        require(left.width() >= 169 && left.width() <= 351 && center.width() >= 359 &&
                    right.width() >= 249 && right.width() <= 401 &&
                    left.right() <= center.left() + 1 && center.right() <= right.left() + 1 &&
                    values["develop.panels"].bottom() <= values["develop.filmstrip"].top() + 1 &&
                    values["develop.filmstrip"].bottom() <= values["main.footer"].top() + 1,
                "Develop panels overlap, violate split bounds or cover filmstrip/footer");
        auto *preview = c.driver->item("develop.preview");
        const double paintedWidth = preview->property("paintedWidth").toDouble();
        const double paintedHeight = preview->property("paintedHeight").toDouble();
        require(paintedWidth > 0 && paintedWidth <= preview->width() + 1 && paintedHeight > 0 &&
                    paintedHeight <= preview->height() + 1 &&
                    preview->property("status").toInt() == 1 &&
                    preview->property("source").toUrl() == c.editor.previewUrl(),
                "Develop resize lost the actual Ready preview or its aspect-fit geometry");
        scroll(*adjustmentFlickable(c), false);
        scroll(*c.driver->item("develop.filmstrip"), true);
    }
    return values;
}
bool equalLayout(const Layout &first, const Layout &second) {
    if (first.keys() != second.keys())
        return false;
    for (auto it = first.begin(); it != first.end(); ++it) {
        const auto other = second.value(it.key());
        if (std::abs(it->x() - other.x()) > .5 || std::abs(it->y() - other.y()) > .5 ||
            std::abs(it->width() - other.width()) > .5 ||
            std::abs(it->height() - other.height()) > .5)
            return false;
    }
    return true;
}
void stable(Context &c, int observationMilliseconds) {
    // A bounded settling phase followed by a separate observation window: an
    // oscillating layout cannot pass merely by reaching one acceptable frame.
    auto &d = *c.driver;
    Layout previous;
    int consecutive = 0;
    d.wait(
        [&] {
            d.frame();
            const auto current = layout(c);
            consecutive = equalLayout(previous, current) ? consecutive + 1 : 0;
            previous = current;
            return consecutive >= 8;
        },
        "eight consecutive stable lifecycle layout frames", 10000);
    const auto baseline = previous;
    QElapsedTimer observation;
    observation.start();
    int frames = 0;
    do {
        d.frame();
        require(equalLayout(baseline, layout(c)),
                "Lifecycle layout oscillated after its initial stable window");
        ++frames;
    } while (frames < 12 || observation.elapsed() < observationMilliseconds);
    d.log({{"action", "stable_lifecycle_layout"},
           {"width", c.window->width()},
           {"height", c.window->height()},
           {"fullscreen", c.window->windowStates().testFlag(Qt::WindowFullScreen)},
           {"library", c.window->property("libraryMode").toBool()},
           {"observed_polishes", frames},
           {"observation_ms", observation.elapsed()}});
}
struct State {
    QString asset;
    QString source;
    luma::EditDocument document;
    std::vector<std::string> selected;
    std::vector<std::string> members;
    QImage preview;
    bool undo = false;
    bool redo = false;
};
State state(Context &c) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    const auto active = catalog.activeAsset();
    require(active && active->id == c.editor.photoID().toStdString() &&
                active->path == std::filesystem::path(c.editor.sourcePath().toStdString()) &&
                active->edits == c.editor.currentSettings(),
            "Lifecycle initial active document is not durably committed");
    return {c.editor.photoID(),       c.editor.sourcePath(),  c.editor.currentSettings(),
            c.library.selectionIDs(), catalog.libraryIDs({}), fixtureReference(c),
            c.editor.canUndo(),       c.editor.canRedo()};
}
void unchanged(Context &c, const State &expected) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    const auto active = catalog.activeAsset();
    require(
        c.editor.photoID() == expected.asset && c.editor.sourcePath() == expected.source &&
            c.editor.currentSettings() == expected.document &&
            c.library.selectionIDs() == expected.selected &&
            catalog.libraryIDs({}) == expected.members && active &&
            active->id == expected.asset.toStdString() && active->edits == expected.document &&
            c.editor.canUndo() == expected.undo && c.editor.canRedo() == expected.redo &&
            !c.editor.busy() && !c.editor.saving() && c.editor.error().isEmpty() &&
            c.provider->requestImage({}, nullptr, {}) == expected.preview,
        "Window transition changed identity, full edits, history, selection, catalog or pixels");
}
void prepareWindow(Context &c) {
    c.import(100);
    c.driver->doubleClick(c.card(0));
    c.loaded();
    edit(c);
    const auto pixels = fixtureReference(c);
    c.driver->wait([&] { return c.provider->requestImage({}, nullptr, {}) == pixels; },
                   "edited lifecycle preview settled before geometry acceptance");
    c.driver->move("develop.preview", {.5, .5});
}
void toggleFullscreen(Context &c, bool fullscreen) {
    auto &d = *c.driver;
    require(c.window->windowStates().testFlag(Qt::WindowFullScreen) != fullscreen,
            "Fullscreen acceptance must start in the opposite native window state");
    d.key(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    d.wait(
        [&] {
            return c.window->windowStates().testFlag(Qt::WindowFullScreen) == fullscreen &&
                   c.window->visibility() ==
                       (fullscreen ? QWindow::FullScreen : QWindow::Windowed) &&
                   c.window->isVisible() && c.window->isExposed() && c.window->isActive();
        },
        "real fullscreen shortcut acknowledged by native window state", 10000);
}
struct ScrollState {
    Scroll library;
    Scroll adjustments;
    Scroll filmstrip;
};
ScrollState scrollState(Context &c) {
    return {scroll(*c.driver->item("library.grid"), false), scroll(*adjustmentFlickable(c), false),
            scroll(*c.driver->item("develop.filmstrip"), true)};
}
void preservedScroll(Context &c, const ScrollState &expected) {
    const auto actual = scrollState(c);
    auto same = [](const Scroll &first, const Scroll &second) {
        return std::abs(first.offset - second.offset) <= 1 &&
               std::abs(first.content - second.content) <= 1 &&
               std::abs(first.viewport - second.viewport) <= 1;
    };
    c.driver->log({{"action", "windowed_scroll_preservation"},
                   {"library", actual.library.json()},
                   {"adjustments", actual.adjustments.json()},
                   {"filmstrip", actual.filmstrip.json()}});
    require(same(expected.library, actual.library) &&
                same(expected.adjustments, actual.adjustments) &&
                same(expected.filmstrip, actual.filmstrip),
            "Returning to the same window geometry reset a nonzero scroll position");
}
} // namespace

bool appExtraScenario(Context &c, const QString &id) {
    if (id != "UX-APP-012" && id != "UX-APP-015")
        return false;
    require(QGuiApplication::platformName() == "cocoa",
            "Native fullscreen lifecycle qualification requires Cocoa");
    auto &d = *c.driver;
    prepareWindow(c);
    const QSize windowed = id == "UX-APP-012" ? QSize(1000, 640) : QSize(1200, 760);
    d.resize(windowed);
    d.wait([&] { return c.window->size() == windowed; }, "initial windowed lifecycle geometry");
    stable(c, 100);
    d.click("main.libraryButton");
    dragScrollbar(c, "library.scrollBar", *d.item("library.grid"), false, .25);
    d.click("main.developButton");
    dragScrollbar(c, "develop.adjustmentsBar", *adjustmentFlickable(c), false, .25);
    dragScrollbar(c, "develop.filmstripBar", *d.item("develop.filmstrip"), true, .25);
    stable(c, 100);
    const auto expected = state(c);
    const auto offsets = scrollState(c);
    require(offsets.library.offset > offsets.library.origin + 20 &&
                offsets.adjustments.offset > offsets.adjustments.origin + 20 &&
                offsets.filmstrip.offset > offsets.filmstrip.origin + 20,
            "Fullscreen preservation fixture must establish three nonzero scroll positions");
    for (int cycle = 0; cycle < 50; ++cycle) {
        toggleFullscreen(c, true);
        for (const bool library : {true, false}) {
            d.click(library ? "main.libraryButton" : "main.developButton");
            stable(c, 60);
            unchanged(c, expected);
        }
        toggleFullscreen(c, false);
        d.wait([&] { return c.window->size() == windowed; },
               "fullscreen exit restored exact prior window dimensions", 10000);
        for (const bool library : {true, false}) {
            d.click(library ? "main.libraryButton" : "main.developButton");
            stable(c, 60);
            unchanged(c, expected);
        }
        preservedScroll(c, offsets);
        c.checkData();
        d.log({{"action", "fullscreen_round_trip"},
               {"completed_cycle", cycle + 1},
               {"windowed_width", windowed.width()},
               {"windowed_height", windowed.height()},
               {"both_workspace_modes_validated", true}});
    }
    writeJson(c.root + "/measurements.json", {{"scenario", id},
                                              {"fullscreen_round_trips", 50},
                                              {"native_state_transitions", 100},
                                              {"workspace_mode_checks", 200},
                                              {"preserved_nonzero_scroll_regions", 3},
                                              {"input", "production Ctrl+Shift+F shortcut"}});
    return true;
}

bool appMissingSourceRestartScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-APP-004")
        return false;
    auto &d = *c.driver;
    const auto source = QDir(c.stateRoot).absoluteFilePath("missing-source.png");
    if (stage == "prepare") {
        require(QFile::copy(c.fixture + "/Photo-0000.png", source),
                "Cannot prepare isolated removable startup source");
        drop(c, source);
        c.loaded();
        edit(c);
        const auto document = c.editor.currentSettings();
        const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
        require(active && active->id == c.editor.photoID().toStdString() &&
                    active->path == std::filesystem::path(source.toStdString()) &&
                    active->edits == document &&
                    luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 1,
                "Missing-source restart fixture has no exact committed active asset");
        {
            luma::Catalog expected((c.stateRoot + "/expected.sqlite").toStdString());
            const auto record =
                expected.addAndSelect(active->path, active->checksum, active->metadata);
            expected.saveEdits(record.id, document);
            const auto saved = expected.activeAsset();
            require(saved && saved->edits == document,
                    "Independent missing-source oracle lost complete edit fields");
        }
        writeJson(c.stateRoot + "/missing-source-oracle.json",
                  {{"source", source},
                   {"asset", c.editor.photoID()},
                   {"checksum", QString::fromStdString(active->checksum)},
                   {"source_sha256", sha256(source)}});
        quit(c);
        return true;
    }
    require(stage == "verify", "Missing-source scenario has an unknown restart stage");
    const auto oracle =
        QJsonDocument::fromJson(readFile(c.stateRoot + "/missing-source-oracle.json")).object();
    const auto expected =
        luma::Catalog((c.stateRoot + "/expected.sqlite").toStdString()).activeAsset();
    require(expected && oracle["source"].toString() == source && !QFileInfo::exists(source),
            "Verify process must start with the owned active source already absent");
    // This is the same startup boundary used by desktop main.cpp and existing
    // multi-process restore adapters, not an interaction acceptance shortcut.
    c.editor.restore();
    d.wait([&] { return !c.editor.busy() && !c.editor.error().isEmpty(); },
           "missing active source produced a finite startup error");
    d.frame();
    auto *error = d.item("main.editorError");
    require(c.window->isVisible() && c.window->isActive() && !c.editor.hasPhoto() &&
                !c.editor.canUndo() && !c.editor.canRedo() && error->isVisible() &&
                c.editor.previewUrl().isEmpty() && !d.item("develop.preview")->isVisible() &&
                !error->property("text").toString().isEmpty() &&
                c.editor.error().contains("missing-source.png") &&
                d.item("openPhotoButton")->isEnabled(),
            "Missing-source startup hid its error, left busy state or blocked recovery input");
    const auto before = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
    require(before && before->id == oracle["asset"].toString().toStdString() &&
                before->path == expected->path &&
                before->checksum == oracle["checksum"].toString().toStdString() &&
                before->edits == expected->edits &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 1,
            "Failed startup restore deleted or mutated the complete persisted missing asset");
    const auto polishes = c.metrics.polishes();
    d.click("main.developButton");
    d.click("main.libraryButton");
    require(c.window->property("libraryMode").toBool() && c.metrics.polishes() > polishes &&
                !c.editor.busy(),
            "Error-state workspace navigation did not remain responsive");
    d.screenshot(c.root + "/missing-source-startup-error.png");
    const auto replacement = c.fixture + "/Photo-0001.png";
    drop(c, replacement);
    c.loaded();
    const auto replacementRecord = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
    require(replacementRecord && c.editor.photoID() != oracle["asset"].toString() &&
                c.editor.sourcePath() == replacement &&
                replacementRecord->edits == luma::EditDocument{} &&
                replacementRecord->id == c.editor.photoID().toStdString() && !c.editor.canUndo() &&
                !c.editor.canRedo() && c.editor.error().isEmpty(),
            "Recovery input failed or leaked missing-photo edits into a replacement photo");
    const auto preserved = luma::Catalog(c.catalogFile.toStdString()).assetByID(before->id);
    require(preserved && preserved->path == before->path &&
                preserved->checksum == before->checksum && preserved->edits == expected->edits &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 2,
            "Opening a recovery photo damaged the missing asset or its saved full document");
    const auto pixels = fixtureReference(c);
    d.wait(
        [&] {
            auto *preview = d.item("develop.preview");
            return preview->isVisible() && preview->property("status").toInt() == 1 &&
                   preview->property("source").toUrl() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == pixels;
        },
        "missing-source recovery photo displays its own exact pixels");
    quit(c);
    return true;
}

void prepareMissingStartupSource(const QString &root, const QString &stateRoot) {
    // Invoke in the NEW verify process before Context/LibraryModel/ThumbnailProvider.
    // The runner has already reaped the prepare process. Never delete a shared fixture.
    require(!stateRoot.isEmpty(), "Missing-source fault requires isolated restart state");
    const auto oracle =
        QJsonDocument::fromJson(readFile(stateRoot + "/missing-source-oracle.json")).object();
    const auto source = QDir(stateRoot).absoluteFilePath("missing-source.png");
    const QFileInfo info(source);
    require(oracle["source"].toString() == source && info.isFile() && !info.isSymLink() &&
                info.canonicalFilePath() == source &&
                sha256(source) == oracle["source_sha256"].toString(),
            "Unsafe or changed owned missing-source startup fixture");
    const auto active = luma::Catalog((stateRoot + "/catalog.sqlite").toStdString()).activeAsset();
    require(active && active->id == oracle["asset"].toString().toStdString() &&
                active->path == std::filesystem::path(source.toStdString()),
            "Missing-source fault no longer targets the committed last active asset");
    require(QFile::remove(source) && !QFileInfo::exists(source),
            "Cannot remove isolated active source before the new process Context exists");
    writeJson(root + "/missing-source-before-context.json",
              {{"source", source},
               {"asset", oracle["asset"]},
               {"prior_sha256", oracle["source_sha256"]},
               {"before_context", true},
               {"owned_source_removed", true}});
}
} // namespace ux
