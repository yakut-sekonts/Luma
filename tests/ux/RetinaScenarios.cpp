#include "tests/ux/NativeRetinaGeometry.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QColor>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QMouseEvent>
#include <QSGRendererInterface>
#include <QScreen>
#include <QSet>
#include <QTest>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

// Acceptance input is Qt QTest window input on the actual Cocoa/Retina
// surface, not hardware input or physical scanout.
namespace ux {
namespace {
using Document = luma::EditDocument;
struct ReleasePointer {
    Context &context;
    QPoint last;
    bool down = false;
    ~ReleasePointer() {
        if (!down)
            return;
        try {
            QTest::mouseRelease(context.window, Qt::LeftButton, Qt::NoModifier, last);
            context.driver->log(
                {{"action", "retina_failure_pointer_cleanup"}, {"acceptance_proof", false}});
        } catch (...) {
            std::fputs("Retina pointer cleanup failed; original failure remains active.\n", stderr);
        }
    }
};
QByteArray hash(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read own Retina source");
    QCryptographicHash value(QCryptographicHash::Sha256);
    require(value.addData(&file), "Cannot hash complete own Retina source");
    return value.result();
}
QJsonObject point(const QPointF &p) {
    return {{"x", p.x()}, {"y", p.y()}};
}
QJsonObject box(const QRectF &r) {
    return {{"x", r.x()}, {"y", r.y()}, {"width", r.width()}, {"height", r.height()}};
}
bool close(double a, double b) {
    return std::abs(a - b) < 1e-6;
}
void exact(Context &c, const Document &expected, const QString &reason) {
    require(c.editor.currentSettings() == expected && c.editor.error().isEmpty(), reason);
}
void pixels(Context &c, const Document &expected) {
    const auto image = fixtureReference(c, expected, c.editor.sourcePath());
    c.driver->wait(
        [&] {
            const auto *preview = c.driver->item("develop.preview");
            return !c.editor.busy() && preview->isVisible() &&
                   preview->property("status").toInt() == 1 &&
                   preview->property("source").toUrl().toString() == c.editor.previewUrl() &&
                   c.provider->requestImage({}, nullptr, {}) == image;
        },
        "Retina full-document CPU reference/current-source/Ready pixels");
    exact(c, expected, "Retina render changed full edit state");
    c.driver->frame();
}
void savedNaturally(Context &c, const QString &asset, const Document &expected) {
    c.driver->wait(
        [&] {
            const auto durable = luma::Catalog(c.catalogFile.toStdString()).activeAsset();
            return !c.editor.busy() && !c.editor.saving() && durable &&
                   durable->id == asset.toStdString() && durable->edits == expected &&
                   c.editor.error().isEmpty();
        },
        "Retina real gestures naturally committed the full durable document");
}
QPointF normalizedDispatched(Context &c, const QString &id, const QPointF &fraction) {
    auto *item = c.driver->item(id);
    const auto p = item->mapFromScene(c.driver->point(id, fraction));
    return {p.x() / item->width(), p.y() / item->height()};
}
QPointF nativePoint(Context &c, const QPointF &logical) {
    const auto mapped = nativeRetinaPoint(*c.window, logical);
    c.driver->log({{"action", "native_retina_coordinate_probe"},
                   {"logical", point(logical)},
                   {"appkit_backing", point(mapped.backingFromTopLeft)},
                   {"appkit_roundtrip", point(mapped.roundTripLogical)}});
    require(close(mapped.roundTripLogical.x(), logical.x()) &&
                close(mapped.roundTripLogical.y(), logical.y()) &&
                close(mapped.backingFromTopLeft.x(), logical.x() * 2) &&
                close(mapped.backingFromTopLeft.y(), logical.y() * 2),
            "Independent AppKit logical/backing roundtrip is not actual Retina2x mapping");
    return mapped.backingFromTopLeft;
}
NativeRetinaGeometry nativeState(Context &c) {
    require(QGuiApplication::platformName() == "cocoa" && c.window->isExposed() &&
                c.window->isActive() && c.window->screen(),
            "Retina qualification requires the active actual Cocoa window, never offscreen");
    require(c.window->rendererInterface() &&
                c.window->rendererInterface()->graphicsApi() == QSGRendererInterface::Metal,
            "This native Retina fixture requires the actual Metal scene backend; "
            "software/offscreen is unqualified");
    for (const auto *variable :
         {"QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS", "QT_DEVICE_PIXEL_RATIO"})
        require(!qEnvironmentVariableIsSet(variable),
                "Forced Qt scaling cannot qualify physical Retina2x: " +
                    QString::fromLatin1(variable));
    const auto state = nativeRetinaGeometry(*c.window);
    require(close(state.backingScale, 2) && close(c.window->devicePixelRatio(), 2) &&
                close(c.window->screen()->devicePixelRatio(), 2) &&
                close(state.viewLogicalSize.width(), c.window->width()) &&
                close(state.viewLogicalSize.height(), c.window->height()) &&
                close(state.viewBackingBounds.width(), c.window->width() * 2) &&
                close(state.viewBackingBounds.height(), c.window->height() * 2),
            "NSView/NSWindow/QtWindow/QtScreen do not agree on native Retina2x dimensions");
    // Fractional probes verify public Cocoa conversion independently of Driver's
    // integer logical input grid. No sublogical hardware input is claimed.
    for (const QPointF sample : {QPointF(.25, .75), QPointF(17.5, 21.25),
                                 QPointF(c.window->width() - .5, c.window->height() - .25)})
        nativePoint(c, sample);
    return state;
}
QImage scene(Context &c, const QString &name) {
    const auto before = nativeState(c);
    c.driver->frame();
    auto result = c.window->grabWindow().convertToFormat(QImage::Format_RGBA8888);
    const auto after = nativeState(c);
    require(!result.isNull() && before.windowNumber == after.windowNumber &&
                before.displayID == after.displayID && result.width() == c.window->width() * 2 &&
                result.height() == c.window->height() * 2,
            "Actual scene readback is not the live Retina backing surface at2physicalpx per "
            "logical point");
    require(result.save(c.root + '/' + name + ".png"), "Cannot save actual Retina scene evidence");
    c.driver->log({{"action", "retina_scene_physical_dimensions"},
                   {"name", name},
                   {"qt_window_logical_width", c.window->width()},
                   {"qt_window_logical_height", c.window->height()},
                   {"frame_pixel_width", result.width()},
                   {"frame_pixel_height", result.height()},
                   {"frame_device_pixel_ratio", result.devicePixelRatio()},
                   {"native_backing_scale", after.backingScale},
                   {"native_view_flipped", after.viewFlipped},
                   {"native_window_number", after.windowNumber},
                   {"native_display_id", static_cast<qint64>(after.displayID)},
                   {"native_backing_bounds", box(after.viewBackingBounds)}});
    return result;
}
class MouseTrace final : public QObject {
  public:
    explicit MouseTrace(Context &context) : c(context) {
        c.window->installEventFilter(this);
    }
    ~MouseTrace() override {
        c.window->removeEventFilter(this);
    }
    void verify(const QString &name, const QPointF &pressed, const QPointF &released) {
        require(presses == 1 && releases == 1 && heldMoves >= 1 && !invalidSequence &&
                    !observed.isEmpty(),
                "Actual QtWindow did not receive the paired real Retina pointer gesture");
        require(firstPress == pressed && lastRelease == released,
                "Window pointer coordinates differ from the actual Driver logical input pixels");
        QJsonArray backed;
        for (const auto &entry : observed) {
            auto record = entry.toObject();
            const auto logical = record["logical"].toObject();
            record["appkit_backing_from_top_left"] =
                point(nativePoint(c, {logical["x"].toDouble(), logical["y"].toDouble()}));
            backed.append(record);
        }
        c.driver->log({{"action", "retina_window_pointer_trace"},
                       {"name", name},
                       {"boundary", "QT_QTEST_WINDOW_INPUT"},
                       {"hardware_event_injection", false},
                       {"sublogical_hardware_precision_exercised", false},
                       {"held_left_button_moves", heldMoves},
                       {"events", backed}});
    }

