#include "apps/shared/PhotoDecoder.h"
#include "apps/shared/PresetStore.h"
#include "core/catalog/Catalog.h"
#include "core/render/CpuRenderer.h"
#include "tests/ux/QueuedCallGate.h"
#include "tests/ux/UxContext.h"
#include <QColor>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QSignalSpy>
#include <QUuid>
#include <algorithm>
#include <cstdint>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <sqlite3.h>
#include <stdexcept>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace ux {
namespace {
QByteArray bytes(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read private stress evidence: " + path);
    return file.readAll();
}
QString sha(const QString &path) {
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256).toHex());
}
QImage samples(QImage image) {
    image = image.convertToFormat(QImage::Format_RGBA8888);
    image.setColorSpace({});
    image.setDevicePixelRatio(1);
    return image;
}
QString pixelSHA(const QImage &value) {
    const auto image = samples(value);
    require(!image.isNull(), "Cannot hash an absent stress pixel oracle");
    const auto data =
        QByteArray(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes());
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
QImage reference(const luma::AssetSnapshot &snapshot) {
    const auto decoded = luma::PhotoDecoder().decode(snapshot.asset.path);
    require(decoded.image.width <= 512 && decoded.image.height <= 512 &&
                decoded.image.alpha.empty(),
            "Stress PNG oracle requires the actual small opaque private fixture originals");
    const auto pixels =
        luma::renderCPU(decoded.image, snapshot.asset.edits, decoded.metadata.orientation, {}, {},
                        decoded.calibration, luma::DetailDiagnostic::none, 1, decoded.metadata);
    return QImage(pixels.rgba.data(), pixels.width, pixels.height,
                  static_cast<qsizetype>(pixels.width) * 4, QImage::Format_RGBA8888)
        .copy();
}
QJsonObject document(const luma::EditDocument &value) {
    const auto encoded = QJsonDocument::fromJson(
        luma::encodePreset({"Export stress document", {}, value, luma::allSettingsGroups}));
    require(encoded.isObject(), "Cannot serialize the complete typed stress document");
    return encoded.object();
}
// This is durable TEST evidence, never a claim that production export queues
// have restart persistence (ADR0018 explicitly makes them session-local).
class Replay {
  public:
    explicit Replay(const QString &path) : file_(path) {
        require(!QFileInfo::exists(path) && !QFileInfo(path).isSymLink() &&
                    file_.open(QIODevice::WriteOnly | QIODevice::Truncate),
                "Cannot create private export stress replay journal");
        const auto parent = QFile::encodeName(QFileInfo(path).absolutePath());
        const int descriptor = ::open(parent.constData(), O_RDONLY);
        const bool directorySynced = descriptor >= 0 && ::fsync(descriptor) == 0;
        if (descriptor >= 0)
            ::close(descriptor);
        require(directorySynced, "Cannot durably create the private export stress journal entry");
    }
    void append(QJsonObject event) {
        event["ordinal"] = ++ordinal_;
        const auto data = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
        require(file_.write(data) == data.size() && file_.flush() &&
                    ::fsync(static_cast<int>(file_.handle())) == 0,
                "Cannot durably append the exact export stress replay boundary");
    }

  private:
    QFile file_;
    int ordinal_ = 0;
};
QJsonArray query(sqlite3 *database, const char *sql) {
    sqlite3_stmt *statement = nullptr;
    const int result = sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> prepared(statement,
                                                                              sqlite3_finalize);
    require(result == SQLITE_OK, "Cannot prepare durable production import-journal observation");
    QJsonArray rows;
    int status = SQLITE_OK;
    while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
        QJsonObject row;
        for (int column = 0; column < sqlite3_column_count(statement); ++column) {
            const auto name = QString::fromUtf8(sqlite3_column_name(statement, column));
            const int type = sqlite3_column_type(statement, column);
            if (type == SQLITE_NULL)
                row[name] = QJsonValue(QJsonValue::Null);
            else if (type == SQLITE_INTEGER)
                row[name] = static_cast<qint64>(sqlite3_column_int64(statement, column));
            else if (type == SQLITE_TEXT)
                row[name] = QString::fromUtf8(
                    reinterpret_cast<const char *>(sqlite3_column_text(statement, column)),
                    sqlite3_column_bytes(statement, column));
            else
                require(false,
                        "Unexpected noninteger/nontext production import-journal field type");
        }
        rows.append(row);
    }
    require(status == SQLITE_DONE,
            "Production journal observation stopped before the complete item set");
    return rows;
}
QJsonObject importJournal(Context &c) {
    sqlite3 *raw = nullptr;
    const int opened = sqlite3_open_v2(c.catalogFile.toUtf8().constData(), &raw,
                                       SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
    require(opened == SQLITE_OK && sqlite3_busy_timeout(raw, 1000) == SQLITE_OK,
            "Cannot open the actual isolated import journal read-only");
    require(sqlite3_exec(raw, "BEGIN", nullptr, nullptr, nullptr) == SQLITE_OK,
            "Cannot consistently snapshot actual import sessions/items");
    const auto sessions = query(raw, "SELECT * FROM import_sessions ORDER BY id");
    const auto items = query(raw, "SELECT * FROM import_items ORDER BY session_id,position");
    require(sqlite3_exec(raw, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK,
            "Cannot complete the read-only import-journal snapshot");
    return {{"sessions", sessions}, {"items", items}};
}
struct Fixture {
    std::vector<luma::AssetSnapshot> ordered;
    std::vector<std::string> selected;
    QJsonObject journal;
    QString activeID, activeSource;
    luma::EditDocument activeDocument;
    bool canUndo = false, canRedo = false;
};
void ownedDirectory(Context &c, const QString &path) {
    require(path.startsWith(c.root + '/') && QDir().mkpath(path) && !QFileInfo(path).isSymLink() &&
                QFileInfo(path).canonicalFilePath().startsWith(
                    QFileInfo(c.root).canonicalFilePath() + '/'),
            "Stress directory is not exclusively inside the case's private root");
}
Fixture prepare(Context &c, int count, Replay &replay) {
    auto &d = *c.driver;
    require(c.library.count() == 0 && !c.editor.hasPhoto(),
            "Export stress requires a fresh isolated catalog");
    const auto directory = c.root + "/owned-originals";
    ownedDirectory(c, directory);
    const auto names = QDir(c.fixture).entryList({"Photo-*.png"}, QDir::Files, QDir::Name);
    require(names.size() >= count,
            "Insufficient read-only public images for the complete stress workload");
    QList<QUrl> sources;
    QSet<QString> privateHashes;
    for (int index = 0; index < count; ++index) {
        const auto path = directory + '/' + names[index];
        require(QFile::copy(c.fixture + '/' + names[index], path) && !QFileInfo(path).isSymLink(),
                "Cannot create an independent private stress original");
        // Public generator colors repeat every 256 indexes. Distinguish all 500
        // own fixtures without ever changing shared/public source bytes.
        QImage own(path);
        require(!own.isNull() && own.width() >= 2 && own.height() >= 1,
                "Private stress fixture has no room for its deterministic identity marker");
        own.setPixelColor(0, 0, QColor(index & 255, (index >> 8) & 255, 167, 255));
        own.setPixelColor(1, 0, QColor(93, index & 255, (index >> 8) & 255, 255));
        require(own.save(path, "PNG"), "Cannot encode the exclusively-owned stress fixture marker");
        const auto checksum = sha(path);
        require(!privateHashes.contains(checksum),
                "Private stress originals are not byte-distinct");
        privateHashes.insert(checksum);
        sources.push_back(QUrl::fromLocalFile(path));
        if ((index + 1) % 16 == 0)
            d.frame();
    }
    require(d.object("library.importDialog")->setProperty("sources", QVariant::fromValue(sources)),
            "Actual Import pane refused the known file-panel selection result");
    d.click("library.importButton");
    d.wait([&] { return d.object("library.importDialog")->property("opened").toBool(); },
           "private stress import dialog opened");
    d.choose("library.importMode", 0);
    if (d.object("library.importDuplicates")->property("checked").toBool())
        d.click("library.importDuplicates");
    d.click("library.importStart");
    d.wait(
        [&] { return !c.library.working() && !c.library.loading() && c.library.count() == count; },
        "every private stress original imported durably", 60000);
    d.wait([&] { return !d.object("library.importDialog")->property("visible").toBool(); },
           "Private stress Import dialog fully closed before card input");
    require(c.library.error().isEmpty(), c.library.error());
    // One actual multi-group preset supplies a non-default complete saved
    // document without writing documents/controller state as fixture shortcuts.
    d.doubleClick(c.card(0));
    c.loaded();
    const auto before = c.editor.currentSettings();
    const auto preset = c.presets.snapshot("builtin:landscape");
    const auto expected =
        luma::mergeSettings(before, preset.settings, preset.groups, preset.parameters);
    d.click("filter_landscape");
    d.move("develop.preview", {.5, .5});
    require(c.editor.currentSettings() == expected && expected != before,
            "Actual preset input did not establish the expected complete non-default document");
    c.saved();
    d.click("main.libraryButton");
    d.wait(
        [&] { return !c.library.loading() && !c.library.working() && c.library.count() == count; },
        "stress Library model restored");
    d.click("library.selectAll");
    d.wait([&] { return !c.library.selecting() && c.library.selectedCount() == count; },
           "whole real stress queue selected");
    Fixture fixture;
    fixture.selected = c.library.selectionIDs();
    fixture.activeID = c.editor.photoID();
    fixture.activeSource = c.editor.sourcePath();
    fixture.activeDocument = c.editor.currentSettings();
    fixture.canUndo = c.editor.canUndo();
    fixture.canRedo = c.editor.canRedo();
    luma::Catalog catalog(c.catalogFile.toStdString());
    require(catalog.libraryCount() == count &&
                fixture.selected.size() == static_cast<std::size_t>(count),
            "Stress model/selection/durable catalog cardinalities differ");
    QSet<QString> identities, filenames;
    QMap<QString, luma::AssetSnapshot> snapshots;
    const auto owned = QFileInfo(directory).canonicalFilePath() + '/';
    for (const auto &id : fixture.selected) {
        const auto saved = catalog.assetSnapshot(id);
        if (!saved)
            throw std::runtime_error("Selected stress UUID has no durable saved snapshot");
        const auto asset = QString::fromStdString(id);
        const auto path = QString::fromStdString(saved->asset.path.string());
        const auto name = QFileInfo(path).fileName();
        require(!QUuid(asset).isNull() && !identities.contains(asset) &&
                    !filenames.contains(name) && path.startsWith(owned) &&
                    QFileInfo(path).isFile() && !QFileInfo(path).isSymLink() &&
                    sha(path).toStdString() == saved->asset.checksum,
                "Stress asset UUID/source ownership/SHA or intended output naming is invalid");
        identities.insert(asset);
        filenames.insert(name);
        snapshots.insert(asset, *saved);
        fixture.ordered.push_back(*saved);
        if (fixture.ordered.size() % 16 == 0)
            d.frame();
    }
    std::sort(fixture.ordered.begin(), fixture.ordered.end(), [](const auto &a, const auto &b) {
        return std::tie(a.asset.path, a.asset.id) < std::tie(b.asset.path, b.asset.id);
    });
    fixture.journal = importJournal(c);
    const auto sessions = fixture.journal["sessions"].toArray();
    const auto items = fixture.journal["items"].toArray();
    require(sessions.size() == 1 && items.size() == count &&
                sessions[0].toObject()["state"].toString() == "completed" &&
                !sessions[0].toObject()["finished_at"].isNull() &&
                !QUuid(sessions[0].toObject()["id"].toString()).isNull(),
            "Complete private import has no genuine terminal durable journal");
    QSet<QString> journalAssets;
    QSet<int> journalPositions;
    for (const auto &value : items) {
        const auto item = value.toObject();
        const auto asset = item["asset_id"].toString();
        const int position = item["position"].toInt(-1);
        require(item["state"].toString() == "committed" && identities.contains(asset) &&
                    !journalAssets.contains(asset) && position >= 0 && position < count &&
                    !journalPositions.contains(position) &&
                    item["session_id"] == sessions[0].toObject()["id"] &&
                    item["error"].toString().isEmpty(),
                "Private setup journal has a pending/failed/duplicate/missing-asset child");
        const auto &saved = snapshots[asset];
        const auto source = QString::fromStdString(saved.asset.path.string());
        require(item["source_path"].toString() == source &&
                    item["target_path"].toString() == source &&
                    item["checksum"].toString() == QString::fromStdString(saved.asset.checksum) &&
                    item["filesize"].toInteger(-1) == QFileInfo(source).size(),
                "Durable import child does not describe its exact private UUID/source/SHA/size");
        journalAssets.insert(asset);
        journalPositions.insert(position);
    }
    require(journalAssets == identities,
            "Durable import journal does not enumerate the exact selected UUID set");
    QJsonArray assets;
    for (const auto &saved : fixture.ordered)
        assets.append(QJsonObject{{"uuid", QString::fromStdString(saved.asset.id)},
                                  {"source", QString::fromStdString(saved.asset.path.string())},
                                  {"source_sha256", QString::fromStdString(saved.asset.checksum)},
                                  {"revision", static_cast<qint64>(saved.revision)},
                                  {"complete_document", document(saved.asset.edits)}});
    const QJsonObject manifest{{"assets", assets},
                               {"production_import_journal", fixture.journal},
                               {"production_export_queue_persistence", "session-local"}};
    writeJson(c.root + "/export-stress-manifest.json", manifest);
    replay.append({{"action", "private_fixture_durable_manifest_ready"},
                   {"actual_assets", count},
                   {"full_documents", true},
                   {"source_sha_verified", true},
                   {"actual_import_journal", true},
                   {"manifest", manifest},
                   {"manifest_file_sha256", sha(c.root + "/export-stress-manifest.json")}});
    return fixture;
}
void unchanged(Context &c, const Fixture &fixture) {
    luma::Catalog catalog(c.catalogFile.toStdString());
    require(
        catalog.libraryCount() == static_cast<std::int64_t>(fixture.ordered.size()) &&
            c.library.count() == static_cast<int>(fixture.ordered.size()) &&
            c.library.selectionIDs() == fixture.selected && !c.library.working() &&
            !c.library.selecting() && c.editor.photoID() == fixture.activeID &&
            c.editor.sourcePath() == fixture.activeSource &&
            c.editor.currentSettings() == fixture.activeDocument &&
            c.editor.canUndo() == fixture.canUndo && c.editor.canRedo() == fixture.canRedo &&
            !c.editor.busy() && !c.editor.saving() && !c.editor.editingLocked(),
        "Export stress altered active document/history/selection or retained an operation lock");
    std::size_t checked = 0;
    for (const auto &before : fixture.ordered) {
        const auto after = catalog.assetSnapshot(before.asset.id);
        require(
            after && after->asset.id == before.asset.id && after->asset.path == before.asset.path &&
                after->asset.checksum == before.asset.checksum &&
                after->revision == before.revision && after->asset.edits == before.asset.edits &&
                sha(QString::fromStdString(after->asset.path.string())).toStdString() ==
                    before.asset.checksum,
            "Export stress changed a full saved document, revision, UUID, original path or source "
            "bytes");
        if (++checked % 16 == 0)
            c.driver->frame();
    }
    const auto active = catalog.activeAsset();
    require(
        active && active->id == fixture.activeID.toStdString() &&
            active->edits == fixture.activeDocument && importJournal(c) == fixture.journal &&
            catalog.unfinishedImports().empty(),
        "Export stress changed durable active/import state or left an unfinished import journal");
}
void noTemps(Context &c) {
    QDirIterator iterator(c.root + "/exports", QDir::Files | QDir::Hidden,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const auto path = iterator.next();
        const auto name = QFileInfo(path).fileName();
        require(!name.startsWith(".luma-") && !name.endsWith(".tmp"),
                "Orphan/incomplete export temp: " + path);
    }
}
void options(Context &c, const QString &directory) {
    const auto settings = c.exporter.settings();
    require(
        settings.value("format").toInt() == 1 && settings.value("colorSpace").toInt() == 0 &&
            settings.value("resize").toInt() == 0 && settings.value("outputMedium").toInt() == 0 &&
            settings.value("watermarkKind").toInt() == 0 &&
            settings.value("metadata").toInt() == 0 && settings.value("conflict").toInt() == 1 &&
            settings.value("naming").toString() == "{filename}" &&
            QFileInfo(settings.value("destination").toString()).canonicalFilePath() ==
                QFileInfo(directory).canonicalFilePath(),
        "Actual committed export options do not match the independent PNG snapshot oracle");
}
void settings(Context &c, const QString &directory) {
    auto &d = *c.driver;
    ownedDirectory(c, directory);
    require(QDir(directory).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(),
            "Each stress queue must start in an empty exclusively-owned destination");
    d.click("exportButton");
    d.wait([&] { return d.object("exportPanel")->property("opened").toBool(); },
           "stress Export settings opened");
    d.type("export.destination", directory + "/./");
    d.type("exportNaming", "{filename}");
    d.choose("export.conflict", 1);
    d.choose("exportFormat", 1);
}
QString output(const QString &directory, const luma::AssetSnapshot &asset) {
    return directory + '/' + QString::fromStdString(asset.asset.path.filename().string());
}
bool completion(Context &c, const Fixture &fixture, int index) {
    if (index < 0 || index >= static_cast<int>(fixture.ordered.size()))
        return false;
    return c.exporter.working() && !c.exporter.waitingForSave() && !c.exporter.paused() &&
           c.exporter.total() == static_cast<int>(fixture.ordered.size()) &&
           c.exporter.completed() == index && c.exporter.succeeded() == index &&
           c.exporter.progress() == 1 && !c.editor.editingLocked() &&
           c.exporter.currentFile() ==
               QString::fromStdString(
                   fixture.ordered[static_cast<std::size_t>(index)].asset.path.filename().string());
}
QJsonObject verifiedFile(const QString &directory, const luma::AssetSnapshot &asset) {
    const auto path = output(directory, asset);
    const auto expected = reference(asset);
    require(QFileInfo(path).isFile() && !QFileInfo(path).isSymLink() &&
                samples(QImage(path)) == expected,
            "Committed stress PNG does not contain its exact UUID/source/full-document snapshot "
            "pixels");
    return {{"uuid", QString::fromStdString(asset.asset.id)},
            {"output", path},
            {"source_sha256", QString::fromStdString(asset.asset.checksum)},
            {"complete_document", document(asset.asset.edits)},
            {"output_sha256", sha(path)},
            {"reference_rgba_sha256", pixelSHA(expected)}};
}
struct Random {
    std::uint32_t state;
    std::uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};
// Observe genuine completion MetaCalls, without replacing the production worker.
// All default opaque PNG final progress(1) notifications precede their completion
// in FIFO order on the one real export worker, as in ExportExtraScenarios.
class PauseScript final : public QObject {
  public:
    PauseScript(Context &c, const Fixture &fixture, QString directory, const QMap<int, int> &plan,
                Replay &replay)
        : c_(c), fixture_(fixture), directory_(std::move(directory)), plan_(plan), replay_(replay) {
        c_.exporter.installEventFilter(this);
    }
    ~PauseScript() override {
        c_.exporter.removeEventFilter(this);
    }
    int observed = 0;
    std::exception_ptr failure;
    QMap<QString, QString> retained;

  private:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object != &c_.exporter || event->type() != QEvent::MetaCall || inside_ || failure ||
            !completion(c_, fixture_, observed))
            return false;
        inside_ = true;
        try {
            options(c_, directory_);
            require(c_.exporter.skipped() == 0 && c_.exporter.failures().isEmpty() &&
                        c_.exporter.error().isEmpty(),
                    "Stress queue failed/skipped before its claimed completion boundary");
            auto file =
                verifiedFile(directory_, fixture_.ordered[static_cast<std::size_t>(observed)]);
            retained.insert(file["output"].toString(), file["output_sha256"].toString());
            file["action"] = "real_file_committed_before_completion_delivery";
            file["queue_index"] = observed;
            replay_.append(file);
            if (plan_.contains(observed + 1)) {
                c_.driver->click("export.pause");
                require(c_.exporter.pausePending() && !c_.exporter.paused() &&
                            completion(c_, fixture_, observed),
                        "Real Pause did not request pause after this genuine committed file");
                replay_.append({{"action", "real_pause_clicked_before_completion"},
                                {"after_file", observed + 1}});
            }
            ++observed;
        } catch (...) {
            failure = std::current_exception();
        }
        inside_ = false;
        return false;
    }
    Context &c_;
    const Fixture &fixture_;
    QString directory_;
    const QMap<int, int> &plan_;
    Replay &replay_;
    bool inside_ = false;
};
void randomPause500(Context &c) {
    auto &d = *c.driver;
    Replay replay(c.root + "/export-stress-replay.ndjson");
    const auto fixture = prepare(c, 500, replay);
    constexpr std::uint32_t seed = 0xe0235eedU;
    Random random{seed};
    QMap<int, int> plan;
    QJsonArray schedule;
    for (int file = 1; file < 500; ++file)
        if (random.next() % 7 == 0) {
            const int frames = 1 + static_cast<int>(random.next() % 4);
            plan.insert(file, frames);
            schedule.append(
                QJsonObject{{"pause_after_file", file}, {"paused_polish_frames", frames}});
        }
    require(plan.size() >= 2,
            "Fixed seeded stress schedule did not contain multiple real pause/resume boundaries");
    replay.append({{"action", "fixed_seed_plan"},
                   {"generator", "xorshift32-13-17-5"},
                   {"seed_hex", "e0235eed"},
                   {"actual_files", 500},
                   {"schedule", schedule}});
    const auto directory = c.root + "/exports/queue-500";
    settings(c, directory);
    QSignalSpy pauses(d.object("export.pause"), SIGNAL(clicked()));
    QSignalSpy resumes(d.object("export.resume"), SIGNAL(clicked()));
    QSignalSpy starts(d.object("export.selection"), SIGNAL(clicked()));
    require(pauses.isValid() && resumes.isValid() && starts.isValid(),
            "Cannot observe the actual export stress control clicks");
    PauseScript script(c, fixture, directory, plan, replay);
    d.click("export.selection");
    QSet<int> handled;
    while (true) {
        d.wait([&] { return script.failure || c.exporter.paused() || !c.exporter.working(); },
               "seeded queue reached its next actual pause or terminal state", 60000);
        if (script.failure)
            std::rethrow_exception(script.failure);
        if (!c.exporter.working())
            break;
        const int done = c.exporter.completed();
        require(done > 0 && done < 500 && done == script.observed &&
                    c.exporter.succeeded() == done && plan.contains(done) &&
                    !handled.contains(done) && c.library.selectionIDs() == fixture.selected &&
                    !QFileInfo::exists(
                        output(directory, fixture.ordered[static_cast<std::size_t>(done)])),
                "Pause did not stop at its exact random planned committed-file checkpoint");
        handled.insert(done);
        noTemps(c);
        replay.append({{"action", "actual_pause_settled"},
                       {"completed", done},
                       {"total", 500},
                       {"next_output_absent", true},
                       {"dwell_frames", plan[done]}});
        for (int frame = 0; frame < plan[done]; ++frame) {
            d.frame();
            require(c.exporter.paused() && c.exporter.completed() == done &&
                        QDir(directory).entryList({"*.png"}, QDir::Files).size() == done &&
                        !QFileInfo::exists(
                            output(directory, fixture.ordered[static_cast<std::size_t>(done)])),
                    "Real paused queue advanced, skipped or created an unexpected next output");
        }
        const auto before = resumes.count();
        replay.append({{"action", "real_resume_requested"}, {"resumed_after_file", done}});
        d.click("export.resume");
        require(resumes.count() == before + 1,
                "Actual Resume input was not acknowledged exactly once");
        replay.append({{"action", "real_resume_clicked"}, {"resumed_after_file", done}});
    }
    require(starts.count() == 1 && script.observed == 500 && script.retained.size() == 500 &&
                handled.size() == plan.size() && pauses.count() == plan.size() &&
                resumes.count() == plan.size() && c.exporter.total() == 500 &&
                c.exporter.completed() == 500 && c.exporter.succeeded() == 500 &&
                c.exporter.skipped() == 0 && c.exporter.failures().isEmpty() &&
                c.exporter.error().isEmpty() &&
                QDir(directory).entryList({"*.png"}, QDir::Files).size() == 500,
            "Seeded stress did not execute the complete 500-file queue and every scheduled real "
            "pause/resume");
    for (auto entry = script.retained.cbegin(); entry != script.retained.cend(); ++entry)
        require(sha(entry.key()) == entry.value(),
                "A later pause/resume overwrote a previously committed PNG");
    noTemps(c);
    unchanged(c, fixture);
    c.checkData();
    replay.append({{"action", "stress_500_complete"},
                   {"exact_outputs", 500},
                   {"actual_pauses", pauses.count()},
                   {"actual_resumes", resumes.count()},
                   {"all_full_documents_uuids_sources_journal_unchanged", true},
                   {"orphan_export_temps", 0}});
}
void hundredCancelCycles(Context &c) {
    auto &d = *c.driver;
    Replay replay(c.root + "/export-stress-replay.ndjson");
    const auto fixture = prepare(c, 10, replay);
    const auto expected = reference(fixture.ordered.front());
    QSignalSpy starts(d.object("export.selection"), SIGNAL(clicked()));
    QSignalSpy cancels(d.object("export.cancel"), SIGNAL(clicked()));
    QSignalSpy closeReady(&c.exporter, &ExportController::closeReady);
    require(starts.isValid() && cancels.isValid() && closeReady.isValid(),
            "Cannot observe actual 100-cycle Start/Cancel/close inputs");
    bool wasWorking = false;
    int idleTransitions = 0;
    QObject lifetime;
    QObject::connect(&c.exporter, &ExportController::changed, &lifetime, [&] {
        const bool now = c.exporter.working();
        if (wasWorking && !now)
            ++idleTransitions;
        wasWorking = now;
    });
    QMap<QString, QString> retained;
    for (int cycle = 0; cycle < 100; ++cycle) {
        const auto directory = c.root + QString("/exports/cycle-%1").arg(cycle, 3, 10, QChar('0'));
        settings(c, directory);
        const auto beforeStart = starts.count();
        const auto beforeCancel = cancels.count();
        const int beforeIdle = idleTransitions;
        QString checkpointHash;
        QueuedCallGate gate(
            c.exporter, [&] { return completion(c, fixture, 0); },
            [&] {
                d.wait([&] { return !d.object("exportPanel")->property("visible").toBool(); },
                       "Export settings fully closed before the held completion's Cancel input");
                options(c, directory);
                const auto checkpoint = output(directory, fixture.ordered.front());
                require(samples(QImage(checkpoint)) == expected &&
                            !QFileInfo::exists(output(directory, fixture.ordered[1])),
                        "Start/Cancel cycle missed its genuine first committed and "
                        "unstarted-second boundary");
                checkpointHash = sha(checkpoint);
                auto file = verifiedFile(directory, fixture.ordered.front());
                file["action"] = "cycle_real_worker_checkpoint_before_cancel";
                file["cycle"] = cycle + 1;
                replay.append(file);
                d.click("export.cancel");
                require(
                    cancels.count() == beforeCancel + 1 && c.exporter.working() &&
                        c.exporter.completed() == 0 && idleTransitions == beforeIdle,
                    "Actual Cancel did not target this still-pending genuine worker completion");
            });
        replay.append(
            {{"action", "cycle_start_requested"}, {"cycle", cycle + 1}, {"directory", directory}});
        d.click("export.selection");
        d.wait([&] { return gate.caught; },
               "actual committed first-file callback before this cycle's Cancel", 60000);
        if (gate.failure)
            std::rethrow_exception(gate.failure);
        d.wait([&] { return !c.exporter.working(); },
               "actual cycle cancellation reached one clean terminal state", 60000);
        const auto checkpoint = output(directory, fixture.ordered.front());
        require(starts.count() == beforeStart + 1 && cancels.count() == beforeCancel + 1 &&
                    idleTransitions == beforeIdle + 1 && c.exporter.total() == 10 &&
                    c.exporter.completed() == 1 && c.exporter.succeeded() == 1 &&
                    c.exporter.skipped() == 0 && c.exporter.failures().isEmpty() &&
                    c.exporter.error().isEmpty() && c.exporter.status().startsWith("Cancelled") &&
                    closeReady.isEmpty() && c.window->isVisible() &&
                    QDir(directory).entryList({"*.png"}, QDir::Files).size() == 1 &&
                    !checkpointHash.isEmpty() && sha(checkpoint) == checkpointHash &&
                    samples(QImage(checkpoint)) == expected,
                "One of the 100 actual cycles lost its committed output, repeated completion, "
                "resumed another job or closed the app");
        retained.insert(checkpoint, checkpointHash);
        noTemps(c);
        unchanged(c, fixture);
        replay.append({{"action", "cycle_cancelled_cleanly"},
                       {"cycle", cycle + 1},
                       {"completed", 1},
                       {"actual_start_clicks", starts.count()},
                       {"actual_cancel_clicks", cancels.count()},
                       {"retained_output", checkpoint},
                       {"retained_sha256", checkpointHash},
                       {"orphan_export_temps", 0},
                       {"full_documents_uuids_sources_journal_unchanged", true}});
    }
    require(
        starts.count() == 100 && cancels.count() == 100 && idleTransitions == 100 &&
            retained.size() == 100 &&
            QDir(c.root + "/exports")
                    .entryList({"cycle-*"}, QDir::Dirs | QDir::NoDotAndDotDot)
                    .size() == 100,
        "Start/Cancel stress shortened, merged or silently retried the required 100 actual cycles");
    for (auto file = retained.cbegin(); file != retained.cend(); ++file)
        require(sha(file.key()) == file.value(),
                "A later cycle damaged an earlier legitimate committed checkpoint");
    noTemps(c);
    unchanged(c, fixture);
    c.checkData();
    replay.append({{"action", "stress_100_cycles_complete"},
                   {"actual_starts", 100},
                   {"actual_cancels", 100},
                   {"genuine_worker_checkpoints", 100},
                   {"retained_pngs", 100},
                   {"orphan_export_temps", 0}});
}
} // namespace
bool exportStressScenario(Context &c, const QString &id) {
    if (id == "UX-EXP-023")
        randomPause500(c);
    else if (id == "UX-EXP-024")
        hundredCancelCycles(c);
    else
        return false;
    return true;
}
} // namespace ux
