#include "apps/shared/PresetStore.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStyleHints>
#include <cmath>
#include <vector>

namespace ux {
namespace {
const QString wbChoice = "develop.whiteBalance.preset";
struct KeyboardNavigation {
    Qt::TabFocusBehavior previous = QGuiApplication::styleHints()->tabFocusBehavior();
    KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    }
    ~KeyboardNavigation() {
        QGuiApplication::styleHints()->setTabFocusBehavior(previous);
    }
};
QString sliderID(const QString &parameter) {
    const bool detail = parameter.startsWith("sharpen.") || parameter.startsWith("noise.");
    return (detail ? QString{} : QString("develop.")) + parameter + "Slider";
}
void exact(Context &c, const luma::EditDocument &expected, const QString &reason) {
    require(c.editor.currentSettings() == expected && c.editor.error().isEmpty(), reason);
}
QImage rendered(Context &c, const luma::EditDocument &expected) {
    const auto pixels = fixtureReference(c, expected, c.editor.sourcePath());
    c.driver->wait(
        [&] {
            auto *image = c.driver->item("develop.preview");
            return !c.editor.busy() && image->isVisible() &&
                   image->property("status").toInt() == 1 &&
                   image->property("source").toUrl().toString() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == pixels;
        },
        "WB/numeric preview matches the complete expected document");
    exact(c, expected, "Preview readiness changed the expected WB/numeric document");
    c.driver->frame();
    return pixels;
}
void durable(Context &c, const luma::EditDocument &expected) {
    c.saved();
    const auto active = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
    require(active && active->id == c.editor.photoID().toStdString() && active->edits == expected,
            "WB/numeric document was not durably saved for the active asset");
}
void tabFocus(Context &c, const QString &name) {
    auto &d = *c.driver;
    c.reveal(name);
    const auto before = c.editor.currentSettings();
    auto *target = d.item(name);
    // Querying nextItemInFocusChain does not predict focus-scope/proxy routing.
    // Exercise actual Tab input until this control acknowledges active focus.
    int steps = 0;
    while (!target->hasActiveFocus() && steps < 240) {
        d.key(Qt::Key_Tab);
        ++steps;
        exact(c, before, "Actual Tab traversal unexpectedly edited the document");
    }
    c.reveal(name);
    require(target->hasActiveFocus() && !target->property("pressed").toBool(),
            "Native Tab input did not leave an unpressed focused slider");
    exact(c, before, "Restoring focused slider visibility edited the document");
    d.log({{"action", "actual_tab_focus_acquired"}, {"control", name}, {"steps", steps}});
}
void controls(Context &c, const luma::EditDocument &expected, int namedIndex = -1) {
    const bool enabled = expected.enabled(luma::Correction::whiteBalance);
    require(c.editor.customWhiteBalance() == expected.customWhiteBalance &&
                c.driver->object("develop.whiteBalance.enabled")->property("checked").toBool() ==
                    enabled &&
                c.driver->item(wbChoice)->isEnabled() == enabled &&
                c.driver->item(sliderID("temperature"))->isEnabled() == enabled &&
                c.driver->item(sliderID("tint"))->isEnabled() == enabled,
            "As Shot/Custom mode disagrees with WB control availability");
    const int index = c.driver->object(wbChoice)->property("currentIndex").toInt();
    if (!expected.customWhiteBalance)
        require(index == 0, "As Shot document has a non-As-Shot mode caption");
    else
        require(index == 6 || (namedIndex > 0 && index == namedIndex),
                "Custom/manual WB has an unrelated mode caption");
    require(c.driver->object(sliderID("temperature"))->property("value").toDouble() ==
                    expected.temperature &&
                c.driver->object(sliderID("tint"))->property("value").toDouble() == expected.tint,
            "WB thumbs show another document's retained numeric values");
}
void boundary(Context &c, const QString &parameter, bool maximum) {
    auto &d = *c.driver;
    const auto name = sliderID(parameter);
    c.reveal(name);
    auto *slider = d.item(name);
    require(slider->isVisible() && slider->isEnabled(), "WB boundary slider is unavailable");
    auto expected = c.editor.currentSettings();
    expected.setParameter(parameter.toStdString(),
                          slider->property(maximum ? "to" : "from").toDouble());
    d.drag(name, d.sliderThumb(name), {maximum ? 1. : 0., .5});
    require(!slider->property("pressed").toBool(), "WB pointer release left an active gesture");
    exact(c, expected, "WB boundary input changed an unrelated field or missed its endpoint");
    controls(c, expected);
    rendered(c, expected);
}
void oneUndoRedo(Context &c, const luma::EditDocument &before, const luma::EditDocument &after) {
    c.driver->click("main.undoButton");
    exact(c, before, "WB action was not one complete Undo");
    rendered(c, before);
    c.driver->click("main.redoButton");
    exact(c, after, "WB Redo did not restore the complete resulting document");
    rendered(c, after);
}
void choose(Context &c, int index, const luma::EditDocument &expected) {
    c.reveal(wbChoice);
    c.driver->choose(wbChoice, index);
    exact(c, expected, "Actual WB preset selection did not apply its declared semantics");
    controls(c, expected, index > 0 && index < 6 ? index : -1);
    rendered(c, expected);
}
struct PublicPair {
    QString aPath, bPath, aID, bID;
    int aRow = -1, bRow = -1;
};
PublicPair importPublicPair(Context &c) {
    auto &d = *c.driver;
    PublicPair pair;
    pair.aPath = QFileInfo(c.fixture + "/stress/Lens.DNG").canonicalFilePath();
    pair.bPath = QFileInfo(c.fixture + "/Photo-0001.png").canonicalFilePath();
    require(!pair.aPath.isEmpty() && !pair.bPath.isEmpty(),
            "WB scenarios require only the runner-generated public DNG/PNG pair");
    // Selection boundary only. Native chooser/HID selection is not claimed.
    const QList<QUrl> sources{QUrl::fromLocalFile(pair.aPath), QUrl::fromLocalFile(pair.bPath)};
    require(d.object("library.pane")->setProperty("sources", QVariant::fromValue(sources)),
            "Cannot prepare the public WB import source selection");
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "WB public-pair import dialog opened");
    d.choose("library.importMode", 0);
    d.click("library.importStart");
    d.wait([&] { return !c.library.working() && !c.library.loading() && c.library.count() == 2; },
           "WB public-pair import completed", 60000);
    require(c.library.error().isEmpty(), c.library.error());
    for (int row = 0; row < 2; ++row) {
        d.wait(
            [&] {
                return !c.library.data(c.library.index(row), LibraryModel::AssetID)
                            .toString()
                            .isEmpty();
            },
            "WB public asset metadata page ready");
        const auto path =
            c.library.data(c.library.index(row), LibraryModel::Path).toUrl().toLocalFile();
        const auto id = c.library.data(c.library.index(row), LibraryModel::AssetID).toString();
        if (path == pair.aPath) {
            pair.aRow = row;
            pair.aID = id;
        }
        if (path == pair.bPath) {
            pair.bRow = row;
            pair.bID = id;
        }
    }
    require(pair.aRow >= 0 && pair.bRow >= 0 && pair.aID != pair.bID,
            "WB import did not preserve two distinct generated public source identities");
    d.doubleClick(c.card(pair.aRow));
    d.wait([&] { return !c.editor.busy() && c.editor.photoID() == pair.aID; },
           "WB public DNG activated");
    c.loaded();
    require(c.editor.sourcePath() == pair.aPath,
            "WB activated a source outside its public fixture");
    return pair;
}
void protectUnrelatedLight(Context &c) {
    const KeyboardNavigation navigation;
    const auto before = c.editor.currentSettings();
    tabFocus(c, sliderID("exposure"));
    c.driver->key(Qt::Key_Right);
    auto expected = before;
    expected.exposure += .01;
    exact(c, expected, "WB fixture failed to add one unrelated native Exposure step");
    durable(c, expected);
    rendered(c, expected);
}
void whiteBalanceSemantics(Context &c) {
    importPublicPair(c);
    protectUnrelatedLight(c);
    const auto base = c.editor.currentSettings();
    require(!base.customWhiteBalance, "Public WB fixture must start in As Shot");
    controls(c, base);
    const auto asShotPixels = rendered(c, base);
    auto custom = base;
    custom.customWhiteBalance = true;
    choose(c, 6, custom);
    oneUndoRedo(c, base, custom);
    const std::vector<double> temperatures{5500, 6500, 7500, 2850, 4000};
    for (int index = 1; index <= 5; ++index) {
        boundary(c, "tint", true);
        const auto beforePreset = c.editor.currentSettings();
        auto expected = beforePreset;
        expected.customWhiteBalance = true;
        expected.temperature = temperatures[static_cast<std::size_t>(index - 1)];
        expected.tint = 0;
        choose(c, index, expected);
        oneUndoRedo(c, beforePreset, expected);
        durable(c, expected);
    }
    auto asShot = c.editor.currentSettings();
    asShot.customWhiteBalance = false;
    asShot.tint = 0;
    choose(c, 0, asShot);
    require(asShot.temperature == 4000 && rendered(c, asShot) == asShotPixels,
            "As Shot did not retain Kelvin while restoring the source-camera WB effect");
    // Enabled manual controls intentionally transition As Shot -> Custom on input.
    boundary(c, "temperature", false);
    boundary(c, "tint", true);
    const auto retained = c.editor.currentSettings();
    require(retained.customWhiteBalance && retained.temperature == 2000 && retained.tint == 150,
            "Manual controls did not leave As Shot for a complete Custom WB document");
    const auto retainedPixels = rendered(c, retained);
    require(retainedPixels != asShotPixels, "Public WB fixture has no distinct manual effect");
    c.reveal("develop.whiteBalance.enabled");
    c.driver->click("develop.whiteBalance.enabled");
    auto disabled = retained;
    disabled.setEnabled(luma::Correction::whiteBalance, false);
    exact(c, disabled, "Disabling WB discarded retained Kelvin/Tint or changed other groups");
    controls(c, disabled);
    require(rendered(c, disabled) == asShotPixels,
            "Disabled Custom WB did not restore the source-camera WB effect");
    oneUndoRedo(c, retained, disabled);
    c.reveal("develop.whiteBalance.enabled");
    c.driver->click("develop.whiteBalance.enabled");
    exact(c, retained, "Re-enabling WB did not restore the complete retained Custom document");
    controls(c, retained);
    require(rendered(c, retained) == retainedPixels, "Re-enabling WB changed its retained effect");
    durable(c, retained);
}
QByteArray readFile(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read owned WB/numeric evidence: " + path);
    const auto result = file.readAll();
    require(file.error() == QFileDevice::NoError, "Cannot read complete WB/numeric evidence");
    return result;
}
void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
            "Cannot write owned WB/numeric evidence: " + path);
    file.close();
    require(file.error() == QFileDevice::NoError, "Cannot finish owned WB/numeric evidence");
}
QImage normalized(QImage image) {
    image = image.convertToFormat(QImage::Format_RGBA8888);
    image.setColorSpace({});
    image.setDevicePixelRatio(1);
    return image;
}
void expectedCatalog(const QString &path, const luma::AssetRecord &asset,
                     const luma::EditDocument &document) {
    luma::Catalog expected(path.toStdString());
    const auto record = expected.addAndSelect(asset.path, asset.checksum, asset.metadata);
    expected.saveEdits(record.id, document);
    require(expected.activeAsset() && expected.activeAsset()->edits == document,
            "WB restart oracle serialization lost a field");
}
void activate(Context &c, int row, const QString &id, const QString &source,
              const luma::EditDocument &expected) {
    c.driver->click(c.card(row, true));
    c.driver->wait([&] { return !c.editor.busy() && c.editor.photoID() == id; },
                   "WB filmstrip switched to the expected asset");
    c.loaded();
    require(c.editor.sourcePath() == source && !c.editor.canUndo() && !c.editor.canRedo(),
            "WB asset switch crossed source identity or session history");
    exact(c, expected, "WB switch lost or crossed an asset's complete document");
    controls(c, expected);
    rendered(c, expected);
}
void quit(Context &c) {
    c.driver->quitShortcut();
    c.driver->wait(
        [&] {
            return !c.window->isVisible() && !c.editor.saving() && !c.editor.busy() &&
                   !c.library.importing();
        },
        "WB restart closed after durable coordinated shutdown");
    c.checkData();
}
void checkNumeric(Context &c, const QString &parameter) {
    const auto name = "develop." + parameter + ".value";
    c.reveal(name);
    c.driver->frame();
    auto *label = c.driver->item(name);
    const auto text = label->property("text").toString();
    const QRegularExpression number("^([+-]?\\d+(?:\\.(\\d+))?)(?:[ \\t]+(?:EV|K))?$");
    const auto match = number.match(text);
    require(label->isVisible() && match.hasMatch(),
            "Rendered numeric Label is absent, NaN/Inf or malformed: " + name + " = " + text);
    bool parsed = false;
    const double visible = match.captured(1).toDouble(&parsed);
    const auto value = c.editor.currentSettings().parameter(parameter.toStdString());
    const auto decimals = match.captured(2).size();
    const double tolerance = .5 * std::pow(10., -static_cast<double>(decimals)) + 1e-12;
    require(parsed && std::isfinite(visible) && std::isfinite(value) &&
                std::abs(visible - value) <= tolerance,
            "Numeric Label does not represent its current parameter: " + name);
    require(!(visible == 0 && match.captured(1).startsWith('-')),
            "Numeric Label renders a negative zero: " + name + " = " + text);
    c.driver->log({{"action", "rendered_numeric_label_checked"},
                   {"parameter", parameter},
                   {"text", text},
                   {"document_value", value}});
}
void importNearZero(Context &c, double value, int ordinal) {
    luma::DevelopPreset preset;
    preset.name = "Numeric near zero " + QString::number(ordinal);
    preset.folder = "Public UX numeric fixtures";
    preset.groups = 1;
    preset.parameters = {"exposure"};
    preset.settings.exposure = value;
    const auto path = c.root + "/numeric-near-zero-" + QString::number(ordinal) + ".json";
    writeFile(path, luma::encodePreset(preset));
    auto *dialog = c.driver->object("presetImportDialog");
    require(!dialog->property("visible").toBool() &&
                dialog->setProperty("selectedFile", QUrl::fromLocalFile(path)),
            "Cannot stage the generated public preset at the FileDialog boundary");
    c.driver->log({{"action", "file_dialog_selection_fixture_boundary"},
                   {"path", path},
                   {"native_selection_exercised", false}});
    require(QMetaObject::invokeMethod(dialog, "accepted", Qt::DirectConnection),
            "Preset FileDialog has no production acceptance handler");
    const auto id = c.presets.importedId();
    require(!id.isEmpty() && c.presets.error().isEmpty() &&
                c.presets.snapshot(id).settings.exposure == value,
            "Generated sparse near-zero preset failed to import without quantization");
    const auto row = "preset." + id;
    c.driver->wait([&] { return c.driver->find(row) != nullptr; },
                   "near-zero preset's actual UI row is ready");
    auto *list = c.driver->item("presetList");
    auto *item = c.driver->item(row);
    bool reachable = false;
    for (int attempt = 0; attempt < 80; ++attempt) {
        c.driver->wait([&] { return !list->property("moving").toBool(); },
                       "near-zero preset list finished its actual scroll");
        c.driver->frame();
        const auto center = item->mapToItem(list, {item->width() / 2, item->height() / 2});
        if (item->isVisible() && list->isVisible() && list->contains(center)) {
            reachable = true;
            break;
        }
        c.driver->wheel("presetList", center.y() < 0 ? 120 : -120);
        c.driver->frame();
    }
    require(reachable, "Near-zero preset must be reachable inside the clipped list");
    QSignalSpy clicked(item, SIGNAL(clicked()));
    require(clicked.isValid(), "Near-zero preset has no clicked signal");
    const auto before = c.editor.currentSettings();
    const auto center = item->mapToItem(list, {item->width() / 2, item->height() / 2});
    c.driver->log({{"action", "near_zero_preset_before_click"},
                   {"ordinal", ordinal},
                   {"center_y", center.y()},
                   {"viewport_height", list->height()},
                   {"content_y", list->property("contentY").toDouble()},
                   {"moving", list->property("moving").toBool()}});
    c.driver->click(row);
    require(clicked.count() == 1, "Real input did not activate the visible near-zero preset row");
    c.driver->move("develop.preview", {.5, .5});
    auto expected = before;
    expected.exposure = value;
    exact(c, expected, "Near-zero preset did not preserve its exact sparse exposure value");
    rendered(c, expected);
    durable(c, expected);
    checkNumeric(c, "exposure");
}
void numericLabels(Context &c) {
    c.openFirst();
    const auto base = c.editor.currentSettings();
    const QStringList basic{"temperature", "tint",   "exposure", "contrast", "highlights",
                            "shadows",     "whites", "blacks",   "vibrance", "saturation"};
    const QStringList detail{"sharpen.amount",          "sharpen.radius",  "sharpen.detail",
                             "sharpen.masking",         "noise.luminance", "noise.luminanceDetail",
                             "noise.luminanceContrast", "noise.color",     "noise.colorDetail",
                             "noise.colorSmoothness"};
    QStringList boundaryControls = basic;
    boundaryControls.append(detail);
    for (const auto &parameter : boundaryControls) {
        for (const bool maximum : {false, true}) {
            const auto name = sliderID(parameter);
            c.reveal(name);
            auto expected = base;
            expected.setParameter(
                parameter.toStdString(),
                c.driver->object(name)->property(maximum ? "to" : "from").toDouble());
            const bool undoBefore = c.editor.canUndo(), redoBefore = c.editor.canRedo();
            c.driver->drag(name, c.driver->sliderThumb(name), {maximum ? 1. : 0., .5});
            exact(c, expected, "Numeric boundary fixture changed the wrong complete document");
            checkNumeric(c, parameter);
            rendered(c, expected);
            durable(c, expected);
            if (expected != base) {
                c.driver->click("main.undoButton");
                exact(c, base, "Numeric boundary was not one complete Undo");
                rendered(c, base);
            } else {
                require(c.editor.canUndo() == undoBefore && c.editor.canRedo() == redoBefore,
                        "Numeric boundary no-op polluted the history branch");
            }
        }
    }
    c.reveal("curves.mode");
    c.driver->choose("curves.mode", 1);
    for (const auto *parameter :
         {"curve.highlights", "curve.lights", "curve.darks", "curve.shadows", "lens.distortion",
          "lens.vignetting", "transform.rotate", "transform.horizontal", "transform.vertical",
          "transform.aspect", "transform.scale", "transform.xOffset", "transform.yOffset"})
        checkNumeric(c, parameter);
    for (const auto band : luma::hslBands) {
        const auto name = QString::fromUtf8(band.data(), static_cast<qsizetype>(band.size()));
        c.reveal("hsl.band." + name);
        c.driver->click("hsl.band." + name);
        for (const auto *field : {"hue", "saturation", "luminance"})
            checkNumeric(c, "hsl." + name + '.' + field);
    }
    exact(c, base, "Inspecting rendered numeric Labels changed the document");
    // Legal external presets retain sub-step precision. They exercise real label
    // formatting around zero without injecting invalid NaN/Inf into the controller.
    int ordinal = 0;
    for (const double value : {-.001, 0., .001})
        importNearZero(c, value, ordinal++);
}
void wheelAfterDrag(Context &c) {
    c.openFirst();
    auto &d = *c.driver;
    const auto before = c.editor.currentSettings();
    c.reveal(sliderID("exposure"));
    d.drag(sliderID("exposure"), d.sliderThumb(sliderID("exposure")), {.65, .5});
    const auto after = c.editor.currentSettings();
    auto expected = before;
    expected.exposure = after.exposure;
    require(after != before && after == expected &&
                !d.object(sliderID("exposure"))->property("pressed").toBool(),
            "Wheel fixture did not complete an accepted single Exposure drag");
    rendered(c, after);
    auto *flickable =
        qvariant_cast<QObject *>(d.object("develop.adjustmentsScroll")->property("contentItem"));
    require(flickable, "Wheel fixture has no actual adjustment Flickable");
    const double y = flickable->property("contentY").toDouble();
    require(flickable->property("contentHeight").toDouble() >
                y + flickable->property("height").toDouble() + 1,
            "Wheel fixture has no remaining downward scroll range");
    d.wheel(sliderID("exposure"), -120);
    d.wait(
        [&] {
            return flickable->property("contentY").toDouble() > y &&
                   !flickable->property("moving").toBool();
        },
        "ordinary wheel after an accepted drag scrolled the page");
    exact(c, after, "Wheel after slider release accidentally edited a parameter");
    require(!d.object(sliderID("exposure"))->property("pressed").toBool(),
            "Wheel revived a released slider gesture");
    oneUndoRedo(c, before, after);
    c.reveal(sliderID("exposure"));
    d.drag(sliderID("exposure"), d.sliderThumb(sliderID("exposure")), {.35, .5});
    const auto second = c.editor.currentSettings();
    expected = after;
    expected.exposure = second.exposure;
    require(second != after && second == expected, "Fresh drag after wheel was ignored or leaked");
    oneUndoRedo(c, after, second);
    durable(c, second);
    rendered(c, second);
    d.log({{"action", "released_slider_then_wheel_verified"},
           {"ordinary_angle_delta_wheel", true},
           {"trackpad_momentum_exercised", false}});
}
} // namespace

