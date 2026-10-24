#include "platform/macos/FileHasher.h"
#include "tests/ux/RetouchResultProbe.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QSet>
#include <cmath>
namespace ux {
bool retouchScenario(Context &c, const QString &id) {
    static const QSet<QString> ids{"UX-P07-REM-001", "UX-P07-REM-002", "UX-P07-REM-003"};
    if (!ids.contains(id))
        return false;
    auto &d = *c.driver;
    c.import(1);
    d.doubleClick(c.card(0));
    c.loaded();
    c.saved();
    const auto originalHash = luma::macos::sha256File(c.editor.sourcePath().toStdString());
    const auto before = c.editor.currentSettings();
    const auto beforePixels = fixtureReference(c);
    c.reveal("retouch.toggle");
    d.click("retouch.toggle");
    c.reveal("retouch.mode.remove");
    d.click("retouch.mode.remove");
    require(c.editor.retouchMode() == "remove", "Actual Remove control did not activate");
    RetouchResultGate gate(c.editor);
    d.drag("retouch.gesture", {.42, .40}, {.49, .46});
    d.wait([&] { return gate.snapshot().held; }, "Actual donor search result reached dispatch",
           60000);
    const auto computed = gate.snapshot();
    require(c.editor.retouchBusy() && computed.stroke.mode == luma::RetouchMode::remove &&
                computed.stroke.points.size() > 1 && std::isfinite(computed.stroke.sourceOffsetX) &&
                std::isfinite(computed.stroke.sourceOffsetY) &&
                std::hypot(computed.stroke.sourceOffsetX, computed.stroke.sourceOffsetY) > .01 &&
                c.editor.currentSettings() == before,
            "Real pending removal changed committed state or returned an invalid donor");
    c.reveal("retouch.cancel");
    require(d.item("retouch.progress")->isVisible() && d.item("retouch.cancel")->isVisible() &&
                d.item("retouch.cancel")->isEnabled(),
            "Actual Remove progress/Cancel is unavailable during generation");
    d.screenshot(c.root + "/remove-pending.png");
    d.log({{"action", "computed_removal_held"},
           {"revision", static_cast<qint64>(computed.revision)},
           {"points", static_cast<qint64>(computed.stroke.points.size())},
           {"offset_x", computed.stroke.sourceOffsetX},
           {"offset_y", computed.stroke.sourceOffsetY}});
    if (id == "UX-P07-REM-002") {
        d.click("retouch.cancel");
        require(!c.editor.retouchBusy() && !c.editor.retouchState().value("drawing").toBool(),
                "Remove Cancel did not clear active operation");
    }
    gate.release();
    d.wait([&] { return gate.snapshot().deliveries == 1; }, "Real held removal callback delivered");
    c.saved();
    if (id == "UX-P07-REM-002") {
        require(gate.snapshot().rejected && c.editor.currentSettings() == before,
                "Cancelled late removal changed committed edits");
        d.wait(
            [&] {
                return previewPixelsAgree(c.provider->requestImage({}, nullptr, {}), beforePixels,
                                          c.editor.metalPreview());
            },
            "Cancelled removal retained original committed pixels");
    } else {
        const auto after = c.editor.currentSettings();
        require(!gate.snapshot().rejected && !c.editor.retouchBusy() && after.retouch.size() == 1 &&
                    after.retouch.front() == computed.stroke,
                "Actual computed removal was not applied exactly once");
        const auto expected = fixtureReference(c);
        require(expected != beforePixels && beforePixels.pixelColor(117, 83).red() > 200 &&
                    expected.pixelColor(117, 83).red() < 100,
                "Actual Remove did not repair the independently known central defect");
        d.wait(
            [&] {
                return previewPixelsAgree(c.provider->requestImage({}, nullptr, {}), expected,
                                          c.editor.metalPreview());
            },
            "Removal preview agrees with committed independent CPU pixels");
        d.click("main.undoButton");
        c.saved();
        require(c.editor.currentSettings() == before && c.editor.canRedo(),
                "Committed removal is not one exact Undo unit");
        d.click("main.redoButton");
        c.saved();
        require(c.editor.currentSettings() == after, "Remove Redo lost actual donor/stroke data");
        d.screenshot(c.root + "/remove-committed.png");
    }
    require(!gate.snapshot().hookFailed, "Retouch observation failed internally");
    require(c.editor.error().isEmpty(), c.editor.error());
    require(luma::macos::sha256File(c.editor.sourcePath().toStdString()) == originalHash,
            "Remove workflow changed the original file");
    return true;
}
} // namespace ux
