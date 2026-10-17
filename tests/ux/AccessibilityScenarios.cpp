#include "tests/ux/UxContext.h"
#include <QAccessible>
#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace ux {
namespace {
bool readable(const QString &name) {
    for (const auto character : name)
        if (character.isLetter())
            return true;
    return false;
}
QAccessibleInterface *interfaceFor(QObject *object) {
    auto *interface = QAccessible::queryAccessibleInterface(object);
    require(interface && interface->isValid(),
            "Control has no valid accessible interface: " + object->objectName());
    return interface;
}
void iconNames(Context &c, QStringList &failures) {
    QJsonArray entries;
    std::function<void(QQuickItem *)> visit = [&](QQuickItem *parent) {
        for (auto *item : parent->childItems()) {
            if (item->isVisible() && item->inherits("QQuickAbstractButton")) {
                const auto text = item->property("text").toString().trimmed();
                if (!readable(text)) {
                    auto *interface = interfaceFor(item);
                    const auto name = interface->text(QAccessible::Name).trimmed();
                    entries.append(QJsonObject{
                        {"id", item->objectName()}, {"text", text}, {"accessible_name", name}});
                    if (!readable(name))
                        failures.append(item->objectName() + " [" + text +
                                        "] has no descriptive accessible name");
                }
            }
            visit(item);
        }
    };
    visit(c.window->contentItem());
    c.driver->log({{"action", "accessible_icon_names"},
                   {"language", c.localization->language()},
                   {"controls", entries}});
}
} // namespace
bool accessibilityScenario(Context &c, const QString &id) {
    if (id != "UX-KEY-012" && id != "UX-KEY-013" && id != "UX-KEY-014")
        return false;
    // Process-local Qt AX activation; this does not enable system VoiceOver or alter permissions.
    QAccessible::setActive(true);
    c.openFirst();
    auto &d = *c.driver;
    if (id == "UX-KEY-012") {
        QStringList failures;
        c.addMask(0);
        c.saved();
        for (int language = 0; language < c.localization->languages().size(); ++language) {
            d.click("settingsButton");
            d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
                   "AX language preferences opened");
            d.choose("languageChoice", language);
            d.key(Qt::Key_Escape);
            d.wait([&] { return !d.object("preferencesPanel")->property("visible").toBool(); },
                   "AX language preferences closed");
            d.click("main.developButton");
            d.click("preset.tabs.presets");
            d.frame();
            iconNames(c, failures);
            d.click("main.libraryButton");
            d.frame();
            iconNames(c, failures);
        }
        failures.removeDuplicates();
        require(failures.isEmpty(), failures.join("; "));
        return true;
    }
    if (id == "UX-KEY-013") {
        for (const auto *selector :
             {"develop.exposureSlider", "develop.saturationSlider", "cropAngleSlider",
              "sharpen.amountSlider", "noise.luminanceSlider",
              "develop.transform.horizontalSlider"}) {
            c.reveal(selector);
            auto *control = d.item(selector);
            auto *interface = interfaceFor(control);
            auto *value = interface->valueInterface();
            require(interface->role() == QAccessible::Slider &&
                        readable(interface->text(QAccessible::Name)) && value,
                    "Slider has no accessible name/value role: " + QString(selector));
            require(value->minimumValue().toDouble() == control->property("from").toDouble() &&
                        value->maximumValue().toDouble() == control->property("to").toDouble(),
                    "Accessible slider range differs from its actual range");
            const auto before = c.editor.currentSettings();
            d.drag(selector, d.sliderThumb(selector), {.6, .5});
            d.frame();
            require(c.editor.currentSettings() != before &&
                        std::abs(value->currentValue().toDouble() -
                                 control->property("value").toDouble()) < 1e-8,
                    "Accessible value did not follow actual slider input");
            d.log({{"action", "accessible_slider_value"},
                   {"id", selector},
                   {"name", interface->text(QAccessible::Name)},
                   {"value", value->currentValue().toDouble()},
                   {"minimum", value->minimumValue().toDouble()},
                   {"maximum", value->maximumValue().toDouble()}});
        }
        return true;
    }
    const QStringList primary{"main.libraryButton", "main.developButton", "settingsButton",
                              "updates.bell",       "main.undoButton",    "main.redoButton",
                              "exportButton"};
    QMap<QString, int> counts;
    QSet<QAccessibleInterface *> visited;
    std::function<void(QAccessibleInterface *)> visit = [&](QAccessibleInterface *interface) {
        require(interface && interface->isValid() && !visited.contains(interface),
                "Accessible tree contains an invalid or repeated node");
        visited.insert(interface);
        if (const auto *object = interface->object();
            object && primary.contains(object->objectName())) {
            ++counts[object->objectName()];
            require(readable(interface->text(QAccessible::Name)),
                    "Primary accessible control is unlabeled: " + object->objectName());
        }
        require(visited.size() < 5000, "Accessible tree is unexpectedly unbounded for ten photos");
        for (int index = 0; index < interface->childCount(); ++index)
            visit(interface->child(index));
    };
    visit(interfaceFor(c.window));
    for (const auto &name : primary)
        require(counts[name] == 1,
                "Primary control is absent or duplicated in the accessible tree: " + name);
    d.log({{"action", "accessible_tree_valid"},
           {"nodes", visited.size()},
           {"primary_controls", primary.size()}});
    return true;
}
} // namespace ux
