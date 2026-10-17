#include "tests/ux/UxContext.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>

namespace ux {
namespace {
QString translated(const QString &language, const QString &source) {
    if (language == "en")
        return source;
    QFile file(":/luma/i18n/" + language + ".json");
    require(file.open(QIODevice::ReadOnly), "Missing public language resource: " + language);
    return QJsonDocument::fromJson(file.readAll())
        .object()["messages"]
        .toObject()
        .value(source)
        .toString(source);
}
void preferences(Context &c) {
    c.driver->click("settingsButton");
    c.driver->wait(
        [&] { return c.driver->object("preferencesPanel")->property("opened").toBool(); },
        "Preferences opened");
}
void closePreferences(Context &c) {
    c.driver->key(Qt::Key_Escape);
    c.driver->wait(
        [&] { return !c.driver->object("preferencesPanel")->property("visible").toBool(); },
        "Preferences closed");
}
void onScreen(Context &c, const QString &selector) {
    auto *item = c.driver->item(selector);
    const auto box = item->mapRectToScene(item->boundingRect());
    require(item->isVisible() && item->isEnabled() && box.width() > 0 && box.height() > 0 &&
                box.left() >= 0 && box.top() >= 0 && box.right() <= c.window->width() + .5 &&
                box.bottom() <= c.window->height() + .5,
            "Localized critical control escaped the viewport: " + selector);
}
} // namespace
bool localeRestartScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-LOC-006" && id != "UX-LOC-007")
        return false;
    auto &d = *c.driver;
    if (stage == "prepare") {
        if (id == "UX-LOC-006") {
            preferences(c);
            d.choose("languageChoice", 7);
            require(c.localization->language() == "fr" && c.localization->error().isEmpty(),
                    "Foreign language preparation failed");
            closePreferences(c);
        } else {
            QFile file(c.stateRoot + "/language.json");
            require(file.open(QIODevice::WriteOnly) && file.write("{broken-preferences") > 0,
                    "Cannot prepare corrupt language preferences");
        }
        d.quitShortcut();
        d.wait([&] { return !c.window->isVisible(); }, "language restart clean shutdown");
        return true;
    }
    require(stage == "verify", "Unknown localization restart stage");
    const QString expected = id == "UX-LOC-006" ? "fr" : "en";
    require(c.localization->language() == expected, "Process restart restored the wrong language");
    require(id == "UX-LOC-006" ? c.localization->error().isEmpty()
                               : !c.localization->error().isEmpty(),
            "Language preference error/fallback status is incorrect");
    preferences(c);
    require(d.object("preferencesPanel")->property("title").toString() ==
                translated(expected, "Settings"),
            "Restarted UI was not translated to the restored/fallback language");
    closePreferences(c);
    return true;
}
bool localizationScenario(Context &c, const QString &id) {
    static const QSet<QString> supported{"UX-LOC-001", "UX-LOC-002", "UX-LOC-003",
                                         "UX-LOC-004", "UX-LOC-005", "UX-LOC-008"};
    if (!supported.contains(id))
        return false;
    auto &d = *c.driver;
    if (id == "UX-LOC-005") {
        const auto name = QString::fromUtf8("Снимок-%{filename}-{sequence}-{date}-🌅.png");
        require(QDir().mkpath(c.root + "/unicode"), "Cannot create Unicode fixture directory");
        require(QFile::copy(c.fixture + "/Photo-0000.png", c.root + "/unicode/" + name),
                "Cannot create literal-name fixture");
        c.fixture = c.root + "/unicode";
        c.import(1);
        require(d.object(c.card(0))->property("filename").toString() == name,
                "Library treated filename as a format string");
        d.doubleClick(c.card(0));
        c.loaded();
        require(c.editor.filename() == name && c.window->title().endsWith(name),
                "Develop treated filename as a format string");
        require(QDir().mkpath(c.root + "/exports"), "Cannot create Unicode export directory");
        d.click("exportButton");
        d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
               "literal-name export opened");
        d.type("export.destination", c.root + "/exports");
        d.type("exportNaming", "{filename}");
        d.choose("exportFormat", 1);
        d.click("export.current");
        d.wait([&] { return !c.exporter.working() && c.exporter.completed() == 1; },
               "literal-name export complete");
        require(c.exporter.succeeded() == 1 && c.exporter.error().isEmpty() &&
                    c.exporter.failures().isEmpty() &&
                    QFileInfo::exists(c.root + "/exports/" + name),
                "Export reinterpreted tokens inside the literal source filename");
        return true;
    }
    c.openFirst();
    c.reveal("develop.exposureSlider");
    d.drag("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"), {.65, .5});
    c.saved();
    if (id == "UX-LOC-008") {
        const auto document = c.editor.currentSettings();
        const auto language = c.localization->language();
        require(QDir().mkpath(c.stateRoot + "/language.json"),
                "Cannot create preferences-write fault");
        preferences(c);
        d.choose("languageChoice", 1);
        require(!c.localization->error().isEmpty() && c.localization->language() == language &&
                    d.object("languageChoice")->property("currentValue").toString() == language,
                "Preference write failure changed active/visible language");
        require(c.editor.currentSettings() == document, "Preference failure mutated edits");
        require(QDir().rmdir(c.stateRoot + "/language.json"),
                "Cannot release preferences-write fault");
        d.choose("languageChoice", 1);
        require(c.localization->language() == "ru" && c.localization->error().isEmpty(),
                "Preference retry did not recover");
        closePreferences(c);
        return true;
    }
    if (id == "UX-LOC-003" || id == "UX-LOC-004")
        d.resize({1000, 640});
    d.click("main.libraryButton");
    d.type("library.search", "Photo-0000");
    d.wait([&] { return !c.library.loading() && c.library.count() == 1; },
           "localized search fixture");
    d.click(c.card(0));
    const auto selection = c.library.selectionIDs();
    const auto document = c.editor.currentSettings();
    const auto source = c.editor.sourcePath();
    const auto languages = c.localization->languages();
    require(languages.size() == 8, "Expected eight available interface languages");
    for (int i = 0; i < languages.size(); ++i) {
        const auto code = languages[i].toMap()["code"].toString();
        preferences(c);
        d.choose("languageChoice", i);
        d.wait([&] { return c.localization->language() == code; }, "Runtime language activated");
        d.frame();
        require(c.localization->error().isEmpty(), c.localization->error());
        require(d.object("preferencesPanel")->property("title").toString() ==
                    translated(code, "Settings"),
                "Visible preferences title was not retranslated");
        require(d.object("main.libraryButton")->property("text").toString() ==
                    translated(code, "Library"),
                "Visible Library control was not retranslated");
        require(d.object("settingsVersion")->property("text").toString() ==
                    "Luma " + QCoreApplication::applicationVersion(),
                "Language switch changed displayed version");
        QFile preferences(c.stateRoot + "/language.json");
        require(preferences.open(QIODevice::ReadOnly), "Language preference was not persisted");
        const auto persisted = QJsonDocument::fromJson(preferences.readAll()).object();
        require(persisted["language"].toString() == code,
                "Persisted language differs from visible selection");
        d.screenshot(c.root + "/locale-" + code + ".png");
        closePreferences(c);
        if (id == "UX-LOC-003" || id == "UX-LOC-004") {
            for (const auto *selector :
                 {"main.libraryButton", "main.developButton", "settingsButton", "exportButton",
                  "library.importButton", "library.search"})
                onScreen(c, selector);
            if (id == "UX-LOC-004") {
                d.click("library.importButton");
                d.wait(
                    [&] { return d.object("library.importDialog")->property("opened").toBool(); },
                    "localized Import opened");
                onScreen(c, "library.importStart");
                d.screenshot(c.root + "/import-" + code + ".png");
                d.key(Qt::Key_Escape);
                d.wait(
                    [&] { return !d.object("library.importDialog")->property("visible").toBool(); },
                    "localized Import closed");
                d.click("exportButton");
                d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
                       "localized Export opened");
                onScreen(c, "export.current");
                onScreen(c, "export.close");
                d.screenshot(c.root + "/export-" + code + ".png");
                d.click("export.close");
                d.wait([&] { return !d.object("exportPanel")->property("visible").toBool(); },
                       "localized Export closed");
            }
        }
        require(c.editor.currentSettings() == document && c.editor.sourcePath() == source &&
                    c.library.selectionIDs() == selection && c.library.count() == 1 &&
                    d.object("library.search")->property("text").toString() == "Photo-0000",
                "Language switch mutated edits, selection, or filtering");
    }
    return true;
}
} // namespace ux
