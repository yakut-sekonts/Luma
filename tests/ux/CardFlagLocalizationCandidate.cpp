#include "apps/shared/PresetStore.h"
#include "tests/ux/MemoryPressureProbe.h"
#include "tests/ux/PreviewResultProbe.h"
#include "tests/ux/UxContext.h"
#include "tests/ux/UxPixels.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

// Bounded PhotoCard flag-caption regression; not full VIS007/VIS008/LOC qualification.
namespace ux {
namespace {
using Document = luma::EditDocument;
constexpr auto candidateID = "CANDIDATE-PHOTOCARD-FLAG-LOCALIZATION";
struct Locale {
    const char *code, *pick, *reject;
};
// Independent literal oracle, manually checked against current resource files.
// Never derive expected captions from the translator/card/flag ComboBox under test.
constexpr std::array locales{
    Locale{"en", "Pick", "Reject"},          Locale{"ru", "Выбрано", "Отклонено"},
    Locale{"zh_CN", "已选", "已拒绝"},       Locale{"hy", "Ընտրված", "Մերժված"},
    Locale{"de", "Ausgewählt", "Abgelehnt"}, Locale{"es", "Seleccionada", "Rechazada"},
    Locale{"it", "Selezionata", "Scartata"}, Locale{"fr", "Retenue", "Rejetée"}};
QString digest(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read card localization source: " + path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    require(hash.addData(&file), "Cannot hash full card localization source");
    return QString::fromLatin1(hash.result().toHex());
}
QJsonObject fullDocument(const Document &document) {
    const auto encoded = QJsonDocument::fromJson(
        luma::encodePreset({"Card flag complete document", {}, document, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot retain complete card localization document");
    auto result = encoded.object();
    // encodePreset(allSettingsGroups) omits Local masks; retain their complete
    // canonical serialization explicitly rather than calling that subset full.
    result["local_masks_serialized"] =
        QString::fromStdString(luma::encodeLocalMasks(document.localMasks));
    return result;
}
QJsonObject messages(const Locale &locale) {
    QFile file(":/luma/i18n/" + QString::fromLatin1(locale.code) + ".json");
    require(file.open(QIODevice::ReadOnly), "Missing compiled card localization resource");
    const auto result = QJsonDocument::fromJson(file.readAll()).object()["messages"].toObject();
    require(result.value("Pick").toString() == QString::fromUtf8(locale.pick) &&
                result.value("Reject").toString() == QString::fromUtf8(locale.reject),
            "Independent literal Pick/Reject oracle differs from the compiled resource");
    return result;
}
bool sameRow(const luma::LibraryItem &a, const luma::LibraryItem &b) {
    return std::tie(a.id, a.path, a.filename, a.checksum, a.width, a.height, a.orientation,
                    a.rating, a.flag, a.label,
                    a.camera) == std::tie(b.id, b.path, b.filename, b.checksum, b.width, b.height,
                                          b.orientation, b.rating, b.flag, b.label, b.camera);
}
bool sameMetadata(const luma::PhotoMetadata &a, const luma::PhotoMetadata &b) {
    return std::tie(a.camera, a.lens, a.iso, a.shutterSeconds, a.aperture, a.focalLength,
                    a.captureTimestamp, a.width, a.height, a.orientation) ==
           std::tie(b.camera, b.lens, b.iso, b.shutterSeconds, b.aperture, b.focalLength,
                    b.captureTimestamp, b.width, b.height, b.orientation);
}
luma::AssetSnapshot saved(Context &c, const std::string &id) {
    const auto value = luma::Catalog(c.catalogFile.toStdString()).assetSnapshot(id);
    require(value.has_value(), "Card localization UUID disappeared from the own catalog");
    return *value;
}
void unchangedAssets(Context &c, const std::array<luma::AssetSnapshot, 2> &assets) {
    for (const auto &expected : assets) {
        const auto current = saved(c, expected.asset.id);
        require(current.asset.edits == expected.asset.edits &&
                    current.revision == expected.revision &&
                    current.asset.id == expected.asset.id &&
                    current.asset.path == expected.asset.path &&
                    current.asset.checksum == expected.asset.checksum &&
                    sameMetadata(current.asset.metadata, expected.asset.metadata) &&
                    digest(QString::fromStdString(current.asset.path.string())).toStdString() ==
                        expected.asset.checksum,
                "Flag/language UI changed a pinned full durable document/revision/metadata or "
                "source bytes");
    }
}
void modelReady(Context &c, const std::array<luma::LibraryItem, 2> &expected) {
    c.driver->wait(
        [&] {
            if (c.library.working() || c.library.loading() || c.library.selecting() ||
                c.library.count() != 2)
                return false;
            for (int index = 0; index < 2; ++index) {
                const auto row = c.library.index(index);
                if (c.library.data(row, LibraryModel::AssetID).toString() !=
                        QString::fromStdString(expected[index].id) ||
                    c.library.data(row, LibraryModel::Path).toUrl().toLocalFile() !=
                        QString::fromStdString(expected[index].path.string()) ||
                    c.library.data(row, LibraryModel::Filename).toString().isEmpty() ||
                    c.library.data(row, LibraryModel::Flag).toInt() != expected[index].flag ||
                    c.library.data(row, LibraryModel::Rating).toInt() != expected[index].rating)
                    return false;
            }
            return true;
        },
        "Card localization actual populated model IDs/paths/flags/ratings", 15000);
    require(c.library.error().isEmpty(), c.library.error());
    c.driver->wait([&] { return !c.driver->object("library.grid")->property("moving").toBool(); },
                   "Card localization grid movement settled");
    c.driver->frame();
}
void durableRows(Context &c, const std::array<luma::LibraryItem, 2> &expected) {
    c.driver->wait(
        [&] {
            const luma::Catalog catalog(c.catalogFile.toStdString());
            const auto actual = catalog.libraryPage({}, 0, 2);
            return !c.library.working() && catalog.libraryCount() == 2 && actual.size() == 2 &&
                   sameRow(actual[0], expected[0]) && sameRow(actual[1], expected[1]);
        },
        "Actual Flag/Rating UI transaction committed exact durable metadata", 10000);
    modelReady(c, expected);
}
QString cardID(const luma::LibraryItem &asset) {
    return "library.card." + QString::fromStdString(asset.id);
}
QQuickItem *card(Context &c, const luma::LibraryItem &asset) {
    const auto name = cardID(asset);
    c.driver->wait([&] { return c.driver->find(name) != nullptr; },
                   "Actual card delegate exists for pinned UUID");
    auto *result = c.driver->item(name);
    require(!result->property("compact").toBool() &&
                result->property("assetID").toString() == QString::fromStdString(asset.id) &&
                result->property("flagValue").toInt() == asset.flag &&
                result->property("rating").toInt() == asset.rating,
            "Current card does not have exact populated model flag/rating identity");
    return result;
}
QQuickItem *status(Context &c, const luma::LibraryItem &asset) {
    auto *owner = card(c, asset);
    auto *label = c.driver->item("library.flagStatus." + QString::fromStdString(asset.id));
    require(label->parentItem() && label->parentItem()->parentItem() == owner,
            "Status label is not the actual owning PhotoCard caption child");
    return label;
}
void readyCards(Context &c, const std::array<luma::LibraryItem, 2> &expected) {
    modelReady(c, expected);
    c.driver->wait(
        [&] {
            for (const auto &asset : expected) {
                auto *owner = qobject_cast<QQuickItem *>(c.driver->find(cardID(asset)));
                if (!owner ||
                    owner->property("assetID").toString() != QString::fromStdString(asset.id) ||
                    owner->property("flagValue").toInt() != asset.flag ||
                    owner->property("rating").toInt() != asset.rating)
                    return false;
                auto *image = owner->findChild<QQuickItem *>("photoThumbnail");
                const auto base = owner->property("thumbnailURL").toString();
                const int retries = owner->property("retryCount").toInt();
                const auto actual = base + (retries ? (base.contains('?') ? "&" : "?") +
                                                          QString("retry=%1").arg(retries)
                                                    : QString{});
                if (!owner->isVisible() || !image || !image->isVisible() ||
                    image->property("status").toInt() != 1 ||
                    !base.startsWith("image://thumbnail/" + QString::fromStdString(asset.id) +
                                     "?generation=") ||
                    image->property("source").toUrl().toString() != actual)
                    return false;
            }
            return true;
        },
        "Both genuine bound card thumbnails are Ready/current source", 20000);
    c.driver->frame();
}
bool fullyVisible(Context &c, QQuickItem *item) {
    if (!item || !item->isVisible() || item->width() <= 0 || item->height() <= 0)
        return false;
    const auto bounds = item->mapRectToScene(item->boundingRect());
    if (!QRectF(0, 0, c.window->width(), c.window->height())
             .adjusted(-.5, -.5, .5, .5)
             .contains(bounds))
        return false;
    for (auto *ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem())
        if (ancestor->clip() && !ancestor->boundingRect()
                                     .adjusted(-.5, -.5, .5, .5)
                                     .contains(ancestor->mapRectFromScene(bounds)))
            return false;
    return true;
}
void revealStatuses(Context &c, const std::array<luma::LibraryItem, 2> &expected) {
    // Caption localization is not a wheel-input contract. Use the actual
    // scrollbar, with acquired press and acknowledged directional movement.
    // Never assign contentY or require the whole taller card to fit the grid.
    auto &d = *c.driver;
    auto *view = d.item("library.grid");
    {
        constexpr int step = 0;
        d.wait([&] { return !view->property("moving").toBool(); }, "Flag caption scroll settled",
               3000);
        d.frame();
        QQuickItem *missing = nullptr;
        for (const auto &asset : expected)
            if (!fullyVisible(c, status(c, asset))) {
                missing = status(c, asset);
                break;
            }
        if (!missing)
            return;
        const auto mapped = missing->mapRectToItem(view, missing->boundingRect());
        const bool above = mapped.top() < 0;
        const auto before = view->property("contentY").toDouble();
        auto *bar = d.item("library.scrollBar");
        const double size = bar->property("size").toDouble();

        require(bar->isVisible() && bar->isEnabled() && size > 0 && size < 1 && bar->width() > 0 &&
                    bar->height() > 0,
                "Clipped flag caption has no usable actual scrollbar");
        auto *thumb =
            qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(bar->property("contentItem")));
        require(thumb && thumb->isVisible() && thumb->isEnabled() && thumb->width() > 0 &&
                    thumb->height() > 0,
                "Actual caption scrollbar has no usable thumb");
        const QPointF center = thumb->mapToItem(bar, thumb->boundingRect().center());
        const QPointF from(center.x() / bar->width(), center.y() / bar->height());
        const double top = bar->property("topPadding").toDouble();
        const double available = bar->property("availableHeight").toDouble();
        require(available > thumb->height(), "Actual caption scrollbar has no thumb travel");
        const QPointF to(from.x(),
                         (top + thumb->height() / 2 + (above ? 0 : available - thumb->height())) /
                             bar->height());
        struct ReleasePointer {
            QPointer<QQuickWindow> window;
            QPoint last;
            bool down = false;
            ~ReleasePointer() {
                if (down && window)
                    QTest::mouseRelease(window.data(), Qt::LeftButton, Qt::NoModifier, last);
            }
        } pointer{c.window, d.point("library.scrollBar", from)};
        const auto snapshot = [&] {
            return QJsonObject{{"content_y", view->property("contentY").toDouble()},
                               {"origin_y", view->property("originY").toDouble()},
                               {"content_height", view->property("contentHeight").toDouble()},
                               {"viewport_height", view->height()},
                               {"moving", view->property("moving").toBool()},
                               {"scrollbar_pressed", bar->property("pressed").toBool()}};
        };
        const auto beforeInput = snapshot();
        try {
            d.press("library.scrollBar", from);
            pointer.down = true;
            require(bar->property("pressed").toBool(),
                    "Actual caption scrollbar press was not acquired");
            pointer.last = d.point("library.scrollBar", to);
            d.move("library.scrollBar", to);
            d.release("library.scrollBar", to);
            pointer.down = false;
            require(!bar->property("pressed").toBool(),
                    "Actual caption scrollbar retained its pointer press");
            d.wait(
                [&] {
                    const double delta = view->property("contentY").toDouble() - before;
                    return above ? delta < -.01 : delta > .01;
                },
                "Real caption scrollbar input moved in the required direction", 1000);
            d.wait([&] { return !view->property("moving").toBool(); },
                   "Actual caption scrollbar movement settled", 3000);
            writeJson(c.root + "/card-flags-caption-scroll-" + QString::number(step) + ".json",
                      {{"before_input", beforeInput},
                       {"after_input", snapshot()},
                       {"caption_top_before", mapped.top()},
                       {"caption_bottom_before", mapped.bottom()},
                       {"direction", above ? "up" : "down"},
                       {"actual_scrollbar_press", true}});
        } catch (...) {
            try {
                writeJson(c.root + "/card-flags-caption-scroll-before-cleanup.json",
                          {{"before_input", beforeInput},
                           {"failure", snapshot()},
                           {"caption_top_before", mapped.top()},
                           {"caption_bottom_before", mapped.bottom()}});
            } catch (...) {
                std::fputs("Cannot retain caption-scroll failure diagnostics\n", stderr);
            }
            throw;
        }
    }
    d.frame();
    const double origin = view->property("originY").toDouble();
    const double maximum = origin + view->property("contentHeight").toDouble() - view->height();
    const double actual = view->property("contentY").toDouble();
    require(actual >= origin - .5 && actual <= maximum + .5,
            "Caption scrollbar left the actual legal content range");
    for (const auto &asset : expected)
        require(fullyVisible(c, status(c, asset)),
                "One real scrollbar drag did not expose both actual flag captions");
}
QString caption(int rating, int flag, const Locale &locale) {
    const auto stars = rating ? QString(rating, QChar(0x2605)) : QString::fromUtf8("☆☆☆☆☆");
    return stars + (flag == 1    ? "  " + QString::fromUtf8(locale.pick)
                    : flag == -1 ? "  " + QString::fromUtf8(locale.reject)
                                 : QString{});
}
void chooseLocale(Context &c, int index) {
    const auto &locale = locales[static_cast<std::size_t>(index)];
    const auto resource = messages(locale);
    auto &d = *c.driver;
    d.click("settingsButton");
    d.wait([&] { return d.object("preferencesPanel")->property("opened").toBool(); },
           "Actual card-locale Settings opened");
    d.choose("languageChoice", index);
    d.wait([&] { return c.localization->language() == QString::fromLatin1(locale.code); },
           "Actual languageChoice activated the expected locale");
    d.frame();
    require(c.localization->error().isEmpty() &&
                d.object("preferencesPanel")->property("title").toString() ==
                    resource.value("Settings").toString() &&
                d.object("main.libraryButton")->property("text").toString() ==
                    resource.value("Library").toString(),
            "Language code changed without actual UI retranslation");
    QFile preferences(c.stateRoot + "/language.json");
    require(preferences.open(QIODevice::ReadOnly) &&
                QJsonDocument::fromJson(preferences.readAll()).object()["language"].toString() ==
                    QString::fromLatin1(locale.code),
            "Actual language preference did not persist");
    d.key(Qt::Key_Escape);
    d.wait([&] { return !d.object("preferencesPanel")->property("visible").toBool(); },
           "Actual Settings modal closed before card acceptance");
    d.frame();
}
void select(Context &c, const std::array<luma::LibraryItem, 2> &expected, int index) {
    revealStatuses(c, expected);
    const auto &asset = expected[static_cast<std::size_t>(index)];
    auto *owner = card(c, asset);
    auto *label = status(c, asset);
    const auto center = label->mapToScene(label->boundingRect().center());
    require(fullyVisible(c, label), "Actual card selection caption is clipped");
    bool ownedHit = false;
    for (auto *child : owner->childItems())
        if (child->inherits("QQuickMouseArea") && child->isVisible() && child->isEnabled() &&
            child->contains(child->mapFromScene(center)))
            ownedHit = true;
    require(ownedHit, "Visible caption center is not inside its own actual card MouseArea");
    // A minimum-height grid can clip the whole-card center. The caption is a
    // real, independently visible point within the same full-card MouseArea.
    c.driver->click(label->objectName());
    c.driver->wait(
        [&] {
            return !c.library.selecting() &&
                   c.library.selectionIDs() == std::vector<std::string>{asset.id} &&
                   card(c, asset)->property("selected").toBool();
        },
        "Real card click selected only the intended flag target");
}
void flag(Context &c, std::array<luma::LibraryItem, 2> &expected, int index, int value) {
    select(c, expected, index);
    QSignalSpy activation(c.driver->object("library.flagChoice"), SIGNAL(activated(int)));
    require(activation.isValid(), "No observable actual metadata Flag ComboBox");
    const int choice = value == 1 ? 1 : value == -1 ? 2 : 3;
    c.driver->choose("library.flagChoice", choice);
    require(activation.count() == 1 && activation.at(0).at(0).toInt() == choice &&
                c.driver->object("library.flagChoice")->property("currentIndex").toInt() == 0,
            "Flag input did not travel through the real activated/reset path");
    expected[static_cast<std::size_t>(index)].flag = value;
    durableRows(c, expected);
}
QJsonArray rectangle(const QRectF &value) {
    return {value.x(), value.y(), value.width(), value.height()};
}
QJsonObject cardEvidence(Context &c, const luma::LibraryItem &asset, const Locale &locale) {
    auto *owner = card(c, asset);
    auto *label = status(c, asset);
    const auto bounds = label->mapRectToScene(label->boundingRect());
    const int row = owner->property("index").toInt();
    return {{"uuid", QString::fromStdString(asset.id)},
            {"source", QString::fromStdString(asset.path.string())},
            {"model_flag", c.library.data(c.library.index(row), LibraryModel::Flag).toInt()},
            {"model_rating", c.library.data(c.library.index(row), LibraryModel::Rating).toInt()},
            {"card_flag_value", owner->property("flagValue").toInt()},
            {"card_rating", owner->property("rating").toInt()},
            {"expected_flag", asset.flag},
            {"expected_rating", asset.rating},
            {"actual_text", label->property("text").toString()},
            {"expected_literal_text", caption(asset.rating, asset.flag, locale)},
            {"status_visible", label->isVisible()},
            {"status_scene_rect", rectangle(bounds)},
            {"card_scene_rect", rectangle(owner->mapRectToScene(owner->boundingRect()))},
            {"thumbnail_url", owner->property("thumbnailURL").toString()},
            {"own_content_exclusion", "caption vs the same PhotoCard full-card MouseArea only"}};
}
void captionBounds(Context &c, const luma::LibraryItem &asset, const luma::LibraryItem &other) {
    auto *owner = card(c, asset);
    auto *label = status(c, asset);
    const auto bounds = label->mapRectToScene(label->boundingRect());
    require(owner->isVisible() && label->isVisible() && bounds.width() > 0 && bounds.height() > 0 &&
                std::isfinite(bounds.x()) && std::isfinite(bounds.y()) && fullyVisible(c, label) &&
                owner->mapRectToScene(owner->boundingRect())
                    .adjusted(-.5, -.5, .5, .5)
                    .contains(bounds),
            "Actual translated flag caption escaped its visible owning card/window bounds");
    // Own full-card MouseArea intentionally contains its own caption. Do not
    // mistake that composite ownership for a foreign overlapping input control.
    require(!bounds.intersects(card(c, other)->mapRectToScene(card(c, other)->boundingRect())),
            "Translated flag caption overlaps a different PhotoCard");
    auto *bar = c.driver->item("library.scrollBar");
    if (bar->isVisible())
        require(!bounds.intersects(bar->mapRectToScene(bar->boundingRect())),
                "Translated flag caption intersects the foreign Library scrollbar");
}
struct Baseline {
    std::array<luma::AssetSnapshot, 2> assets;
    std::array<luma::LibraryItem, 2> rows;
    std::vector<std::string> members, selected;
    Document current;
    QString source, photo, preview;
    QImage pixels;
};
void invariant(Context &c, const Baseline &baseline) {
    const luma::Catalog catalog(c.catalogFile.toStdString());
    const auto actual = catalog.libraryPage({}, 0, 2);
    const QJsonObject observations{
        {"window", c.window->size() == QSize(1000, 640)},
        {"library_mode", c.window->property("libraryMode").toBool()},
        {"photo", c.editor.photoID() == baseline.photo},
        {"source", c.editor.sourcePath() == baseline.source},
        {"document", c.editor.currentSettings() == baseline.current},
        {"undo", c.editor.canUndo()},
        {"redo_empty", !c.editor.canRedo()},
        {"editor_idle", !c.editor.busy()},
        {"save_idle", !c.editor.saving()},
        {"editor_error_empty", c.editor.error().isEmpty()},
        {"guided_off", !c.editor.guidedMode()},
        {"crop_off", !c.editor.cropMode()},
        {"local_off", !c.editor.localMode()},
        {"local_gesture_off", !c.editor.localGestureActive()},
        {"original_off", !c.editor.original()},
        {"editing_unlocked", !c.editor.editingLocked()},
        {"preset_preview_off", !c.editor.presetPreview()},
        {"preview_url", c.editor.previewUrl() == baseline.preview},
        {"preview_pixels", c.provider->requestImage({}, nullptr, {}) == baseline.pixels},
        {"library_idle", !c.library.working()},
        {"library_loaded", !c.library.loading()},
        {"selection_idle", !c.library.selecting()},
        {"count", c.library.count() == 2},
        {"library_error_empty", c.library.error().isEmpty()},
        {"members", catalog.libraryIDs({}) == baseline.members},
        {"selection", c.library.selectionIDs() == baseline.selected},
        {"rows", actual.size() == 2 && sameRow(actual[0], baseline.rows[0]) &&
                     sameRow(actual[1], baseline.rows[1])}};
    for (auto field = observations.begin(); field != observations.end(); ++field)
        if (!field.value().toBool()) {
            writeJson(c.root + "/card-flags-invariant-failure.json",
                      {{"checks", observations},
                       {"preview_url_before", baseline.preview},
                       {"preview_url_after", c.editor.previewUrl()},
                       {"expected_document", fullDocument(baseline.current)},
                       {"actual_document", fullDocument(c.editor.currentSettings())}});
            break;
        }
    require(c.window->size() == QSize(1000, 640) && c.window->property("libraryMode").toBool() &&
                c.editor.photoID() == baseline.photo && c.editor.sourcePath() == baseline.source &&
                c.editor.currentSettings() == baseline.current && c.editor.canUndo() &&
                !c.editor.canRedo() && !c.editor.busy() && !c.editor.saving() &&
                c.editor.error().isEmpty() && !c.editor.guidedMode() && !c.editor.cropMode() &&
                !c.editor.localMode() && !c.editor.localGestureActive() && !c.editor.original() &&
                !c.editor.editingLocked() && !c.editor.presetPreview() &&
                c.editor.previewUrl() == baseline.preview &&
                c.provider->requestImage({}, nullptr, {}) == baseline.pixels &&
                !c.library.working() && !c.library.loading() && !c.library.selecting() &&
                c.library.count() == 2 && c.library.error().isEmpty() &&
                catalog.libraryIDs({}) == baseline.members &&
                c.library.selectionIDs() == baseline.selected && actual.size() == 2 &&
                sameRow(actual[0], baseline.rows[0]) && sameRow(actual[1], baseline.rows[1]),
            "Language/reveal mutated current full document/history/flags/selection/membership");
    unchangedAssets(c, baseline.assets);
}
void naturalSave(Context &c, const std::string &id, const Document &expected) {
    c.driver->wait(
        [&] {
            return !c.editor.saving() && !c.editor.busy() && saved(c, id).asset.edits == expected;
        },
        "Card localization natural full edit persistence");
    require(c.editor.error().isEmpty() && c.editor.currentSettings() == expected,
            "Card localization autosave changed the expected complete document");
}
bool previewPixelsReady(Context &c, const QImage &pixels) {
    auto *image = c.driver->item("develop.preview");
    return !c.editor.busy() && image->isVisible() && image->property("status").toInt() == 1 &&
           image->property("source").toUrl().toString() == c.editor.previewUrl() &&
           c.provider->requestImage({}, nullptr, {}) == pixels;
}
bool previewSettled(Context &c, const QImage &pixels) {
    // Small originals can have identical interactive/final pixels. EndEdit's
    // actual final delivery must settle before pinning an immutable preview URL.
    return !c.editor.histogramPending() && previewPixelsReady(c, pixels);
}
void preview(Context &c, const Document &expected, const QString &path) {
    const auto pixels = fixtureReference(c, expected, path);
    c.driver->wait([&] { return previewSettled(c, pixels); },
                   "Card localization actual final Ready Develop preview");
    require(c.editor.currentSettings() == expected, "Preview changed the complete seed document");
}
} // namespace

bool cardFlagLocalizationCandidate(Context &c, const QString &id) {
    if (id != candidateID)
        return false;
    auto &d = *c.driver;
    require(luma::Catalog(c.catalogFile.toStdString()).libraryCount() == 0,
            "Card localization requires a fresh two-image catalog");
    for (const auto &locale : locales)
        (void)messages(locale);
    const auto languages = c.localization->languages();
    require(languages.size() == static_cast<qsizetype>(locales.size()),
            "Card localization expected exactly eight supported languages");
    for (qsizetype index = 0; index < languages.size(); ++index)
        require(languages[index].toMap()["code"].toString() ==
                    QString::fromLatin1(locales[static_cast<std::size_t>(index)].code),
                "Actual languageChoice order differs from the explicit independent manifest");
    // Observe real OS pressure without suppressing callbacks or injecting state.
    // It may legitimately re-render while this caption-only scenario is running.
    QObject observation;
    QObject::connect(&c.editor, &EditorController::stateChanged, &observation, [&] {
        d.log({{"action", "card_flags_editor_state"},
               {"pressure", int(MemoryPressureProbe::current(c.editor))},
               {"preview_url", c.editor.previewUrl()},
               {"histogram_pending", c.editor.histogramPending()}});
    });
    c.import(2); // Declared chooser-result setup; actual Import buttons/processing.
    const auto rows = luma::Catalog(c.catalogFile.toStdString()).libraryPage({}, 0, 2);
    require(rows.size() == 2 && rows[0].id != rows[1].id && rows[0].flag == 0 &&
                rows[1].flag == 0 && rows[0].rating == 0 && rows[1].rating == 0,
            "Card localization import does not contain exactly two neutral flagged/rated assets");
    std::array<luma::LibraryItem, 2> expected{rows[0], rows[1]};
    const Document neutral;
    for (const auto &asset : expected)
        require(saved(c, asset.id).asset.edits == neutral &&
                    digest(QString::fromStdString(asset.path.string())).toStdString() ==
                        asset.checksum,
                "Fresh card source/document differs from the typed neutral/checksum fixture");
    modelReady(c, expected);
    d.doubleClick(cardID(expected[0]));
    const auto source = QString::fromStdString(expected[0].path.string());
    preview(c, neutral, source);
    require(c.editor.photoID() == QString::fromStdString(expected[0].id) && !c.editor.canUndo() &&
                !c.editor.canRedo(),
            "Card localization seed opened another asset or inherited history");
    c.reveal("develop.exposureSlider");
    Document edited = neutral;
    edited.exposure = 5;
    const auto positivePixels = fixtureReference(c, edited, source);
    {
        d.press("develop.exposureSlider", d.sliderThumb("develop.exposureSlider"));
        d.move("develop.exposureSlider", {1., .5});
        d.wait(
            [&] {
                return c.editor.currentSettings() == edited &&
                       previewPixelsReady(c, positivePixels);
            },
            "Actual interactive seed preview before pointer release");
        const auto interactiveURL = c.editor.previewUrl();
        // Arm only after the edited interactive result has arrived. A refinement
        // from initial photo loading may still finish before the drag starts;
        // that unrelated completion must not become this gesture's held final.
        PreviewResultGate final(c.editor, 2560);
        d.release("develop.exposureSlider", {1., .5});
        d.wait(
            [&] {
                const auto state = final.snapshot();
                return state.held && state.held->job->state.load() == luma::JobState::completed;
            },
            "Actual identical-pixel final completion retained");
        const auto held = final.snapshot();
        d.log({{"action", "card_flags_pending_final_observation"},
               {"held", bool(held.held)},
               {"asset_matches", held.held && held.held->assetID == expected[0].id},
               {"held_pixels_match", held.pixels == positivePixels},
               {"hook_failed", held.hookFailed},
               {"released", held.released},
               {"deliveries", int(held.deliveries)},
               {"pixels_ready", previewPixelsReady(c, positivePixels)},
               {"interactive_url", interactiveURL},
               {"actual_url", c.editor.previewUrl()},
               {"busy", c.editor.busy()},
               {"histogram_pending", c.editor.histogramPending()},
               {"preview_settled", previewSettled(c, positivePixels)}});
        require(held.held && held.held->assetID == expected[0].id &&
                    held.pixels == positivePixels && !held.hookFailed && !held.released &&
                    held.deliveries == 0 && previewPixelsReady(c, positivePixels) &&
                    c.editor.previewUrl() == interactiveURL && c.editor.histogramPending() &&
                    !previewSettled(c, positivePixels),
                "Preview readiness accepted identical interactive pixels with a pending final");
        final.release();
        d.wait(
            [&] { return final.snapshot().deliveries == 1 && previewSettled(c, positivePixels); },
            "Original final callback accepted before pinning preview baseline");
        const auto delivered = final.snapshot();
        require(delivered.held && delivered.delivered &&
                    delivered.held->sameAs(*delivered.delivered) && delivered.rejections == 0 &&
                    !delivered.hookFailed && !delivered.deliveryBeforeRelease &&
                    c.editor.previewUrl() != interactiveURL,
                "Final readiness control did not deliver the exact current result once");
        d.log({{"action", "card_flags_final_readiness_control"},
               {"interactive_url", interactiveURL},
               {"final_url", c.editor.previewUrl()},
               {"identical_pixels", true},
               {"pending_final_rejected", true},
               {"deliveries", int(delivered.deliveries)},
               {"rejections", int(delivered.rejections)}});
    }
    require(!d.object("develop.exposureSlider")->property("pressed").toBool() &&
                c.editor.currentSettings() == edited && c.editor.canUndo() && !c.editor.canRedo(),
            "Real seed drag did not release with exactly the expected isolated Exposure edit");
    naturalSave(c, expected[0].id, edited);
    preview(c, edited, source);
    require(positivePixels != fixtureReference(c, neutral, source),
            "Card localization typed edit has no positive pixel effect");
    // Pin these BEFORE any Rating/Flag UI; a metadata-operation defect must not
    // become the new expected document/revision/metadata baseline.
    const std::array<luma::AssetSnapshot, 2> protectedAssets{saved(c, expected[0].id),
                                                             saved(c, expected[1].id)};
    require(protectedAssets[0].asset.edits == edited && protectedAssets[1].asset.edits == neutral,
            "Typed A edited/B neutral baseline was not persisted before metadata controls");
    const std::vector<std::string> members{expected[0].id, expected[1].id};
    require(luma::Catalog(c.catalogFile.toStdString()).libraryIDs({}) == members,
            "Two-image fixture has unexpected initial membership");
    d.resize({1000, 640});
    d.click("main.libraryButton");
    readyCards(c, expected);
    require(d.object("libraryFlagFilter")->property("currentIndex").toInt() == 0 &&
                d.object("library.ratingFilter")->property("currentIndex").toInt() == 0 &&
                d.object("libraryLabelFilter")->property("currentIndex").toInt() == 0 &&
                d.object("library.search")->property("text").toString().isEmpty(),
            "Card localization must preserve an unfiltered two-card view");
    // Neutral flag must add no suffix or trailing spaces. This is a separate
    // positive control before either nonempty translated branch is entered.
    for (const auto &asset : expected)
        require(status(c, asset)->property("text").toString() == caption(0, 0, locales[0]),
                "Unflagged card did not preserve its exact five-empty-stars prefix");
    select(c, expected, 1);
    auto *ratingTarget = card(c, expected[1]);
    require(ratingTarget->hasActiveFocus() && c.window->activeFocusItem() == ratingTarget,
            "Actual PhotoCard click did not focus the numeric rating shortcut target");
    d.key(Qt::Key_3); // Existing real Library shortcut, not a rating/model setter.
    expected[1].rating = 3;
    durableRows(c, expected);
    unchangedAssets(c, protectedAssets);
    require(status(c, expected[1])->property("text").toString() == caption(3, 0, locales[0]),
            "Rated neutral card did not preserve its exact three-stars-only prefix");
    flag(c, expected, 0, 1);
    unchangedAssets(c, protectedAssets);
    flag(c, expected, 1, -1);
    unchangedAssets(c, protectedAssets);
    readyCards(c, expected);
    const Baseline baseline{protectedAssets,
                            expected,
                            members,
                            c.library.selectionIDs(),
                            edited,
                            source,
                            QString::fromStdString(expected[0].id),
                            c.editor.previewUrl(),
                            positivePixels};
    require(baseline.selected == std::vector<std::string>{expected[1].id},
            "Actual Flag controls changed the selected target set unexpectedly");
    QJsonArray observedLocales;
    for (std::size_t index = 0; index < locales.size(); ++index) {
        const auto &locale = locales[index];
        chooseLocale(c, static_cast<int>(index));
        readyCards(c, expected);
        revealStatuses(c, expected);
        invariant(c, baseline);
        QJsonArray evidence;
        for (const auto &asset : expected)
            evidence.append(cardEvidence(c, asset, locale));
        const auto code = QString::fromLatin1(locale.code);
        writeJson(c.root + "/card-flags-" + code + ".json",
                  {{"scope", "PhotoCard Pick/Reject only; not full VIS007/VIS008/LOC"},
                   {"locale", code},
                   {"window", QJsonArray{1000, 640}},
                   {"cards", evidence},
                   {"current_full_document", fullDocument(c.editor.currentSettings())},
                   {"a_full_durable_document", fullDocument(baseline.assets[0].asset.edits)},
                   {"b_full_durable_document", fullDocument(baseline.assets[1].asset.edits)}});
        d.screenshot(c.root + "/card-flags-" + code + ".png");
        // Strict first original failure is here in Russian: actual English text
        // is captured above, never accepted by translator-derived expectation.
        for (const auto &asset : expected) {
            const auto actual = status(c, asset)->property("text").toString();
            const auto literal = caption(asset.rating, asset.flag, locale);
            require(actual == literal, "PhotoCard flag localization mismatch for " + code +
                                           ": expected [" + literal + "], actual [" + actual + "]");
        }
        captionBounds(c, expected[0], expected[1]);
        captionBounds(c, expected[1], expected[0]);
        for (const auto &asset : expected)
            thumbnailCardPixels(c, card(c, asset), saved(c, asset.id));
        invariant(c, baseline);
        observedLocales.append(code);
    }
    // Exact history was retained under Library UI: one Undo returns the complete
    // neutral document and empties Undo; one Redo restores the whole typed edit.
    d.click("main.developButton");
    preview(c, edited, source);
    d.click("main.undoButton");
    preview(c, neutral, source);
    naturalSave(c, expected[0].id, neutral);
    require(!c.editor.canUndo() && c.editor.canRedo(),
            "Language/flag UI polluted seed Undo history");
    auto afterUndoExpected = protectedAssets;
    afterUndoExpected[0].asset.edits = neutral;
    ++afterUndoExpected[0].revision;
    unchangedAssets(c, afterUndoExpected);
    d.click("main.redoButton");
    preview(c, edited, source);
    naturalSave(c, expected[0].id, edited);
    require(c.editor.canUndo() && !c.editor.canRedo() &&
                saved(c, expected[1].id).asset.edits == neutral,
            "Language/flag UI changed Redo branch or crossed asset document boundaries");
    auto afterRedoExpected = protectedAssets;
    afterRedoExpected[0].revision += 2;
    unchangedAssets(c, afterRedoExpected);
    const auto finalRows = luma::Catalog(c.catalogFile.toStdString()).libraryPage({}, 0, 2);
    require(finalRows.size() == 2 && sameRow(finalRows[0], expected[0]) &&
                sameRow(finalRows[1], expected[1]) &&
                luma::Catalog(c.catalogFile.toStdString()).libraryIDs({}) == members,
            "Post-language history verification changed durable flags/rating/membership");
    for (const auto &asset : expected)
        require(digest(QString::fromStdString(asset.path.string())).toStdString() == asset.checksum,
                "Card localization changed original source bytes");
    luma::Catalog(c.catalogFile.toStdString()).verify();
    writeJson(c.root + "/card-flags-summary.json",
              {{"scope", "bounded PhotoCard Pick/Reject caption localization only"},
               {"official_vis007_pass", false},
               {"official_vis008_pass", false},
               {"locales", observedLocales},
               {"assets", 2},
               {"russian_literal_pick", QString::fromUtf8(locales[1].pick)},
               {"russian_literal_reject", QString::fromUtf8(locales[1].reject)},
               {"a_revision_before_history", static_cast<qint64>(protectedAssets[0].revision)},
               {"a_revision_after_undo", static_cast<qint64>(afterUndoExpected[0].revision)},
               {"a_revision_after_redo", static_cast<qint64>(afterRedoExpected[0].revision)},
               {"b_revision_unchanged", static_cast<qint64>(protectedAssets[1].revision)},
               {"a_full_document_after_undo", fullDocument(afterUndoExpected[0].asset.edits)},
               {"b_full_document_unchanged", fullDocument(protectedAssets[1].asset.edits)},
               {"final_document", fullDocument(c.editor.currentSettings())}});
    return true;
}
} // namespace ux
