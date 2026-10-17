#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QEvent>
#include <QGuiApplication>
#include <QSet>

namespace ux {
namespace {
const QStringList detailControls{
    "sharpen.amount",    "sharpen.radius",        "sharpen.detail",          "sharpen.masking",
    "noise.luminance",   "noise.luminanceDetail", "noise.luminanceContrast", "noise.color",
    "noise.colorDetail", "noise.colorSmoothness"};
void visiblePixels(Context &c, const QImage &expected, const QString &reason) {
    c.driver->wait(
        [&] {
            auto *preview = c.driver->item("develop.preview");
            return preview->isVisible() && preview->property("status").toInt() == 1 &&
                   preview->property("source").toUrl().toString() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == expected;
        },
        reason);
    c.driver->frame();
}
void undoRedo(Context &c, const luma::EditDocument &before, const luma::EditDocument &after) {
    c.driver->click("main.undoButton");
    require(c.editor.currentSettings() == before, "Detail gesture was not one Undo");
    c.driver->click("main.redoButton");
    require(c.editor.currentSettings() == after, "Detail Redo lost parameters");
}
} // namespace
void editDetailControls(Context &c, bool includeBasic) {
    auto &d = *c.driver;
    QStringList controls;
    if (includeBasic)
        controls = {"temperature", "tint",   "exposure", "contrast", "highlights",
                    "shadows",     "whites", "blacks",   "vibrance", "saturation"};
    controls.append(detailControls);
    for (const auto &parameter : controls) {
        const auto selector =
            (parameter.contains('.') ? QString{} : QString("develop.")) + parameter + "Slider";
        c.reveal(selector);
        const auto before = c.editor.currentSettings();
        d.drag(selector, d.sliderThumb(selector), {.65, .5});
        require(c.editor.currentSettings() != before &&
                    c.editor.currentSettings().parameter(parameter.toStdString()) !=
                        before.parameter(parameter.toStdString()),
                "Detail/Basic control did not change its parameter: " + parameter);
    }
}
bool detailScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-DTL-001", "UX-DTL-002", "UX-DTL-003", "UX-DTL-004",
                                         "UX-DTL-005", "UX-DTL-006", "UX-DTL-008", "UX-DTL-009"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    c.openFirst();
    if (id == "UX-DTL-001") {
        for (int i = 0; i < 4; ++i) {
            const auto &parameter = detailControls[i];
            const auto selector = parameter + "Slider";
            c.reveal(selector);
            const auto before = c.editor.currentSettings();
            d.drag(selector, d.sliderThumb(selector), {.65, .5});
            const auto after = c.editor.currentSettings();
            auto expected = before;
            expected.setParameter(parameter.toStdString(),
                                  after.parameter(parameter.toStdString()));
            require(after != before && after == expected,
                    "Sharpening drag changed the wrong settings");
            undoRedo(c, before, after);
        }
        return true;
    }
    if (id == "UX-DTL-008" || id == "UX-DTL-009") {
        const bool color = id == "UX-DTL-009";
        const QString slider = color ? "noise.colorSlider" : "noise.luminanceSlider";
        const QString toggle = color ? "colorNoiseEnabled" : "luminanceNoiseEnabled";
        const auto correction =
            color ? luma::Correction::colorNoise : luma::Correction::luminanceNoise;
        c.reveal(slider);
        d.drag(slider, d.sliderThumb(slider), {.4, .5});
        const auto before = c.editor.currentSettings();
        c.reveal(toggle);
        d.click(toggle);
        auto expected = before;
        expected.setEnabled(correction, false);
        require(c.editor.currentSettings() == expected && !d.item(slider)->isEnabled(),
                "Noise disable discarded settings or left control enabled");
        d.click(toggle);
        require(c.editor.currentSettings() == before && d.item(slider)->isEnabled(),
                "Noise re-enable lost retained value");
        return true;
    }
    const QString slider = "sharpen.amountSlider";
    c.reveal(slider);
    const auto before = c.editor.currentSettings();
    d.keyDown(Qt::Key_Alt);
    d.press(slider, d.sliderThumb(slider));
    d.move(slider, {.65, .5});
    require(!c.editor.detailDiagnostic().isEmpty(), "Option drag did not enter diagnostic view");
    const auto normal = fixtureReference(c, luma::DetailDiagnostic::none);
    const auto diagnostic = fixtureReference(c, luma::DetailDiagnostic::amount);
    require(normal != diagnostic, "Diagnostic fixture has no distinct visual effect");
    visiblePixels(c, diagnostic, "Option diagnostic pixels displayed");
    if (id == "UX-DTL-004") {
        QEvent event(QEvent::ApplicationDeactivate);
        d.log({{"action", "application_deactivate_event"}});
        QCoreApplication::sendEvent(QGuiApplication::instance(), &event);
        require(c.editor.detailDiagnostic().isEmpty(), "Deactivation retained Option diagnostic");
        visiblePixels(c, normal, "Deactivation restored normal pixels");
    }
    if (id == "UX-DTL-002" || id == "UX-DTL-003") {
        d.keyUp(Qt::Key_Alt);
        require(c.editor.detailDiagnostic().isEmpty(),
                "Option release retained diagnostic while dragging");
        visiblePixels(c, normal, "Option release restored normal preview pixels");
    }
    d.release(slider, {.65, .5});
    if (id == "UX-DTL-005") {
        const auto source = c.editor.sourcePath();
        d.click(c.card(1, true));
        d.wait([&] { return c.editor.sourcePath() != source && !c.editor.busy(); },
               "diagnostic photo switch");
        c.loaded();
        require(c.editor.detailDiagnostic().isEmpty(),
                "Photo switch retained another asset's diagnostic");
        d.keyUp(Qt::Key_Alt);
        return true;
    }
    if (id == "UX-DTL-006") {
        const auto diagnosticExport = c.exportPNG("diagnostic-active");
        d.keyUp(Qt::Key_Alt);
        require(c.editor.detailDiagnostic().isEmpty(),
                "Diagnostic did not clear before normal export");
        const auto normalExport = c.exportPNG("diagnostic-released");
        require(QImage(diagnosticExport) == QImage(normalExport),
                "Transient diagnostic leaked into export pixels");
        return true;
    }
    d.keyUp(Qt::Key_Alt);
    require(c.editor.detailDiagnostic().isEmpty(), "Released diagnostic is stale");
    undoRedo(c, before, c.editor.currentSettings());
    return true;
}
} // namespace ux
