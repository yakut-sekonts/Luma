#include "tests/ux/QueuedCallGate.h"
#include "tests/ux/UxContext.h"
#include <QTest>

namespace ux {
bool asyncScenario(Context &c, const QString &id) {
    if (id != "UX-RACE-001" && id != "UX-RACE-004")
        return false;
    auto &d = *c.driver;
    c.openFirst();
    c.saved();
    const auto source = c.editor.sourcePath();
    const auto original = c.provider->requestImage({}, nullptr, {});
    const auto originalURL = c.editor.previewUrl();
    const auto next = c.card(1, true);
    const auto nextPath =
        c.library.data(c.library.index(1), LibraryModel::Path).toUrl().toLocalFile();
    QImage expected;
    QueuedCallGate gate(
        c.editor,
        [&] { return c.editor.presetPreview() && !c.editor.busy() && !c.editor.saving(); },
        [&] {
            d.log({{"action", "old_preview_callback_held"}, {"source", source}});
            if (id == "UX-RACE-001") {
                // Deliver a real click before the already queued old preview callback.
                d.log({{"action", "click"},
                       {"id", next},
                       {"scheduler_boundary", "old_meta_call_held"}});
                QTest::mouseClick(c.window, Qt::LeftButton, Qt::NoModifier, d.point(next));
                d.wait([&] { return c.editor.sourcePath() == nextPath && !c.editor.busy(); },
                       "new photo before old callback");
                c.loaded();
                expected = c.provider->requestImage({}, nullptr, {});
                require(expected != original, "Race fixture has indistinguishable photos");
            } else {
                d.move("develop.preview", {.5, .5});
                d.wait(
                    [&] {
                        return !c.editor.presetPreview() && c.editor.previewUrl() != originalURL &&
                               c.provider->requestImage({}, nullptr, {}) == original;
                    },
                    "new normal render before old hover callback");
                expected = original;
            }
            d.log({{"action", "new_state_ready_before_old_callback"},
                   {"source", c.editor.sourcePath()}});
        });
    d.move("filter_soft-portrait", {.5, .5});
    d.wait([&] { return gate.caught; }, "queued preview scheduler boundary reached");
    if (gate.failure)
        std::rethrow_exception(gate.failure);
    d.frame();
    require(c.provider->requestImage({}, nullptr, {}) == expected && !c.editor.presetPreview(),
            "Old queued preview callback replaced the current visible state");
    if (id == "UX-RACE-001")
        require(c.editor.sourcePath() == nextPath && !c.editor.canUndo(),
                "Late old callback crossed asset history/metadata");
    return true;
}
} // namespace ux
