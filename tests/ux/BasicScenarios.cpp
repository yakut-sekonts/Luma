#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include "tests/ux/UxTestIds.h"
#include <QQmlComponent>
#include <QSignalSpy>
#include <QTest>
#include <QtTest/qtestwheel.h>
#include <cmath>
namespace ux {
bool selectionScenario(Context &, const QString &);
bool libraryScaleScenario(Context &, const QString &);
bool accessibilityScenario(Context &, const QString &);
bool cropScenario(Context &, const QString &);
bool keyboardScenario(Context &, const QString &);
bool navigationScenario(Context &, const QString &);
bool transformExtraScenario(Context &, const QString &);
bool filterExtraScenario(Context &, const QString &);
bool presetExtraScenario(Context &, const QString &);
bool presetRemainingScenario(Context &, const QString &);
bool historyStressScenario(Context &, const QString &);
bool sliderExtraScenario(Context &, const QString &);
bool previewExtraScenario(Context &, const QString &);
bool recoveryExtraScenario(Context &, const QString &);
bool importExtraScenario(Context &, const QString &);
bool currentPerformanceScenario(Context &, const QString &);
bool whiteBalanceScenario(Context &, const QString &);
bool appExtraScenario(Context &, const QString &);
bool exportExtraScenario(Context &, const QString &);
bool gestureRemainingScenario(Context &, const QString &);
bool visualRemainingScenario(Context &, const QString &);
bool libraryRemainingScenario(Context &, const QString &);
void basicScenario(Context &c, const QString &id) {
    auto &d = *c.driver;
    if (id == "UX-EXP-011") {
        require(visualRemainingScenario(c, "UX-VIS-004"),
                "The complete export-failure readability scenario is unavailable");
        return;
    }
    if (libraryRemainingScenario(c, id) || visualRemainingScenario(c, id) ||
        gestureRemainingScenario(c, id) || whiteBalanceScenario(c, id) || appExtraScenario(c, id) ||
        exportExtraScenario(c, id) || importExtraScenario(c, id) ||
        currentPerformanceScenario(c, id) || presetRemainingScenario(c, id) ||
        historyStressScenario(c, id) || sliderExtraScenario(c, id) || previewExtraScenario(c, id) ||
        recoveryExtraScenario(c, id) || presetExtraScenario(c, id) || navigationScenario(c, id) ||
        transformExtraScenario(c, id) || filterExtraScenario(c, id) || keyboardScenario(c, id) ||
        selectionScenario(c, id) || libraryScaleScenario(c, id) || accessibilityScenario(c, id) ||
        localizationScenario(c, id) || advancedScenario(c, id) || presetScenario(c, id) ||
        asyncScenario(c, id))
        return;
    if (lensScenario(c, id))
        return;
    if (detailScenario(c, id))
        return;
    if (cropScenario(c, id) || transformScenario(c, id) || geometryScenario(c, id))
        return;
    if (batchScenario(c, id))
        return;
    if (exportScenario(c, id))
        return;
    if (recoveryScenario(c, id))
        return;
    if (importScenario(c, id))
        return;
    if (lifecycleScenario(c, id))
        return;
    if (maskScenario(c, id))
        return;
    if (libraryScenario(c, id))
        return;
    if (id == "UX-APP-002" || id == "UX-NAV-003" || id == "UX-APP-013") {
        require(!c.editor.hasPhoto() && !c.editor.busy() && c.library.count() == 0,
                "Empty state is inconsistent");
        d.click(ids::develop);
        d.resize({1000, 640});
        for (const auto *name :
             {"main.libraryButton", "main.developButton", "settingsButton", "openPhotoButton"}) {
            auto *item = d.item(name);
            const auto p = item->mapToScene(QPointF(item->width(), item->height()));
            require(item->isVisible() && p.x() <= c.window->width() && p.y() <= c.window->height(),
                    "Critical control escaped minimum window");
        }
        require(!d.item("exportButton")->isEnabled(), "Empty export is enabled");
    } else if (id == "UX-NAV-001") {
        c.openFirst();
        const auto before = c.editor.currentSettings();
        for (int i = 0; i < 100; ++i) {
            d.click(ids::library);
            require(c.window->property("libraryMode").toBool(), "Library click lost");
            d.click(ids::develop);
            require(!c.window->property("libraryMode").toBool(), "Develop click lost");
        }
        require(c.editor.currentSettings() == before, "Navigation mutated edits");
    } else if (id.startsWith("UX-SEL-00")) {
        c.import(10);
        const auto first = c.card(0), second = c.card(1), fourth = c.card(3);
        d.click(first);
        require(c.library.selectedCount() == 1 && !c.editor.hasPhoto() &&
                    c.window->property("libraryMode").toBool(),
                "Single click opened photo or failed selection");
        if (id == "UX-SEL-002" || id == "UX-SEL-003") {
            QSignalSpy requests(&c.library, &LibraryModel::openRequested);
            d.doubleClick(first);
            c.loaded();
            require(requests.count() == 1 && c.library.selectedCount() == 1,
                    "Double click duplicated activation or selection");
        } else if (id == "UX-SEL-004") {
            d.click(second, Qt::ControlModifier);
            require(c.library.selectedCount() == 2, "Cmd-click did not extend selection");
            d.click(first, Qt::ControlModifier);
            require(c.library.selectedCount() == 1 &&
                        c.library.data(c.library.index(1), LibraryModel::Selected).toBool(),
                    "Cmd-click did not toggle expected asset");
        } else if (id == "UX-SEL-005") {
            d.click(fourth, Qt::ShiftModifier);
            d.wait([&] { return !c.library.selecting(); }, "range selection completed");
            require(c.library.selectedCount() == 4, "Shift-click selected incorrect range");
        } else
            require(id == "UX-SEL-001", "Scenario implementation missing");
    } else if (id == "UX-SLD-001") {
        c.openFirst();
        c.reveal(ids::exposure);
        d.drag(ids::exposure, d.sliderThumb(ids::exposure), {0, .5});
        require(c.editor.currentSettings().exposure == -5,
                "Exposure drag did not reach exact minimum");
        d.drag(ids::exposure, d.sliderThumb(ids::exposure), {1, .5});
        require(c.editor.currentSettings().exposure == 5,
                "Exposure drag did not reach exact maximum");
        auto *slider = d.object(ids::exposure);
        const auto start = d.sliderThumb(ids::exposure);
        d.press(ids::exposure, start);
        auto *grabber = c.window->mouseGrabberItem();
        auto *viewport = d.item(ids::adjustments);
        d.log({{"action", "maximum_thumb_press"},
               {"pressed", slider->property("pressed").toBool()},
               {"grabber",
                grabber ? QString::fromUtf8(grabber->metaObject()->className()) : QString{}},
               {"grabber_id", grabber ? grabber->objectName() : QString{}},
               {"viewport_width", viewport->width()},
               {"slider_right_in_viewport",
                d.item(ids::exposure)
                    ->mapToItem(viewport, QPointF(d.item(ids::exposure)->width(), 0))
                    .x()}});
        for (int i = 1; i <= 12; ++i)
            d.move(ids::exposure,
                   start + (QPointF(.5, .5) - start) * (static_cast<double>(i) / 12));
        d.release(ids::exposure, {.5, .5});
        auto *handle = qvariant_cast<QObject *>(slider->property("handle"));
        require(handle, "Exposure slider has no handle");
        const double travel =
            slider->property("availableWidth").toDouble() - handle->property("width").toDouble();
        d.log({{"action", "slider_zero_geometry"},
               {"value", c.editor.currentSettings().exposure},
               {"position", slider->property("position").toDouble()},
               {"width", slider->property("width").toDouble()},
               {"available_width", slider->property("availableWidth").toDouble()},
               {"travel", travel},
               {"handle_x", handle->property("x").toDouble()},
               {"handle_width", handle->property("width").toDouble()},
               {"scene_right",
                d.item(ids::exposure)->mapToScene(QPointF(d.item(ids::exposure)->width(), 0)).x()},
               {"window_width", c.window->width()}});
        require(travel > 0 && std::abs(c.editor.currentSettings().exposure) <= 5 / travel + .01,
                "Exposure thumb missed the zero position beyond one input-pixel tolerance");
        d.doubleClick(ids::exposure);
        require(c.editor.currentSettings().exposure == 0,
                "Default reset failed to restore exact zero");
    } else if (id.startsWith("UX-SLD-00")) {
        c.openFirst();
        c.reveal(ids::exposure);
        const auto before = c.editor.currentSettings();
        const auto beforeImage = c.provider->requestImage({}, nullptr, {});
        d.drag(ids::exposure, {.5, .5}, {.7, .5});
        const auto edited = c.editor.currentSettings();
        require(edited.exposure > 1 && edited.exposure < 3 && edited != before,
                "Exposure thumb did not follow real pointer");
        if (id == "UX-SLD-004")
            d.wait([&] { return c.provider->requestImage({}, nullptr, {}) != beforeImage; },
                   "edited exposure pixels visible");
        d.click(ids::undo);
        require(c.editor.currentSettings() == before, "Continuous slider drag was not one Undo");
        if (id == "UX-SLD-004") {
            d.wait([&] { return c.provider->requestImage({}, nullptr, {}) == beforeImage; },
                   "Undo restored exact preview pixels");
            d.frame();
        }
        d.click(ids::redo);
        require(c.editor.currentSettings() == edited, "Redo lost slider state");
        if (id == "UX-SLD-005" || id == "UX-SLD-006") {
            d.doubleClick(ids::exposure);
            require(c.editor.currentSettings().exposure == 0,
                    "Double-click did not reset exposure");
            d.click(ids::undo);
            require(c.editor.currentSettings() == edited,
                    "Double-click reset polluted gesture history");
        } else
            require(id == "UX-SLD-001" || id == "UX-SLD-003" || id == "UX-SLD-004",
                    "Scenario implementation missing");
    } else if (id == "UX-DSC-003") {
        c.openFirst();
        c.reveal(ids::exposure);
        const auto before = c.editor.currentSettings();
        auto *flickable =
            qvariant_cast<QObject *>(d.object(ids::adjustments)->property("contentItem"));
        const double y = flickable->property("contentY").toDouble();
        d.log({{"action", "slider_scroll_probe"},
               {"wheel_enabled", d.item(ids::exposure)->property("wheelEnabled").toBool()},
               {"content_y", y}});
        d.wheel(ids::exposure, -120);
        d.frame();
        require(c.editor.currentSettings() == before,
                "Page scroll over slider accidentally changed a value");
        require(flickable->property("contentY").toDouble() > y,
                "Page scroll was swallowed over slider");
    } else if (id == "UX-PRV-001" || id == "UX-PRV-003" || id == "UX-KEY-006") {
        c.openFirst();
        const auto initial = c.editor.currentSettings();
        const auto originalImage = fixtureReference(c);
        c.reveal(ids::exposure);
        d.drag(ids::exposure, {.5, .5}, {.65, .5});
        d.wait([&] { return c.provider->requestImage({}, nullptr, {}) != originalImage; },
               "distinct processed preview");
        const auto captured = c.provider->requestImage({}, nullptr, {});
        const auto processedImage = fixtureReference(c);
        d.log({{"action", "processed_fixture_capture"},
               {"matches_final_document", captured == processedImage},
               {"exposure", c.editor.currentSettings().exposure}});
        if (captured != processedImage) {
            require(captured.save(c.root + "/captured-intermediate.png") &&
                        processedImage.save(c.root + "/expected-final.png"),
                    "Cannot retain intermediate-frame oracle evidence");
        }
        const auto before = c.editor.currentSettings();
        auto visible = [&](const QImage &image) {
            auto *preview = d.item("develop.preview");
            return preview->isVisible() && preview->property("status").toInt() == 1 &&
                   preview->property("source").toUrl().toString() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == image;
        };
        d.wait([&] { return visible(processedImage); },
               "final processed fixture ready before comparison toggles");
        for (int i = 0; i < 100; ++i) {
            if (id == "UX-KEY-006")
                d.key(Qt::Key_Backslash);
            else
                d.click("develop.originalButton");
            require(c.editor.original(), "Original click lost");
            d.wait([&] { return visible(originalImage); }, "Original displayed its own pixels");
            if (id == "UX-KEY-006")
                d.key(Qt::Key_Backslash);
            else
                d.click("develop.processedButton");
            require(!c.editor.original(), "Develop click lost");
            d.wait([&] { return visible(processedImage); }, "Processed displayed its own pixels");
        }
        require(c.editor.currentSettings() == before, "Original mutated edits");
        d.click(ids::undo);
        require(c.editor.currentSettings() == initial && !c.editor.canUndo(),
                "Comparison toggles polluted history");
    } else if (id == "UX-DSC-001") {
        QQmlComponent probe(&c.engine);
        probe.setData("import QtQuick; import QtQuick.Controls; "
                      "ApplicationWindow{width:400;height:400;visible:true;ScrollView{objectName: "
                      "'probe';anchors.fill:parent;contentWidth:availableWidth;contentHeight:3000;"
                      "Label{width:200;"
                      "height:3000;text:'Wheel probe'}}}",
                      QUrl());
        d.wait([&] { return probe.status() != QQmlComponent::Loading; },
               "wheel probe component ready");
        require(!probe.isError(), probe.errorString());
        const std::unique_ptr<QObject> probeRoot(probe.create());
        auto *probeWindow = qobject_cast<QQuickWindow *>(probeRoot.get());
        require(probeWindow, "Wheel positive control failed to load");
        auto *probeScroll = probeWindow->findChild<QQuickItem *>("probe");
        auto *probeFlickable = qvariant_cast<QObject *>(probeScroll->property("contentItem"));
        d.wait([&] { return probeWindow->isExposed(); }, "wheel probe exposed");
        d.wait([&] { return probeFlickable->property("contentHeight").toDouble() > 400; },
               "positive-control layout");
        d.log({{"action", "wheel_control_geometry"},
               {"content_height", probeFlickable->property("contentHeight").toDouble()},
               {"height", probeFlickable->property("height").toDouble()},
               {"interactive", probeFlickable->property("interactive").toBool()}});
        probeWindow->requestActivate();
        QTest::mouseMove(probeWindow, {200, 200});
        Driver probeDriver(*probeWindow, c.metrics, c.root + "/actions.jsonl");
        probeDriver.wheel("probe", -120);
        d.wait([&] { return probeFlickable->property("contentY").toDouble() > 0; },
               "positive-control ScrollView accepts wheel", 3000);
        d.log({{"action", "wheel_positive_control"},
               {"content_y", probeFlickable->property("contentY").toDouble()}});
        probeWindow->close();
        c.window->requestActivate();
        d.wait([&] { return c.window->isActive(); }, "main window reactivated");
        c.openFirst();
        c.reveal("develop.resetButton");
        require(d.item("develop.resetButton")->isEnabled(), "Bottom adjustment panel unreachable");
        c.reveal("localPanelToggle");
        require(d.item("localPanelToggle")->isVisible(), "Top adjustment panel unreachable");
    } else {
        throw std::runtime_error("Scenario has no implementation: " + id.toStdString());
    }
}
} // namespace ux