  private:
    Context &c;
    int presses = 0, releases = 0, heldMoves = 0;
    bool invalidSequence = false;
    QPointF firstPress, lastRelease;
    QJsonArray observed;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == c.window &&
            (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
             event->type() == QEvent::MouseButtonRelease)) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (event->type() == QEvent::MouseButtonPress && mouse->button() == Qt::LeftButton) {
                invalidSequence = invalidSequence || presses != 0 || releases != 0 ||
                                  !mouse->buttons().testFlag(Qt::LeftButton);
                ++presses;
                firstPress = mouse->position();
            }
            if (presses == 1 && releases == 0 && event->type() == QEvent::MouseMove &&
                mouse->buttons().testFlag(Qt::LeftButton))
                ++heldMoves;
            if (event->type() == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) {
                invalidSequence = invalidSequence || presses != 1 || releases != 0 ||
                                  mouse->buttons().testFlag(Qt::LeftButton);
                ++releases;
                lastRelease = mouse->position();
            }
            if (presses) {
                const auto logical = mouse->position();
                // Observer never consumes/mutates an event. Avoid throwing an
                // assertion through Qt event dispatch; conversion is verified later.
                observed.append(QJsonObject{{"event_type", static_cast<int>(event->type())},
                                            {"logical", point(logical)},
                                            {"buttons", static_cast<int>(mouse->buttons())}});
            }
        }
        return false;
    }
};
void drag(Context &c, const QString &name, const QString &selector, const QPointF &from,
          const QPointF &to) {
    const auto first = c.driver->point(selector, from), last = c.driver->point(selector, to);
    const auto firstNative = nativePoint(c, first), lastNative = nativePoint(c, last);
    MouseTrace trace(c);
    ReleasePointer pointer{c, first};
    c.driver->press(selector, from);
    pointer.down = true;
    for (int step = 1; step <= 12; ++step) {
        const auto fraction = from + (to - from) * (static_cast<double>(step) / 12);
        pointer.last = c.driver->point(selector, fraction);
        c.driver->move(selector, fraction);
    }
    c.driver->release(selector, to);
    pointer.down = false;
    trace.verify(name, first, last);
    c.driver->log({{"action", "retina_pointer_logical_to_native"},
                   {"name", name},
                   {"first_logical", point(first)},
                   {"last_logical", point(last)},
                   {"first_appkit_backing", point(firstNative)},
                   {"last_appkit_backing", point(lastNative)}});
}
void createSource(const QString &path) {
    require(!QFileInfo::exists(path), "Retina tile input must be a new owned file");
    QImage source(256, 192, QImage::Format_RGBA8888);
    source.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const std::array<QColor, 12> colors{
        QColor(25, 30, 110),  QColor(130, 25, 35), QColor(20, 125, 45), QColor(115, 25, 120),
        QColor(20, 110, 115), QColor(110, 80, 20), QColor(60, 30, 125), QColor(125, 60, 30),
        QColor(25, 70, 120),  QColor(100, 25, 65), QColor(60, 110, 25), QColor(115, 110, 120)};
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(
                x, y,
                colors[(static_cast<std::size_t>(y) / 64) * 4 + static_cast<std::size_t>(x) / 64]);
    require(source.save(path, "PNG"), "Cannot encode own Retina tile source");
}
void importOwn(Context &c, const QString &source) {
    // Existing declared chooser-result fixture; the real Import button does work.
    c.driver->object("library.importDialog")
        ->setProperty("sources", QVariant::fromValue(QList<QUrl>{QUrl::fromLocalFile(source)}));
    c.driver->log({{"action", "fixture_file_chooser_result"}, {"source", source}});
    c.driver->click("library.importButton");
    c.driver->wait(
        [&] { return c.driver->object("library.importDialog")->property("opened").toBool(); },
        "Own Retina import dialog opened");
    c.driver->choose("library.importMode", 0);
    if (c.driver->object("library.importDuplicates")->property("checked").toBool())
        c.driver->click("library.importDuplicates");
    c.driver->click("library.importStart");
    c.driver->wait(
        [&] { return !c.library.working() && !c.library.loading() && c.library.count() == 1; },
        "Own Retina input committed");
    require(c.library.error().isEmpty(), c.library.error());
    c.driver->wait(
        [&] { return !c.driver->object("library.importDialog")->property("visible").toBool(); },
        "Own Retina Import modal exit completed before card input");
    c.driver->doubleClick(c.card(0));
    c.loaded();
    c.driver->wait(
        [&] {
            return !c.library.loading() && c.library.count() == 1 &&
                   c.library.data(c.library.index(0), LibraryModel::AssetID).toString() ==
                       c.editor.photoID() &&
                   c.library.data(c.library.index(0), LibraryModel::Path).toUrl().toLocalFile() ==
                       source;
        },
        "Retina post-photoOpened UUID/path metadata and filmstrip layout ready");
    c.driver->frame();
    require(!c.window->property("libraryMode").toBool() && c.editor.sourcePath() == source,
            "Real Retina input did not open Develop");
}
QRectF paintedBounds(Context &c, const QImage &reference) {
    auto *image = c.driver->item("develop.preview");
    const double scale =
        std::min(image->width() / reference.width(), image->height() / reference.height());
    const QSizeF expected(reference.width() * scale, reference.height() * scale);
    require(close(image->property("paintedWidth").toDouble(), expected.width()) &&
                close(image->property("paintedHeight").toDouble(), expected.height()),
            "Actual QML image fit bounds differ from independently known output aspect/size");
    return {image->mapToScene({(image->width() - expected.width()) / 2,
                               (image->height() - expected.height()) / 2}),
            expected};
}
int channelDistance(const QColor &a, const QColor &b) {
    return std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                     std::abs(a.blue() - b.blue()), std::abs(a.alpha() - b.alpha())});
}
void scenePixels(Context &c, const Document &document, const QString &name) {
    pixels(c, document);
    const auto reference = fixtureReference(c, document, c.editor.sourcePath());
    const auto bounds = paintedBounds(c, reference);
    const auto first = nativePoint(c, bounds.topLeft()),
               last = nativePoint(c, bounds.bottomRight());
    const auto frame = scene(c, name);
    const QRectF nativeBounds(first, last);
    require(frame.rect().contains(nativeBounds.toAlignedRect()),
            "Actual fitted Retina image escaped physical framebuffer");
    QJsonArray samples;
    QSet<QRgb> colors;
    for (int row = 0; row < 7; ++row)
        for (int column = 0; column < 9; ++column) {
            const auto candidate =
                nativePoint(c, {bounds.left() + bounds.width() * (column + .5) / 9,
                                bounds.top() + bounds.height() * (row + .5) / 7});
            const QPoint sample(static_cast<int>(std::floor(candidate.x())),
                                static_cast<int>(std::floor(candidate.y())));
            require(frame.rect().contains(sample),
                    "A native scene sample lies outside the readback");
            const double u = (sample.x() + .5 - nativeBounds.left()) / nativeBounds.width();
            const double v = (sample.y() + .5 - nativeBounds.top()) / nativeBounds.height();
            const int sx = static_cast<int>(std::floor(u * reference.width())),
                      sy = static_cast<int>(std::floor(v * reference.height()));
            if (sx < 3 || sy < 3 || sx >= reference.width() - 3 || sy >= reference.height() - 3)
                continue;
            const auto expected = reference.pixelColor(sx, sy);
            bool flat = true;
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx)
                    flat = flat && reference.pixelColor(sx + dx, sy + dy) == expected;
            if (!flat)
                continue; // Explicit anti-interpolation guard, never skip a mismatching actual
                          // pixel.
            const auto actual = frame.pixelColor(sample);
            require(channelDistance(actual, expected) <= 1,
                    "Native Retina scene pixels do not match their independently mapped flat "
                    "source region");
            colors.insert(expected.rgba());
            samples.append(QJsonObject{{"native_pixel_x", sample.x()},
                                       {"native_pixel_y", sample.y()},
                                       {"source_pixel_x", sx},
                                       {"source_pixel_y", sy},
                                       {"actual_rgba", static_cast<qint64>(actual.rgba())},
                                       {"expected_rgba", static_cast<qint64>(expected.rgba())}});
        }
    require(samples.size() >= 20 && colors.size() >= 6,
            "Retina scene oracle did not sample enough distinct physical image regions");
    // Positive letterbox controls establish the actual fitted boundary in scene
    // pixels, not only the QML paintedWidth/Height properties and interior colors.
    auto *image = c.driver->item("develop.preview");
    const auto itemBounds = image->mapRectToScene(image->boundingRect());
    const auto background =
        c.driver->object("develop.canvasPane")->property("color").value<QColor>();
    require(background.isValid(), "Retina image canvas has no declared background color");
    QJsonArray backgroundSamples;
    for (const QPointF candidate : {QPointF(bounds.left() - 3, bounds.center().y()),
                                    QPointF(bounds.right() + 3, bounds.center().y()),
                                    QPointF(bounds.center().x(), bounds.top() - 3),
                                    QPointF(bounds.center().x(), bounds.bottom() + 3)}) {
        if (!itemBounds.adjusted(1, 1, -1, -1).contains(candidate))
            continue;
        const auto native = nativePoint(c, candidate);
        const QPoint sample(static_cast<int>(std::floor(native.x())),
                            static_cast<int>(std::floor(native.y())));
        require(frame.rect().contains(sample) &&
                    channelDistance(frame.pixelColor(sample), background) <= 1,
                "Actual Retina letterbox pixels disagree with the independently mapped fitted "
                "image boundary");
        backgroundSamples.append(
            QJsonObject{{"logical", point(candidate)},
                        {"native_pixel_x", sample.x()},
                        {"native_pixel_y", sample.y()},
                        {"actual_rgba", static_cast<qint64>(frame.pixelColor(sample).rgba())}});
    }
    require(
        backgroundSamples.size() >= 2,
        "Odd-size Retina fixture did not expose two usable letterbox boundary positive controls");
    c.driver->log({{"action", "retina_independent_scene_mapping"},
                   {"name", name},
                   {"logical_painted_bounds", box(bounds)},
                   {"native_painted_bounds", box(nativeBounds)},
                   {"distinct_reference_colors", colors.size()},
                   {"flat_samples", samples},
                   {"letterbox_samples", backgroundSamples}});
}
void goldHandle(Context &c, const QString &areaID, const QPointF &normalized, const QString &name) {
    c.driver->frame();
    auto *area = c.driver->item(areaID);
    const auto logical =
        area->mapToScene({area->width() * normalized.x(), area->height() * normalized.y()});
    const auto expected = nativePoint(c, logical);
    const QColor gold(223, 174, 119);
    auto canvasDocument = c.editor.currentSettings();
    if (c.editor.cropMode()) {
        canvasDocument.crop.x = canvasDocument.crop.y = 0;
        canvasDocument.crop.width = canvasDocument.crop.height = 1;
        canvasDocument.crop.aspect = 0;
    }
    const auto plain = fixtureReference(c, canvasDocument, c.editor.sourcePath());
    for (int y = 0; y < plain.height(); ++y)
        for (int x = 0; x < plain.width(); ++x)
            require(channelDistance(plain.pixelColor(x, y), gold) > 16,
                    "Retina glyph fixture has gold-like underlying photo pixels and could give a "
                    "false positive");
    QJsonArray hits;
    QPointF centroid;
    bool centroidAvailable = false;
    QImage frame;
    const auto initial = nativeState(c);
    // Canvas paint is asynchronous. Await the actual rendered glyph, never
    // declare a requestPaint/polish counter to be its displayed acknowledgment.
    try {
        c.driver->wait(
            [&] {
                c.window->update();
                hits = {};
                centroid = {};
                centroidAvailable = false;
                frame = c.window->grabWindow().convertToFormat(QImage::Format_RGBA8888);
                if (frame.isNull() || frame.width() != c.window->width() * 2 ||
                    frame.height() != c.window->height() * 2)
                    return false;
                double sumX = 0, sumY = 0;
                for (int dy = -20; dy <= 20; ++dy)
                    for (int dx = -20; dx <= 20; ++dx) {
                        const QPoint sample(static_cast<int>(std::floor(expected.x())) + dx,
                                            static_cast<int>(std::floor(expected.y())) + dy);
                        if (!frame.rect().contains(sample))
                            return false;
                        if (channelDistance(frame.pixelColor(sample), gold) <= 1) {
                            sumX += sample.x() + .5;
                            sumY += sample.y() + .5;
                            hits.append(QJsonObject{{"x", sample.x()}, {"y", sample.y()}});
                        }
                    }
                if (!hits.isEmpty()) {
                    centroid = {sumX / static_cast<double>(hits.size()),
                                sumY / static_cast<double>(hits.size())};
                    centroidAvailable = true;
                }
                if (hits.size() < 60)
                    return false;
                return std::hypot(centroid.x() - expected.x(), centroid.y() - expected.y()) <= 1.1;
            },
            "Actual full Retina gold glyph has its independently mapped native-pixel centroid",
            5000);
    } catch (...) {
        if (!frame.isNull())
            frame.save(c.root + '/' + name + "-unmatched.png");
        c.driver->log({{"action", "retina_overlay_native_center_unmatched"},
                       {"name", name},
                       {"expected_native", point(expected)},
                       {"observed_gold_centroid", point(centroid)},
                       {"centroid_available_in_last_frame", centroidAvailable},
                       {"gold_samples", hits}});
        throw;
    }
    const auto final = nativeState(c);
    require(initial.windowNumber == final.windowNumber && initial.displayID == final.displayID &&
                frame.save(c.root + '/' + name + ".png"),
            "Cannot retain the actual stable-display Retina glyph scene");
    c.driver->log({{"action", "retina_overlay_native_center"},
                   {"name", name},
                   {"source_normalized", point(normalized)},
                   {"logical_center", point(logical)},
                   {"appkit_native_expected", point(expected)},
                   {"actual_gold_centroid", point(centroid)},
                   {"centroid_euclidean_distance_native_pixels",
                    std::hypot(centroid.x() - expected.x(), centroid.y() - expected.y())},
                   {"native_gold_samples", hits}});
}
void localReady(Context &c) {
    c.driver->wait([&] { return c.editor.localCanvasReady() && !c.editor.localSampling(); },
                   "Retina local canvas ready");
    require(!c.editor.localGestureActive() &&
                !c.driver->object("localMaskGestureArea")->property("pressed").toBool() &&
                !c.driver->object("localMaskGestureArea")->property("adjusting").toBool(),
            "Released Retina local gesture retained controller/QML pointer state");
}
} // namespace

