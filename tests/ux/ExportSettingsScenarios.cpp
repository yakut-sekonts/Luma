#include "apps/shared/PhotoDecoder.h"
#include "core/render/CpuRenderer.h"
#include "tests/ux/ExportPrivacyFixture.h"
#include "tests/ux/QueuedCallGate.h"
#include "tests/ux/UxContext.h"
#include <QColor>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QSignalSpy>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>

namespace ux {
namespace {
QByteArray bytes(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read owned export-settings evidence: " + path);
    return file.readAll();
}
QByteArray digest(const QString &path) {
    return QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256);
}
QJsonObject settings(Context &c) {
    return QJsonObject::fromVariantMap(c.exporter.settings());
}
QQuickItem *scroll(Context &c) {
    auto *view = qobject_cast<QQuickItem *>(
        qvariant_cast<QObject *>(c.driver->object("export.scroll")->property("contentItem")));
    require(view && view->property("contentY").isValid(), "Actual Export ScrollView is not ready");
    return view;
}
void reveal(Context &c, const QString &id) {
    auto &d = *c.driver;
    auto *target = d.item(id);
    auto *view = scroll(c);
    require(target->isVisible(), "Requested export field is not visible: " + id);
    for (int attempt = 0; attempt < 100; ++attempt) {
        d.wait([&] { return !view->property("moving").toBool(); },
               "export form discrete wheel settled");
        d.frame();
        const auto rectangle = target->mapRectToItem(view, target->boundingRect());
        if (rectangle.top() >= 0 && rectangle.bottom() <= view->height())
            return;
        d.wheel("export.scroll", rectangle.top() < 0 ? 120 : -120);
    }
    throw std::runtime_error("Actual Export wheel input cannot reveal field: " + id.toStdString());
}
void choose(Context &c, const QString &id, int index) {
    reveal(c, id);
    c.driver->choose(id, index);
}
void text(Context &c, const QString &id, const QString &value, bool spin = false) {
    reveal(c, id);
    c.driver->type(id, value);
    if (spin)
        c.driver->key(Qt::Key_Return);
    c.driver->key(Qt::Key_Tab); // Actual editingFinished, not direct form/controller mutation.
}
void open(Context &c) {
    c.driver->click("exportButton");
    c.driver->wait([&] { return c.driver->object("exportPanel")->property("opened").toBool(); },
                   "export-settings production panel opened");
}
void close(Context &c) {
    c.driver->click("export.close");
    c.driver->wait([&] { return !c.driver->object("exportPanel")->property("visible").toBool(); },
                   "export-settings production panel closed");
}
void importPrivate(Context &c, const QStringList &paths) {
    QList<QUrl> urls;
    for (const auto &path : paths)
        urls.append(QUrl::fromLocalFile(path));
    // Declared chooser-result fixture boundary; acceptance is the real Import button.
    c.driver->object("library.importDialog")->setProperty("sources", QVariant::fromValue(urls));
    c.driver->log(
        {{"action", "fixture_file_chooser_result"}, {"count", static_cast<qint64>(paths.size())}});
    c.driver->click("library.importButton");
    c.driver->wait(
        [&] { return c.driver->object("library.importDialog")->property("opened").toBool(); },
        "private export inputs Import dialog opened");
    c.driver->choose("library.importMode", 0);
    if (c.driver->object("library.importDuplicates")->property("checked").toBool())
        c.driver->click("library.importDuplicates");
    c.driver->click("library.importStart");
    c.driver->wait(
        [&] {
            return !c.library.working() && !c.library.loading() &&
                   c.library.count() == paths.size();
        },
        "all owned export fixture inputs committed");
    require(c.library.error().isEmpty(), "Owned export input import failed");
}
QString sourceAt(Context &c, int row) {
    c.driver->wait(
        [&] {
            return !c.library.loading() && !c.library.data(c.library.index(row), LibraryModel::Path)
                                                .toUrl()
                                                .toLocalFile()
                                                .isEmpty();
        },
        "owned export source metadata ready before scheduling boundary");
    return c.library.data(c.library.index(row), LibraryModel::Path).toUrl().toLocalFile();
}
void ownedPNG(const QString &path, int variant) {
    require(!QFileInfo::exists(path), "Generated export source must be a new owned file");
    QImage image(128, 96, QImage::Format_RGBA8888);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, variant == 0 ? QColor(x, y, 80) : QColor(70, x, y));
    require(image.save(path, "PNG"), "Cannot encode generated private export source");
}
void mark(const QString &path, const QColor &color) {
    QImage image(32, 32, QImage::Format_RGBA8888);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    image.fill(color);
    require(image.save(path, "PNG"), "Cannot write the owned solid-color watermark");
}
QImage plainReference(const QString &source, const luma::EditDocument &document) {
    const auto decoded = luma::PhotoDecoder().decode(source.toStdString());
    require(decoded.image.width == 128 && decoded.image.height == 96 && decoded.image.alpha.empty(),
            "Independent watermark reference requires the known opaque 128x96 fixture");
    const auto rendered =
        luma::renderCPU(decoded.image, document, decoded.metadata.orientation, {}, {},
                        decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
    return QImage(rendered.rgba.data(), rendered.width, rendered.height,
                  static_cast<qsizetype>(rendered.width) * 4, QImage::Format_RGBA8888)
        .copy();
}
void redWatermark(const QString &path, const QImage &plain) {
    const QImage output = QImage(path).convertToFormat(QImage::Format_RGBA8888);
    require(output.size() == QSize(128, 96), "Watermarked output has the wrong actual dimensions");
    // Public setting contract: size20% of 128 ->26px square, margin3% of96 ->3px,
    // bottom-right. This oracle never calls applyWatermark/resizeOutput/export code.
    const QRect markBounds(99, 67, 26, 26);
    for (int y = 70; y < 90; ++y)
        for (int x = 102; x < 122; ++x) {
            const auto pixel = output.pixelColor(x, y);
            require(pixel.red() >= 250 && pixel.green() <= 5 && pixel.blue() <= 5 &&
                        pixel.alpha() == 255,
                    "Queue did not retain the old opaque red watermark snapshot");
        }
    for (int y = 0; y < output.height(); ++y)
        for (int x = 0; x < output.width(); ++x) {
            if (markBounds.adjusted(-1, -1, 1, 1).contains(x, y))
                continue;
            const auto actual = output.pixelColor(x, y), expected = plain.pixelColor(x, y);
            require(std::abs(actual.red() - expected.red()) <= 1 &&
                        std::abs(actual.green() - expected.green()) <= 1 &&
                        std::abs(actual.blue() - expected.blue()) <= 1 &&
                        actual.alpha() == expected.alpha(),
                    "Watermark queue altered source pixels outside its defined destination region");
        }
}
void noTemporaryFiles(Context &c) {
    for (const auto &directory : {c.root + "/exports", c.stateRoot + "/profile-a-export",
                                  c.stateRoot + "/last-settings-export"}) {
        QDirIterator iterator(directory, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const auto name = QFileInfo(iterator.next()).fileName();
            require(!name.startsWith(".luma-") && !name.endsWith(".tmp"),
                    "Settings export leaked a temporary output");
        }
    }
}
void done(Context &c, int count) {
    c.driver->wait([&] { return !c.exporter.working(); }, "settings export queue completed", 60000);
    require(c.exporter.total() == count && c.exporter.completed() == count &&
                c.exporter.succeeded() == count && c.exporter.skipped() == 0 &&
                c.exporter.failures().isEmpty() && c.exporter.error().isEmpty(),
            "Settings export did not complete its exact real output set");
    noTemporaryFiles(c);
}
void pngBase(Context &c, const QString &destination, const QString &name) {
    require(QDir().mkpath(destination), "Cannot create own export-settings destination");
    text(c, "export.destination", destination);
    text(c, "exportNaming", name);
    choose(c, "export.conflict", 0);
    choose(c, "exportFormat", 1);
    choose(c, "export.colorSpace", 0);
    choose(c, "export.resize", 0);
    choose(c, "export.outputMedium", 0);
    choose(c, "export.metadata", 0);
    choose(c, "export.watermarkKind", 0);
}
bool firstCompletion(Context &c, const QString &firstName) {
    // Both basic and AdvancedImageExporter emit final progress1 only after guarded
    // install. One worker posts progress then completion in FIFO; no later item ran.
    return c.exporter.working() && !c.exporter.waitingForSave() && !c.exporter.paused() &&
           c.exporter.total() == 2 && c.exporter.completed() == 0 && c.exporter.progress() == 1 &&
           c.exporter.currentFile() == firstName && !c.editor.editingLocked();
}
void watermarkImmutable(Context &c) {
    auto &d = *c.driver;
    const auto input = c.root + "/owned-inputs";
    require(QDir().mkpath(input), "Cannot prepare owned watermark inputs");
    const QStringList paths{input + "/Owned-A.png", input + "/Owned-B.png"};
    ownedPNG(paths[0], 0);
    ownedPNG(paths[1], 1);
    const auto firstHash = digest(paths[0]), secondHash = digest(paths[1]);
    importPrivate(c, paths);
    require(sourceAt(c, 0) == paths[0] && sourceAt(c, 1) == paths[1],
            "Watermark queue source ordering is not A then B");
    d.click("library.selectAll");
    d.wait([&] { return !c.library.selecting() && c.library.selectedCount() == 2; },
           "two watermark inputs selected");
    const auto selected = c.library.selectionIDs();
    const auto firstPlain = plainReference(paths[0], {}),
               secondPlain = plainReference(paths[1], {});
    const auto watermark = c.root + "/own-watermark.png";
    for (const bool remove : {false, true}) {
        mark(watermark, Qt::red);
        const auto redHash = digest(watermark);
        const auto directory =
            c.root + (remove ? "/exports/deleted-original" : "/exports/changed-original");
        open(c);
        pngBase(c, directory, "{filename}");
        choose(c, "export.watermarkKind", 2);
        text(c, "export.watermarkImage", watermark);
        text(c, "export.watermarkOpacity", "100", true);
        text(c, "export.watermarkSize", "20", true);
        text(c, "export.watermarkMargin", "3", true);
        choose(c, "export.watermarkAnchor", 8);
        const auto firstOutput = directory + "/Owned-A.png",
                   secondOutput = directory + "/Owned-B.png";
        const auto form =
            QJsonObject::fromVariantMap(d.object("exportPanel")->property("form").toMap());
        require(form["watermarkKind"].toInt() == 2 && form["watermarkOpacity"].toDouble() == 100 &&
                    form["watermarkSize"].toDouble() == 20 &&
                    form["watermarkMargin"].toDouble() == 3 &&
                    form["watermarkAnchor"].toInt() == 8 && !QFileInfo::exists(firstOutput) &&
                    !QFileInfo::exists(secondOutput),
                "Actual UI watermark settings did not reach the intended immutable-queue fixture");
        QueuedCallGate gate(
            c.exporter, [&] { return firstCompletion(c, "Owned-A.png"); },
            [&] {
                require(settings(c) == form && !QFileInfo::exists(secondOutput),
                        "Watermark mutation boundary is not after committed A and before queued B "
                        "starts");
                redWatermark(firstOutput, firstPlain);
                const auto committed = digest(firstOutput);
                open(c); // Legal settings panel opening; the form itself is disabled while working.
                reveal(c, "export.watermarkKind");
                auto *kind = d.item("export.watermarkKind");
                require(!kind->isEnabled(),
                        "Working export unexpectedly exposes mutable watermark settings");
                QSignalSpy activated(kind, SIGNAL(activated(int)));
                require(activated.isValid(),
                        "Disabled watermark control lacks an activation positive-control signal");
                d.press("export.watermarkKind");
                d.release("export.watermarkKind");
                require(
                    activated.isEmpty() && settings(c) == form &&
                        QJsonObject::fromVariantMap(
                            d.object("exportPanel")->property("form").toMap()) == form,
                    "Disabled real UI events mutated an already-enqueued watermark configuration");
                close(c);
                require(
                    firstCompletion(c, "Owned-A.png") && !QFileInfo::exists(secondOutput),
                    "UI negative-input check escaped the established first completion boundary");
                if (remove)
                    require(QFile::remove(watermark) && !QFileInfo::exists(watermark),
                            "Cannot delete only the owned original watermark");
                else {
                    mark(watermark, Qt::green);
                    require(digest(watermark) != redHash &&
                                QImage(watermark).pixelColor(16, 16).green() > 250,
                            "Owned watermark rewrite did not establish a distinct green input");
                }
                require(digest(firstOutput) == committed,
                        "Changing original watermark altered an already committed A output");
                d.log({{"action", "watermark_original_mutated_after_plan_before_B"},
                       {"operation", remove ? "delete" : "rewrite_green"},
                       {"completed_notifications_held", 1},
                       {"B_output_absent", true},
                       {"original_watermark", watermark}});
            });
        d.click("export.selection");
        d.wait([&] { return gate.caught; }, "first actual watermark output completion held");
        if (gate.failure)
            std::rethrow_exception(gate.failure);
        done(c, 2);
        redWatermark(firstOutput, firstPlain);
        redWatermark(secondOutput, secondPlain);
        require(QDir(directory).entryList({"*.png"}, QDir::Files, QDir::Name) ==
                        QStringList{"Owned-A.png", "Owned-B.png"} &&
                    settings(c) == form && c.library.selectionIDs() == selected &&
                    digest(paths[0]) == firstHash && digest(paths[1]) == secondHash &&
                    luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 2,
                "Watermark snapshot completion mutated live settings, source inputs or selection");
        for (const auto &id : selected) {
            const auto record = luma::Catalog(c.catalogFile.toStdString()).assetByID(id);
            require(record && record->edits == luma::EditDocument{},
                    "Immutable watermark queue changed a source's complete saved edit document");
        }
    }
}
// A tag left in either primary ImageIO representation is still present. In
// particular, an empty XMP value must not hide a nonempty raw properties value.
void absentTag(const QJsonObject &data, const QString &key) {
    require(!data["properties"].toObject().contains(key) &&
                !data["metadata"].toObject().contains(key) && data[key].toString().isEmpty(),
            "Forbidden privacy tag remains in a primary encoded representation: " + key);
}
void preservedTag(const QJsonObject &data, const QString &key,
                  const std::function<bool(const QString &, const QString &)> &matches) {
    bool present = false;
    for (const auto *channel : {"properties", "metadata"}) {
        const auto values = data[channel].toObject();
        if (!values.contains(key))
            continue;
        present = true;
        require(matches(QString::fromLatin1(channel), values[key].toString()),
                "Preserved privacy tag changed in a primary encoded representation: " + key);
    }
    require(present, "Expected privacy tag is absent from both primary representations: " + key);
}
void privacy(Context &c) {
    auto &d = *c.driver;
    const auto source = c.root + "/private-metadata-source.jpg";
    createPrivacySource(source);
    const auto originalHash = digest(source);
    const auto actualSource = encodedMetadata(source);
    const QMap<QString, QString> secrets{{"city", "PrivateFixtureCity"},
                                         {"state", "PrivateFixtureState"},
                                         {"country", "PrivateFixtureCountry"},
                                         {"street", "PrivateFixtureStreet"}};
    auto attribution = [](const QJsonObject &data, bool expected) {
        for (const auto &[key, value] :
             QList<QPair<QString, QString>>{{"artist", "Luma Privacy Artist"},
                                            {"copyright", "Luma Privacy Copyright"},
                                            {"iptcCopyright", "Luma Privacy Copyright"}}) {
            if (expected)
                preservedTag(data, key, [&](const QString &, const QString &actual) {
                    return actual.contains(value);
                });
            else
                absentTag(data, key);
        }
    };
    auto captureValue = [](const QString &value) {
        return (value.contains("2026:10:17") || value.contains("2026-10-17")) &&
               value.contains("10:20:30");
    };
    auto gps = [&](const QJsonObject &data) {
        for (const auto &[key, value] :
             QList<QPair<QString, double>>{{"latitude", 48.5}, {"longitude", 2.3}})
            preservedTag(data, key, [&](const QString &channel, const QString &actual) {
                if (channel == "properties") {
                    bool valid = false;
                    const auto coordinate = actual.toDouble(&valid);
                    return valid && std::abs(coordinate - value) < 1e-8;
                }
                // ImageIO's primary metadata representation may use XMP DMS text.
                // Compare it with the source's own independently read canonical tag;
                // the source's numeric coordinates are positively checked below.
                const auto reference = actualSource["metadata"].toObject()[key].toString();
                return !reference.isEmpty() && actual == reference;
            });
        for (const auto &[key, value] :
             QList<QPair<QString, QString>>{{"latitudeRef", "N"}, {"longitudeRef", "E"}})
            for (const auto *channel : {"properties", "metadata"}) {
                const auto values = data[channel].toObject();
                if (values.contains(key))
                    require(values[key].toString() == value, "Encoded GPS hemisphere changed");
            }
    };
    attribution(actualSource, true);
    const auto rawSource = actualSource["properties"].toObject();
    bool latitudeValid = false, longitudeValid = false;
    const auto latitude = rawSource["latitude"].toString().toDouble(&latitudeValid);
    const auto longitude = rawSource["longitude"].toString().toDouble(&longitudeValid);
    require(actualSource["width"].toInt() == 128 && actualSource["height"].toInt() == 96 &&
                latitudeValid && longitudeValid && std::abs(latitude - 48.5) < 1e-8 &&
                std::abs(longitude - 2.3) < 1e-8 && rawSource["latitudeRef"].toString() == "N" &&
                rawSource["longitudeRef"].toString() == "E",
            "Primary ImageIO input lost its literal known GPS coordinate/hemisphere fixture");
    gps(actualSource);
    preservedTag(actualSource, "capture",
                 [&](const QString &, const QString &value) { return captureValue(value); });
    preservedTag(actualSource, "lens", [](const QString &, const QString &value) {
        return value == "Luma Privacy Lens";
    });
    for (auto it = secrets.begin(); it != secrets.end(); ++it)
        preservedTag(actualSource, it.key(),
                     [&](const QString &, const QString &value) { return value == it.value(); });
    writeJson(c.root + "/source-primary-metadata.json", actualSource);
    importPrivate(c, {source});
    d.doubleClick(c.card(0));
    c.loaded();
    const auto asset = c.editor.photoID();
    for (const int policy : {1, 3, 2, 0}) {
        const auto directory = c.root + QString("/exports/privacy-%1").arg(policy);
        open(c);
        pngBase(c, directory, "Privacy");
        choose(c, "export.metadata", policy);
        d.click("export.current");
        done(c, 1);
        const auto result = encodedMetadata(directory + "/Privacy.png");
        const bool all = policy == 1, removeLocation = policy == 3;
        attribution(result, policy != 0);
        require(result["width"].toInt() == 128 && result["height"].toInt() == 96,
                "Privacy toggles altered actual image dimensions");
        if (all)
            gps(result);
        else {
            for (const auto *key : {"latitude", "latitudeRef", "longitude", "longitudeRef"})
                absentTag(result, QString::fromLatin1(key));
            require(!result["xmp"].toString().contains("GPSLatitude", Qt::CaseInsensitive) &&
                        !result["xmp"].toString().contains("GPSLongitude", Qt::CaseInsensitive),
                    "Forbidden GPS tags remain in the actual serialized XMP");
        }
        for (auto it = secrets.begin(); it != secrets.end(); ++it) {
            if (all)
                preservedTag(result, it.key(), [&](const QString &, const QString &value) {
                    return value == it.value();
                });
            else {
                absentTag(result, it.key());
                require(!result["xmp"].toString().contains(it.value()),
                        "Forbidden known IPTC location remains in actual serialized XMP");
            }
        }
        if (all || removeLocation) {
            preservedTag(result, "capture", [&](const QString &, const QString &value) {
                return captureValue(value);
            });
            preservedTag(result, "lens", [](const QString &, const QString &value) {
                return value == "Luma Privacy Lens";
            });
        } else {
            absentTag(result, "capture");
            absentTag(result, "lens");
            require(!result["xmp"].toString().contains("Luma Privacy Lens") &&
                        !result["xmp"].toString().contains("2026:10:17") &&
                        !result["xmp"].toString().contains("2026-10-17"),
                    "Forbidden capture/lens value remains in actual serialized XMP");
        }
        if (policy == 0)
            require(!result["xmp"].toString().contains("Luma Privacy Artist") &&
                        !result["xmp"].toString().contains("Luma Privacy Copyright"),
                    "Do-not-copy policy retained known attribution in serialized XMP");
        const auto durable =
            luma::Catalog(c.catalogFile.toStdString()).assetByID(asset.toStdString());
        require(digest(source) == originalHash && c.editor.sourcePath() == source &&
                    c.editor.photoID() == asset &&
                    c.editor.currentSettings() == luma::EditDocument{} && durable &&
                    durable->edits == luma::EditDocument{},
                "UI metadata privacy changes altered original bytes, source identity or edits");
        writeJson(c.root + QString("/encoded-policy-%1.json").arg(policy), result);
    }
}
void profileA(Context &c, const QString &destination) {
    text(c, "export.destination", destination);
    text(c, "exportNaming", "Profile-{sequence}");
    choose(c, "export.conflict", 0);
    choose(c, "exportFormat", 3);
    choose(c, "export.compression", 2);
    choose(c, "export.colorSpace", 1);
    choose(c, "export.resize", 3);
    text(c, "export.resizeValue", "160");
    choose(c, "export.outputMedium", 1);
    choose(c, "export.outputAmount", 2);
    choose(c, "export.metadata", 3);
    choose(c, "export.watermarkKind", 0);
}
void persisted(Context &c, const QJsonObject &last, const QString &id, const QJsonObject &profile) {
    const auto file =
        QJsonDocument::fromJson(bytes(c.stateRoot + "/export-settings.json")).object();
    const auto profiles = file["profiles"].toObject();
    require(file.size() == 3 && file["schema"].toInt() == 1 && file["last"].toObject() == last &&
                profiles.size() == 1 && profiles.contains(id) && profiles[id].toObject() == profile,
            "Exact last settings/named profile did not persist with its complete schema and UUID");
}
} // namespace