bool whiteBalanceScenario(Context &c, const QString &id) {
    if (id == "UX-SLD-015")
        whiteBalanceSemantics(c);
    else if (id == "UX-SLD-017")
        numericLabels(c);
    else if (id == "UX-DSC-004")
        wheelAfterDrag(c);
    else
        return false;
    return true;
}

bool whiteBalanceRestartScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-SLD-016")
        return false;
    auto &d = *c.driver;
    const auto manifestPath = c.stateRoot + "/wb-expected.json";
    if (stage == "prepare") {
        const auto pair = importPublicPair(c);
        auto custom = c.editor.currentSettings();
        custom.customWhiteBalance = true;
        choose(c, 6, custom);
        boundary(c, "temperature", false);
        boundary(c, "tint", true);
        protectUnrelatedLight(c);
        const auto expectedA = c.editor.currentSettings();
        durable(c, expectedA);
        const auto pixelsA = rendered(c, expectedA);
        const auto originalB =
            luma::Catalog(c.catalogFile.toStdString()).assetByID(pair.bID.toStdString());
        require(originalB.has_value(), "WB restart fixture lost its second asset");
        activate(c, pair.bRow, pair.bID, pair.bPath, originalB->edits);
        c.reveal(sliderID("exposure"));
        d.drag(sliderID("exposure"), d.sliderThumb(sliderID("exposure")), {1., .5});
        auto expectedB = originalB->edits;
        expectedB.exposure = 5;
        exact(c, expectedB, "Editing B crossed A's Custom WB values");
        controls(c, expectedB);
        durable(c, expectedB);
        rendered(c, expectedB);
        activate(c, pair.aRow, pair.aID, pair.aPath, expectedA);
        require(rendered(c, expectedA) == pixelsA, "A->B->A changed the Custom WB effect");
        const luma::Catalog catalog(c.catalogFile.toStdString());
        const auto assetA = catalog.assetByID(pair.aID.toStdString());
        const auto assetB = catalog.assetByID(pair.bID.toStdString());
        require(assetA && assetB && assetA->edits == expectedA && assetB->edits == expectedB,
                "A->B->A did not preserve independent durable documents");
        expectedCatalog(c.stateRoot + "/wb-a-expected.sqlite", *assetA, expectedA);
        expectedCatalog(c.stateRoot + "/wb-b-expected.sqlite", *assetB, expectedB);
        const auto pixelPath = c.stateRoot + "/wb-before-restart.png";
        require(pixelsA.save(pixelPath), "Cannot save owned public WB pixel oracle");
        writeJson(manifestPath, {{"source_a", pair.aPath},
                                 {"source_b", pair.bPath},
                                 {"asset_a", pair.aID},
                                 {"asset_b", pair.bID},
                                 {"row_a", pair.aRow},
                                 {"row_b", pair.bRow},
                                 {"pixel_oracle", pixelPath}});
        quit(c);
        return true;
    }
    require(stage == "verify", "Unknown WB restart stage");
    QJsonParseError parse;
    const auto file = QJsonDocument::fromJson(readFile(manifestPath), &parse);
    require(parse.error == QJsonParseError::NoError && file.isObject(),
            "WB restart manifest is invalid");
    const auto expected = file.object();
    require(expected["source_a"].toString() ==
                    QFileInfo(c.fixture + "/stress/Lens.DNG").canonicalFilePath() &&
                expected["source_b"].toString() ==
                    QFileInfo(c.fixture + "/Photo-0001.png").canonicalFilePath() &&
                expected["pixel_oracle"].toString() == c.stateRoot + "/wb-before-restart.png",
            "WB restart manifest names a source outside the generated public pair");
    const auto a =
        luma::Catalog((c.stateRoot + "/wb-a-expected.sqlite").toStdString()).activeAsset();
    const auto b =
        luma::Catalog((c.stateRoot + "/wb-b-expected.sqlite").toStdString()).activeAsset();
    require(a && b && a->edits.customWhiteBalance && a->edits.temperature == 2000 &&
                a->edits.tint == 150 && !b->edits.customWhiteBalance,
            "WB restart lost its typed independent expected documents");
    c.editor.restore(); // Same startup boundary used by the production/restart harness.
    c.loaded();
    require(c.editor.photoID() == expected["asset_a"].toString() &&
                c.editor.sourcePath() == expected["source_a"].toString() && !c.editor.canUndo() &&
                !c.editor.canRedo(),
            "Process restart restored another WB asset or session history");
    exact(c, a->edits, "Process restart lost part of Custom WB or another edit group");
    controls(c, a->edits);
    const auto pixels = rendered(c, a->edits);
    const QImage before(expected["pixel_oracle"].toString());
    require(!before.isNull() && normalized(pixels) == normalized(before),
            "Custom WB pixels changed across the real process restart");
    const luma::Catalog catalog(c.catalogFile.toStdString());
    const auto actualA = catalog.assetByID(expected["asset_a"].toString().toStdString());
    const auto actualB = catalog.assetByID(expected["asset_b"].toString().toStdString());
    require(actualA && actualB && actualA->edits == a->edits && actualB->edits == b->edits,
            "Process restart crossed the two durable WB documents");
    activate(c, expected["row_b"].toInt(), expected["asset_b"].toString(),
             expected["source_b"].toString(), b->edits);
    activate(c, expected["row_a"].toInt(), expected["asset_a"].toString(),
             expected["source_a"].toString(), a->edits);
    require(normalized(rendered(c, a->edits)) == normalized(before),
            "Post-restart A->B->A changed the restored WB effect");
    durable(c, a->edits);
    quit(c);
    return true;
}
} // namespace ux