bool retinaScenario(Context &c, const QString &id) {
    if (id != "UX-MAC-001")
        return false;
    auto &d = *c.driver;
    const auto initialNative = nativeState(c);
    require(c.window->screen()->availableGeometry().width() >= 1101 &&
                c.window->screen()->availableGeometry().height() >= 801,
            "This Retina fixture needs an actual visible1101x801logical content area");
    d.resize({1101, 801}); // Odd logical dimensions expose accidental 1x2x conflation.
    d.wait([&] { return c.window->width() == 1101 && c.window->height() == 801; },
           "Odd-size actual Retina window ready");
    nativeState(c);
    const auto source = c.root + "/owned-retina-tiles.png";
    createSource(source);
    const auto originalHash = hash(source);
    importOwn(c, source);
    const auto asset = c.editor.photoID();
    const auto baseline = c.editor.currentSettings();
    require(baseline == Document{},
            "Own Retina source must start with a complete neutral document");
    scenePixels(c, baseline, "retina-baseline-physical-scene");
    c.reveal("develop.exposureSlider");
    auto *slider = d.item("develop.exposureSlider");
    const QPointF exposureEnd(.7, .5);
    const auto endLocal = slider->mapFromScene(d.point("develop.exposureSlider", exposureEnd));
    auto *handle = qvariant_cast<QObject *>(slider->property("handle"));
    require(handle, "Retina slider fixture has no actual handle");
    const double half = handle->property("width").toDouble() / 2;
    const double travel = slider->property("availableWidth").toDouble() - 2 * half;
    require(travel > 20 && slider->property("orientation").toInt() == Qt::Horizontal,
            "Retina slider has no meaningful horizontal travel");
    const double position = std::clamp(
        (endLocal.x() - slider->property("leftPadding").toDouble() - half) / travel, 0.0, 1.0);
    const double pointerExpected = -5 + position * 10;
    // The integer-logical QTest press may land half a logical pixel off the
    // calculated thumb center. Bound that possible grab offset plus one public
    // .01EV step; this is input quantization, not an arbitrary UX tolerance.
    const double pointerTolerance = 5 / travel + .011;
    drag(c, "exposure", "develop.exposureSlider", d.sliderThumb("develop.exposureSlider"),
         exposureEnd);
    const auto edited = c.editor.currentSettings();
    auto expected = baseline;
    expected.exposure = edited.exposure;
    d.log({{"action", "retina_slider_independent_value_oracle"},
           {"actual", edited.exposure},
           {"expected_from_logical_geometry", pointerExpected},
           {"input_and_step_budget", pointerTolerance},
           {"handle_travel_logical", travel},
           {"release_local_x", endLocal.x()}});
    require(edited != baseline && edited == expected &&
                std::abs(edited.exposure - pointerExpected) <= pointerTolerance &&
                close(edited.exposure, slider->property("value").toDouble()) &&
                !slider->property("pressed").toBool(),
            "Retina pointer missed the independent logical Slider geometry/value mapping or "
            "changed unrelated edits");
    scenePixels(c, edited, "retina-exposure-physical-scene");
    d.click("main.undoButton");
    exact(c, baseline, "Retina Slider drag was not one exact Undo");
    d.click("main.redoButton");
    exact(c, edited, "Retina Slider Redo changed full state");
    d.click("main.undoButton");
    exact(c, baseline, "Cannot restore baseline for Retina overlay mapping");
    c.addMask(0);
    c.reveal("localOverlayToggle");
    if (c.editor.localOverlay())
        d.click("localOverlayToggle");
    localReady(c);
    const auto created = c.editor.currentSettings();
    require(created.localMasks.size() == 1 && !c.editor.localOverlay(),
            "Retina gradient fixture lacks its real neutral mask/display toggle");
    const QPointF gradientFrom(.2, .2), gradientTo(.75, .65);
    const auto gradientStart = normalizedDispatched(c, "localMaskGestureArea", gradientFrom);
    const auto gradientFinish = normalizedDispatched(c, "localMaskGestureArea", gradientTo);
    expected = created;
    expected.localMasks[0].x1 = gradientStart.x();
    expected.localMasks[0].y1 = gradientStart.y();
    expected.localMasks[0].x2 = gradientFinish.x();
    expected.localMasks[0].y2 = gradientFinish.y();
    drag(c, "linear_redraw", "localMaskGestureArea", gradientFrom, gradientTo);
    localReady(c);
    const auto gradient = c.editor.currentSettings();
    auto numeric = gradient;
    require(std::abs(gradient.localMasks[0].x1 - expected.localMasks[0].x1) < 1e-12 &&
                std::abs(gradient.localMasks[0].y1 - expected.localMasks[0].y1) < 1e-12 &&
                std::abs(gradient.localMasks[0].x2 - expected.localMasks[0].x2) < 1e-12 &&
                std::abs(gradient.localMasks[0].y2 - expected.localMasks[0].y2) < 1e-12,
            "Retina gradient source coordinates differ from actual logical dispatched points");
    numeric.localMasks[0].x1 = expected.localMasks[0].x1;
    numeric.localMasks[0].y1 = expected.localMasks[0].y1;
    numeric.localMasks[0].x2 = expected.localMasks[0].x2;
    numeric.localMasks[0].y2 = expected.localMasks[0].y2;
    require(numeric == expected, "Retina gradient redraw changed unrelated full-document fields");
    pixels(c, gradient);
    goldHandle(c, "localMaskGestureArea", gradientStart, "retina-linear-start-native-glyph");
    goldHandle(c, "localMaskGestureArea", gradientFinish, "retina-linear-end-native-glyph");
    const QPointF movedEnd(.65, .45);
    const auto actualMoved = normalizedDispatched(c, "localMaskGestureArea", movedEnd);
    expected = gradient;
    const auto actualPress = normalizedDispatched(c, "localMaskGestureArea", gradientFinish);
    expected.localMasks[0].x2 += actualMoved.x() - actualPress.x();
    expected.localMasks[0].y2 += actualMoved.y() - actualPress.y();
    drag(c, "linear_handle_hit", "localMaskGestureArea", gradientFinish, movedEnd);
    localReady(c);
    const auto moved = c.editor.currentSettings();
    require(std::abs(moved.localMasks[0].x2 - expected.localMasks[0].x2) < 1e-12 &&
                std::abs(moved.localMasks[0].y2 - expected.localMasks[0].y2) < 1e-12,
            "Actual native-positioned Retina overlay handle did not follow its independent pointer "
            "delta");
    expected.localMasks[0].x2 = moved.localMasks[0].x2;
    expected.localMasks[0].y2 = moved.localMasks[0].y2;
    require(moved != gradient && moved == expected,
            "Retina handle hit redrew the mask or changed unrelated geometry");
    pixels(c, moved);
    goldHandle(c, "localMaskGestureArea", {moved.localMasks[0].x2, moved.localMasks[0].y2},
               "retina-moved-end-native-glyph");
    d.click("main.undoButton");
    exact(c, gradient, "Retina actual handle drag was not one Undo");
    d.click("main.redoButton");
    exact(c, moved, "Retina handle Redo lost full geometry");
    c.reveal("cropTool");
    d.click("cropTool");
    d.wait([&] { return c.editor.cropCanvasReady(); }, "Retina crop overlay ready");
    const QPointF cropFrom(.001, .001), cropTo(.12, .1);
    const auto cropStart = normalizedDispatched(c, "cropDragArea", cropFrom);
    const auto cropFinish = normalizedDispatched(c, "cropDragArea", cropTo);
    expected = moved;
    expected.crop.x = cropFinish.x() - cropStart.x();
    expected.crop.y = cropFinish.y() - cropStart.y();
    expected.crop.width = 1 - expected.crop.x;
    expected.crop.height = 1 - expected.crop.y;
    drag(c, "crop_top_left_grip", "cropDragArea", cropFrom, cropTo);
    const auto cropped = c.editor.currentSettings();
    require(std::abs(cropped.crop.x - expected.crop.x) < 1e-12 &&
                std::abs(cropped.crop.y - expected.crop.y) < 1e-12 &&
                std::abs(cropped.crop.width - expected.crop.width) < 1e-12 &&
                std::abs(cropped.crop.height - expected.crop.height) < 1e-12,
            "Retina Crop grip source geometry does not follow actual logical pointer displacement");
    expected.crop = cropped.crop;
    require(cropped != moved && cropped == expected &&
                d.object("cropOverlay")->property("dragMode").toString().isEmpty() &&
                !d.object("cropDragArea")->property("pressed").toBool(),
            "Retina Crop grip changed unrelated edits or retained transient pointer state");
    // Crop's displayed frame rounds each edge to a source pixel. Calculate
    // that public display geometry independently, keeping raw saved Crop exact.
    const QPointF cropGlyph(static_cast<double>(std::lround(cropped.crop.x * 256)) / 256.0,
                            static_cast<double>(std::lround(cropped.crop.y * 192)) / 192.0);
    goldHandle(c, "cropDragArea", cropGlyph, "retina-crop-grip-native-glyph");
    c.reveal("cropTool");
    d.click("cropTool");
    require(!c.editor.cropMode() && !d.item("cropOverlay")->isVisible(),
            "Retina Done did not clear Crop overlay");
    scenePixels(c, cropped, "retina-cropped-physical-scene");
    d.click("main.undoButton");
    exact(c, moved, "Retina Crop drag was not one Undo");
    d.click("main.redoButton");
    exact(c, cropped, "Retina Crop Redo lost geometry");
    pixels(c, cropped);
    savedNaturally(c, asset, cropped);
    const auto finalNative = nativeState(c);
    require(initialNative.windowNumber == finalNative.windowNumber &&
                initialNative.displayID == finalNative.displayID && c.editor.photoID() == asset &&
                c.editor.sourcePath() == source && hash(source) == originalHash,
            "Retina test changed original bytes/source identity/native display ownership");
    writeJson(c.root + "/measurements.json",
              {{"scenario", id},
               {"boundary", "QT_QTEST_WINDOW_INPUT"},
               {"native_appkit_backing_scale", finalNative.backingScale},
               {"actual_scene_graphics_api", "metal"},
               {"physical_scanout_measured", false},
               {"hardware_pointer_events_exercised", false},
               {"sublogical_hardware_precision_exercised", false},
               {"forced_qt_scale", false},
               {"scene_color_channel_tolerance", 1},
               {"glyph_centroid_native_pixel_tolerance", 1.1},
               {"provider_pixels_tolerance", 0},
               {"full_document_and_undo", true},
               {"source_sha256", QString::fromLatin1(originalHash.toHex())}});
    return true;
}
} // namespace ux
