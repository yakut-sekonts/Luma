#include "tests/ux/UxContext.h"
#include <QSet>
#include <algorithm>

namespace ux {
void openKnownLens(Context &c) {
    auto &d = *c.driver;
    const QList<QUrl> sources{QUrl::fromLocalFile(c.fixture + "/stress/Lens.DNG")};
    d.object("library.pane")->setProperty("sources", QVariant::fromValue(sources));
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "lens fixture import opened");
    d.click("library.importStart");
    d.wait([&] { return !c.library.working() && !c.library.loading() && c.library.count() == 1; },
           "known-lens DNG imported");
    require(c.library.error().isEmpty(), c.library.error());
    d.doubleClick(c.card(0));
    c.loaded();
    require(c.editor.lensState()["resolvedProfile"].toString().contains("18-140"),
            "Known lens metadata did not resolve its calibrated profile");
}
void selectKnownLensProfile(Context &c) {
    const auto options = c.editor.lensOptions();
    int selected = -1;
    for (int i = 1; i < options.size(); ++i)
        if (options[i].toMap()["name"].toString().contains("18-140")) {
            selected = i;
            break;
        }
    require(selected > 0, "Known calibrated profile missing from manual selection");
    c.reveal("lensSearch");
    c.driver->type("lensSearch", "18-140");
    c.reveal("lensProfile");
    c.driver->choose("lensProfile", 1);
    c.driver->log(
        {{"action", "lens_choice_probe"},
         {"expected_index", selected},
         {"current_index", c.driver->object("lensProfile")->property("currentIndex").toInt()},
         {"qml_count", c.driver->object("lensProfile")->property("count").toInt()},
         {"model_count", options.size()},
         {"text", c.driver->object("lensProfile")->property("currentText").toString()},
         {"expected_id", options[selected].toMap()["id"].toString()},
         {"actual_id", QString::fromStdString(c.editor.currentSettings().lens.profile)},
         {"error", c.editor.error()}});
    require(c.editor.currentSettings().lens.profile ==
                options[selected].toMap()["id"].toString().toStdString(),
            "Manual lens selection did not apply chosen identity");
    c.reveal("lensSearch");
    c.driver->type("lensSearch", "");
    c.reveal("lensEnabled");
    if (!c.editor.currentSettings().enabled(luma::Correction::lens))
        c.driver->click("lensEnabled");
}
bool lensScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-LNS-001", "UX-LNS-002", "UX-LNS-003", "UX-LNS-005",
                                         "UX-LNS-006", "UX-LNS-007", "UX-LNS-008"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    if (id == "UX-LNS-002") {
        c.openFirst();
        c.reveal("lensProfile");
        require(c.editor.lensOptions().size() == 1 &&
                    c.editor.currentSettings().lens.profile.empty() &&
                    c.editor.lensState()["resolvedProfile"].toString() == QString::fromUtf8("—"),
                "Unknown camera/lens silently selected an arbitrary calibration");
        require(!d.object("lensStatus")->property("text").toString().isEmpty(),
                "Unknown lens has no readable status");
        return true;
    }
    openKnownLens(c);
    c.reveal("lensProfile");
    require(c.editor.currentSettings().lens.profile.empty() &&
                !c.editor.currentSettings().enabled(luma::Correction::lens),
            "Auto profile changed default opt-in correction policy");
    if (id == "UX-LNS-003") {
        const auto count = d.object("lensProfile")->property("count").toInt();
        c.reveal("lensSearch");
        d.type("lensSearch", "18-140");
        require(d.object("lensSearch")->property("activeFocus").toBool() &&
                    d.object("lensProfile")->property("count").toInt() >= 2 &&
                    d.object("lensProfile")->property("count").toInt() < count,
                "Lens profile search did not filter responsively");
        return true;
    }
    selectKnownLensProfile(c);
    if (id == "UX-LNS-001") {
        require(c.editor.lensState()["distortionAvailable"].toBool() &&
                    c.editor.lensState()["vignettingAvailable"].toBool() &&
                    c.editor.lensState()["caAvailable"].toBool(),
                "Known profile lost supported calibrations");
        c.saved();
        return true;
    }
    if (id == "UX-LNS-005") {
        const auto options = c.editor.lensOptions();
        const int count = std::min(8, static_cast<int>(options.size()));
        for (int i = 1; i < count; ++i) {
            c.reveal("lensProfile");
            d.choose("lensProfile", i);
            require(c.editor.currentSettings().lens.profile ==
                        options[i].toMap()["id"].toString().toStdString(),
                    "Rapid lens selection retained a previous profile");
        }
        return true;
    }
    if (id == "UX-LNS-006") {
        for (const auto *selector :
             {"develop.lens.distortionSlider", "develop.lens.vignettingSlider"}) {
            c.reveal(selector);
            const auto before = c.editor.currentSettings();
            d.drag(selector, d.sliderThumb(selector), {.65, .5});
            const auto after = c.editor.currentSettings();
            require(after != before, "Calibrated lens slider did not move");
            d.click("main.undoButton");
            require(c.editor.currentSettings() == before, "Lens amount gesture was not one Undo");
            d.click("main.redoButton");
            require(c.editor.currentSettings() == after,
                    "Lens amount Redo lost calibration settings");
        }
        return true;
    }
    const auto before = c.editor.currentSettings();
    if (id == "UX-LNS-007") {
        c.reveal("lensCA");
        d.click("lensCA");
        auto expected = before;
        expected.lens.chromaticAberration = !before.lens.chromaticAberration;
        require(c.editor.currentSettings() == expected, "CA toggle changed other settings");
    } else {
        c.reveal("lensEnabled");
        d.click("lensEnabled");
        auto expected = before;
        expected.setEnabled(luma::Correction::lens, false);
        require(c.editor.currentSettings() == expected,
                "Disabling Optics discarded calibration values");
        d.click("lensEnabled");
        require(c.editor.currentSettings() == before, "Re-enabling Optics lost retained values");
    }
    return true;
}
} // namespace ux