bool exportSettingsScenario(Context &c, const QString &id) {
    if (id == "UX-EXP-017") {
        watermarkImmutable(c);
        return true;
    }
    if (id == "UX-EXP-027") {
        privacy(c);
        return true;
    }
    return false;
}

bool exportSettingsRestartScenario(Context &c, const QString &id, const QString &stage) {
    if (id != "UX-EXP-025")
        return false;
    auto &d = *c.driver;
    const auto source = c.stateRoot + "/reusable-source.png";
    const auto destinationA = c.stateRoot + "/profile-a-export";
    const auto destinationB = c.stateRoot + "/last-settings-export";
    if (stage == "prepare") {
        ownedPNG(source, 0);
        importPrivate(c, {source});
        d.doubleClick(c.card(0));
        c.loaded();
        require(QDir().mkpath(destinationA) && QDir().mkpath(destinationB),
                "Cannot create reusable-profile output directories");
        // Literal complete public settings contract, independent of settings()/json()
        // defaults. Missing or unexpectedly altered unused fields must not become
        // their own expected values during prepare.
        const QJsonObject expectedA{{"destination", destinationA},
                                    {"naming", "Profile-{sequence}"},
                                    {"conflict", 0},
                                    {"format", 3},
                                    {"quality", 92},
                                    {"compression", 2},
                                    {"colorSpace", 1},
                                    {"resize", 3},
                                    {"resizeValue", 160},
                                    {"outputMedium", 1},
                                    {"outputAmount", 2},
                                    {"metadata", 3},
                                    {"watermarkKind", 0},
                                    {"watermarkText", ""},
                                    {"watermarkImage", ""},
                                    {"watermarkOpacity", 70},
                                    {"watermarkSize", 15},
                                    {"watermarkMargin", 3},
                                    {"watermarkAnchor", 8}};
        open(c);
        profileA(c, destinationA);
        text(c, "export.profileName", "Reusable QA");
        reveal(c, "export.profileSave");
        d.click("export.profileSave");
        require(c.exporter.error().isEmpty() && settings(c) == expectedA &&
                    c.exporter.profiles().size() == 1,
                "Actual Save profile did not commit complete intended reusable settings");
        const auto entry = c.exporter.profiles().front().toMap();
        const auto uuid = entry["id"].toString();
        const QJsonObject profile{{"name", "Reusable QA"}, {"settings", expectedA}};
        require(!QUuid(uuid).isNull() && entry["name"].toString() == "Reusable QA",
                "Reusable profile lacks its exact name/UUID");
        persisted(c, expectedA, uuid, profile);
        auto expectedB = expectedA;
        expectedB["destination"] = destinationB;
        expectedB["naming"] = "Recent-{sequence}";
        expectedB["format"] = 1;
        expectedB["compression"] = 0;
        expectedB["colorSpace"] = 0;
        expectedB["resize"] = 1;
        expectedB["resizeValue"] = 64;
        expectedB["outputMedium"] = 0;
        expectedB["metadata"] = 0;
        pngBase(c, destinationB, "Recent-{sequence}");
        choose(c, "export.resize", 1);
        text(c, "export.resizeValue", "64");
        d.click("export.current");
        done(c, 1);
        require(settings(c) == expectedB &&
                    QImage(destinationB + "/Recent-0001.png").size() == QSize(64, 48),
                "Independent last-settings positive control did not export its known 64x48 result");
        persisted(c, expectedB, uuid, profile);
        const auto afterLast =
            luma::Catalog(c.catalogFile.toStdString()).assetByID(c.editor.photoID().toStdString());
        require(afterLast && afterLast->edits == luma::EditDocument{} &&
                    c.editor.currentSettings() == luma::EditDocument{} &&
                    c.editor.sourcePath() == source,
                "Preparing last-used export changed the source's complete durable document");
        writeJson(c.stateRoot + "/reusable-settings-oracle.json",
                  {{"source", source},
                   {"source_sha256", QString::fromLatin1(digest(source).toHex())},
                   {"asset", c.editor.photoID()},
                   {"profile_id", uuid},
                   {"profile", profile},
                   {"last", expectedB},
                   {"saved", expectedA}});
        d.quitShortcut();
        d.wait(
            [&] {
                return !c.window->isVisible() && !c.editor.busy() && !c.editor.saving() &&
                       !c.exporter.working();
            },
            "reusable-settings prepare process closed cleanly");
        return true;
    }
    require(stage == "verify", "Reusable settings require a real prepare/verify process restart");
    const auto oracle =
        QJsonDocument::fromJson(bytes(c.stateRoot + "/reusable-settings-oracle.json")).object();
    const auto expectedA = oracle["saved"].toObject(), expectedB = oracle["last"].toObject();
    const auto uuid = oracle["profile_id"].toString();
    require(
        settings(c) == expectedB && c.exporter.error().isEmpty() &&
            c.exporter.profiles().size() == 1 &&
            c.exporter.profiles().front().toMap()["id"].toString() == uuid,
        "New process did not restore distinct last-used settings and the named reusable profile");
    persisted(c, expectedB, uuid, oracle["profile"].toObject());
    c.editor.restore(); // Same explicit desktop startup boundary as existing restart adapters.
    c.loaded();
    require(c.editor.sourcePath() == source && c.editor.photoID() == oracle["asset"].toString() &&
                c.editor.currentSettings() == luma::EditDocument{},
            "Settings restart changed active source/full document");
    open(c);
    require(QJsonObject::fromVariantMap(d.object("exportPanel")->property("form").toMap()) ==
                expectedB,
            "Actual reopened form did not initialize from persisted last settings");
    choose(c, "export.profiles", 0);
    reveal(c, "export.profileLoad");
    d.click("export.profileLoad");
    require(settings(c) == expectedA &&
                QJsonObject::fromVariantMap(d.object("exportPanel")->property("form").toMap()) ==
                    expectedA,
            "Actual Load profile did not replace the full form/current persisted settings");
    persisted(c, expectedA, uuid, oracle["profile"].toObject());
    d.click("export.current");
    done(c, 1);
    const auto output = encodedMetadata(destinationA + "/Profile-0001.tif");
    require(output["width"].toInt() == 160 && output["height"].toInt() == 120 &&
                settings(c) == expectedA &&
                QString::fromLatin1(digest(source).toHex()) == oracle["source_sha256"].toString() &&
                c.editor.currentSettings() == luma::EditDocument{},
            "Restarted reusable profile did not produce its independently known 160x120 encoded "
            "result");
    const auto durable = luma::Catalog(c.catalogFile.toStdString())
                             .assetByID(oracle["asset"].toString().toStdString());
    require(durable && durable->edits == luma::EditDocument{} &&
                luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 1 &&
                c.editor.photoID() == oracle["asset"].toString() && c.editor.sourcePath() == source,
            "Restarted profile export changed durable document or source identity");
    require(!d.object("exportPanel")->property("visible").toBool(),
            "Successful profile export unexpectedly retained its settings popup");
    return true;
}
} // namespace ux
