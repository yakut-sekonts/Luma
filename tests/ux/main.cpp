#include "tests/ux/HistoryChaos.h"
#include "tests/ux/UxContext.h"
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQuickStyle>
#include <QRegularExpression>
#include <cstdio>
#include <iostream>
#include <optional>
namespace ux {
bool updaterScenario(Context &, const QString &, const QString &);
bool autoTransformRaceCandidate(Context &, const QString &);
bool cardFlagLocalizationCandidate(Context &, const QString &);
bool localizedDialogCandidate(Context &, const QString &);
bool exportWatermarkSnapshotMissingCandidate(Context &, const QString &);
bool exportCurrentFilenameLegalCandidate(Context &, const QString &);
bool lib004ActualResponseCandidate(Context &, const QString &);
bool lib005ActualResponseCandidate(Context &, const QString &);
bool cropDoneResidueCandidate(Context &, const QString &);
bool race002ActualResponseCandidate(Context &, const QString &);
bool appDeactivationCandidate(Context &, const QString &);
bool race007PartialRenderCandidate(Context &, const QString &);
bool maskHandleTransparentCandidate(Context &, const QString &);
bool minimizeExportCandidate(Context &, const QString &);
bool copyDestinationScenario(Context &, const QString &, const QString &);
bool removableDestinationScenario(Context &, const QString &, const QString &);
bool mergeDailyScenario(Context &, const QString &);
bool offlineDailyScenario(Context &);
bool dailyCullingScenario(Context &);
bool migrationDataScenario(Context &);
void prepareOfflineModelFixture(const QString &);
void prepareThumbnailCorruption(const QString &, const QString &);
void prepareMissingStartupSource(const QString &, const QString &);
} // namespace ux
namespace {
struct ShutdownMarker {
    const char *phase;
    ~ShutdownMarker() noexcept {
        std::fprintf(stderr, "UX_SHUTDOWN phase=%s\n", phase);
        std::fflush(stderr);
    }
};
} // namespace
int main(int argc, char **argv) {
    ShutdownMarker applicationDestroyed{"qguiapplication.owner.destroy.end"};
    QGuiApplication app(argc, argv);
    ShutdownMarker applicationDestroying{"qguiapplication.owner.destroy.begin"};
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("Luma UX");
    app.setApplicationVersion(LUMA_VERSION);
    QQuickStyle::setStyle("Basic");
    QCommandLineParser parser;
    parser.addOption({"root", "Isolated public fixture evidence directory", "directory"});
    parser.addOption({"fixtures", "Generated fixture directory", "directory"});
    parser.addOption({"qml", "Production QML directory", "directory"});
    parser.addOption({"case", "Specification scenario ID", "id"});
    parser.addOption({"variant", "Isolated operation variant", "variant"});
    parser.addOption({"stage", "Multi-process test stage", "stage"});
    parser.addOption({"state-root", "Shared state for a process restart test", "directory"});
    parser.addOption({"seed", "Scoped UND-017 unsigned decimal seed", "integer"});
    parser.addOption({"actions", "Exact UND-017 semantic action count", "integer"});
    parser.addOption({"replay", "Immutable UND-017 replay schedule", "path"});
    parser.process(app);
    const auto root = parser.value("root");
    QDir().mkpath(root);
    if (parser.isSet("state-root"))
        QDir().mkpath(parser.value("state-root"));
    // Reverse destruction brackets the unchanged unique_ptr cleanup, including members.
    ShutdownMarker contextDestroyed{"context.owner.destroy.end"};
    std::unique_ptr<ux::Context> context;
    ShutdownMarker contextDestroying{"context.owner.destroy.begin"};
    try {
        const auto id = parser.value("case");
        if (id == "UX-P10-DAY-009" || id == "UX-P10-DAY-003")
            ux::require(!parser.isSet("variant") && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "Daily workflow requires fresh owned state");
        if (id == "UX-P10-DAY-012") {
            ux::require(!parser.isSet("variant") && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "Offline workflow requires fresh owned state without variants");
            ux::prepareOfflineModelFixture(root);
        }
        std::optional<ux::HistoryChaosPlan> chaos;
        if (id == "UX-UND-017") {
            ux::require(parser.isSet("seed") && parser.isSet("actions") && !parser.isSet("stage") &&
                            !parser.isSet("state-root") && !parser.isSet("variant"),
                        "UND-017 requires explicit seed/actions and forbids stage/state/variant");
            const auto seedText = parser.value("seed");
            const auto countText = parser.value("actions");
            bool validSeed = false, validCount = false;
            const auto seed = seedText.toULongLong(&validSeed, 10);
            const auto count = countText.toInt(&validCount, 10);
            ux::require(
                validSeed && validCount &&
                    QRegularExpression(QRegularExpression::anchoredPattern("(0|[1-9][0-9]{0,19})"))
                        .match(seedText)
                        .hasMatch() &&
                    QRegularExpression(QRegularExpression::anchoredPattern("[1-9][0-9]{0,5}"))
                        .match(countText)
                        .hasMatch() &&
                    count >= 1 && count <= 100000,
                "UND-017 seed/count is invalid or exceeds the bounded evidence contract");
            // Actual Qt timer progress before any QML window/Context is created.
            // The deterministic generator yields to this timer, never fakes a beat.
            ux::Metrics preflightHeartbeat(root + "/heartbeat.json");
            chaos = ux::prepareHistoryChaosPlan(root, parser.value("fixtures"), seed, count,
                                                parser.value("replay"));
            std::cerr << "CHAOS preContext seed=" << seed << " actions=" << count
                      << " schedule_sha256=" << chaos->scheduleSHA256.toStdString() << '\n';
        } else {
            ux::require(
                !parser.isSet("seed") && !parser.isSet("actions") && !parser.isSet("replay"),
                "seed/actions/replay are supported only by UND-017; refusing ignored flags");
        }
        if (id == "UX-P10-UPG-012") {
            const QStringList variants{"save",  "import", "export",
                                       "batch", "merge",  "merge-cancel"};
            ux::require(parser.values("variant").size() == 1 &&
                            variants.contains(parser.value("variant")) && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "Update guard requires one operation variant without shared state");
        }
        if (id == "UX-P10-DAY-011") {
            ux::require(
                parser.values("variant").size() == 1 &&
                    (parser.value("variant") == "hdr" || parser.value("variant") == "panorama") &&
                    !parser.isSet("stage") && !parser.isSet("state-root"),
                "Daily derived workflow requires one HDR/Panorama variant without shared state");
        }
        if (id == "UX-MAC-008") {
            ux::require(
                parser.values("variant").size() == 1 &&
                    (parser.value("variant") == "copy" || parser.value("variant") == "export") &&
                    !parser.isSet("stage") && !parser.isSet("state-root"),
                "MAC-008 requires exactly one copy/export variant and forbids stage/state-root");
        } else if (id == "CANDIDATE-MAC008-COPY-DESTINATION" ||
                   id == "CANDIDATE-MAC008-EXPORT-DESTINATION") {
            ux::require(
                !parser.isSet("variant") && !parser.isSet("stage") && !parser.isSet("state-root"),
                "Historical MAC-008 candidate IDs forbid ignored variant/stage/state-root flags");
        }
        if (id == "CANDIDATE-CRP010-DONE-RESIDUE" || id == "UX-CRP-010")
            ux::require(!parser.isSet("variant") && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "CRP010 Done candidate forbids ignored variant/stage/state-root flags");
        if (id == "CANDIDATE-RACE007-PARTIAL-THUMBNAIL" || id == "UX-RACE-007" ||
            id == "CANDIDATE-MSK023-TRANSPARENT-HANDLE" ||
            id == "CANDIDATE-APP016-MINIMIZED-EXPORT" ||
            id == "CANDIDATE-KEYBOARD-IMPORT-EDIT-EXPORT" ||
            id == "CANDIDATE-LOCALIZED-PRESET-DIALOGS")
            ux::require(!parser.isSet("variant") && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "Partial-render/transparent-handle candidates forbid ignored "
                        "variant/stage/state-root flags");
        if (id == "CANDIDATE-LIB005-AWAY-BACK" || id == "UX-LIB-005")
            ux::require(!parser.isSet("variant") && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "LIB005 forbids ignored variant/stage/state-root flags");
        if (id == "CANDIDATE-VIS003-LEGAL-CURRENT-FILENAME" || id == "UX-VIS-003")
            ux::require(!parser.isSet("variant") && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "VIS003 candidate forbids ignored variant/stage/state-root flags");
        if (id == "CANDIDATE-APP017-SLD009-FINDER-DEACTIVATION")
            ux::require(
                !parser.isSet("variant") && !parser.isSet("stage") && !parser.isSet("state-root"),
                "App deactivation candidate forbids ignored variant/stage/state-root flags");
        if (id == "CANDIDATE-RACE002-THUMBNAIL-INVALIDATION" || id == "UX-RACE-002")
            ux::require(!parser.isSet("variant") && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "RACE002 forbids ignored variant/stage/state-root flags");
        if (id == "CANDIDATE-LIB004-ACTUAL-RESPONSE" || id == "UX-LIB-004")
            ux::require(!parser.isSet("variant") && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "LIB004 forbids ignored variant/stage/state-root flags");
        if (id == "CANDIDATE-EXP028-WATERMARK-SNAPSHOT-MISSING" || id == "UX-EXP-028")
            ux::require(!parser.isSet("variant") && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "EXP028 snapshot scenario forbids ignored variant/stage/state-root flags");
        if (id == "CANDIDATE-PHOTOCARD-FLAG-LOCALIZATION")
            ux::require(!parser.isSet("variant") && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "Card flag localization candidate forbids variant/stage/state-root flags");
        if (id == "CANDIDATE-RACE005-AUTO-COMPLETION" || id == "UX-RACE-005")
            ux::require(!parser.isSet("variant") && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "RACE005 forbids ignored variant/stage/state-root flags");
        if (id == "CANDIDATE-LIBRARY-SELECTION-SCROLL" ||
            id == "CANDIDATE-FILMSTRIP-SELECTION-SCROLL")
            ux::require(!parser.isSet("variant") && !parser.isSet("stage") &&
                            !parser.isSet("state-root"),
                        "Library scroll regression forbids variant/stage/state-root flags");
        if (parser.value("case") == "UX-APP-004" && parser.value("stage") == "verify")
            ux::prepareMissingStartupSource(root, parser.value("state-root"));
        if (parser.value("case") == "UX-REC-009" && parser.value("stage") == "verify")
            ux::prepareThumbnailCorruption(root, parser.value("state-root"));
        const bool updaterProbe = id == "UX-UPD-005" || id == "UX-UPD-006";
        auto stateRoot = parser.value("state-root");
        if ((id == "UX-P10-DAY-010" || id == "UX-P10-DAT-005") &&
            parser.value("stage") == "verify") {
            // Open the new file produced by the actual Restore handler; never copy
            // it over the original live catalog or silently create an empty one.
            stateRoot += "/restored";
            const QFileInfo restored(stateRoot + "/catalog.sqlite");
            ux::require(restored.isFile() && !restored.isSymLink(),
                        "Daily restore did not produce a regular catalog file");
        }
        context = std::make_unique<ux::Context>(root, parser.value("fixtures"), parser.value("qml"),
                                                stateRoot, updaterProbe);
        if (id == "UX-P10-DAT-004")
            ux::require(ux::migrationDataScenario(*context), "Migration data workflow unavailable");
        else if (id == "UX-P10-DAY-003")
            ux::require(ux::dailyCullingScenario(*context), "Daily culling workflow unavailable");
        else if (id == "UX-P10-DAY-012")
            ux::require(ux::offlineDailyScenario(*context), "Offline daily workflow unavailable");
        else if (id == "UX-P10-DAY-011")
            ux::require(ux::mergeDailyScenario(*context, parser.value("variant")),
                        "Daily derived workflow unavailable");
        else if (chaos)
            ux::runHistoryChaos(*context, *chaos);
        else if (id == "CANDIDATE-APP016-MINIMIZED-EXPORT")
            ux::require(ux::minimizeExportCandidate(*context, id),
                        "Native minimized export candidate unavailable");
        else if (id == "CANDIDATE-RACE007-PARTIAL-THUMBNAIL" || id == "UX-RACE-007")
            ux::require(ux::race007PartialRenderCandidate(*context, id),
                        "Actual partial-thumbnail/batch candidate unavailable");
        else if (id == "CANDIDATE-MSK023-TRANSPARENT-HANDLE")
            ux::require(ux::maskHandleTransparentCandidate(*context, id),
                        "Actual uncovered radius handle candidate unavailable");
        else if (id == "CANDIDATE-CRP010-DONE-RESIDUE" || id == "UX-CRP-010")
            ux::require(ux::cropDoneResidueCandidate(*context, id),
                        "Actual Crop Done-only residue candidate unavailable");
        else if (id == "CANDIDATE-VIS003-LEGAL-CURRENT-FILENAME" || id == "UX-VIS-003")
            ux::require(ux::exportCurrentFilenameLegalCandidate(*context, id),
                        "Legal current export filename candidate unavailable");
        else if (id == "CANDIDATE-LIB005-AWAY-BACK" || id == "UX-LIB-005")
            ux::require(ux::lib005ActualResponseCandidate(*context, id),
                        "Actual Library away/back thumbnail candidate unavailable");
        else if (id == "CANDIDATE-APP017-SLD009-FINDER-DEACTIVATION")
            ux::require(ux::appDeactivationCandidate(*context, id),
                        "Real cross-process app deactivation candidate unavailable");
        else if (id == "CANDIDATE-RACE002-THUMBNAIL-INVALIDATION" || id == "UX-RACE-002")
            ux::require(ux::race002ActualResponseCandidate(*context, id),
                        "Actual thumbnail invalidation adapter unavailable");
        else if (id == "CANDIDATE-LIB004-ACTUAL-RESPONSE" || id == "UX-LIB-004")
            ux::require(ux::lib004ActualResponseCandidate(*context, id),
                        "Actual thumbnail response adapter unavailable");
        else if (id == "CANDIDATE-EXP028-WATERMARK-SNAPSHOT-MISSING" || id == "UX-EXP-028")
            ux::require(ux::exportWatermarkSnapshotMissingCandidate(*context, id),
                        "Watermark snapshot missing candidate adapter unavailable");
        else if (id == "CANDIDATE-PHOTOCARD-FLAG-LOCALIZATION")
            ux::require(ux::cardFlagLocalizationCandidate(*context, id),
                        "Card flag localization candidate adapter unavailable");
        else if (id == "CANDIDATE-LOCALIZED-PRESET-DIALOGS")
            ux::require(ux::localizedDialogCandidate(*context, id),
                        "Localized dialog candidate adapter unavailable");
        else if (id == "CANDIDATE-RACE005-AUTO-COMPLETION" || id == "UX-RACE-005")
            ux::require(ux::autoTransformRaceCandidate(*context, id),
                        "Genuine Auto completion regression adapter unavailable");
        else if (id == "UX-MAC-008") {
            const auto variant = parser.value("variant");
            ux::require(variant == "copy" ? ux::copyDestinationScenario(*context, id, variant)
                                          : ux::removableDestinationScenario(*context, id, variant),
                        "Removable destination operation adapter unavailable");
        } else if (parser.isSet("stage"))
            ux::restartScenario(*context, parser.value("case"), parser.value("stage"));
        else if (parser.value("case") == "UX-BAT-013")
            ux::require(ux::updaterScenario(*context, "UX-UPD-003", "batch"),
                        "Batch restart adapter unavailable");
        else if (!ux::updaterScenario(*context, parser.value("case"), parser.value("variant")))
            ux::basicScenario(*context, parser.value("case"));
        context->saved();
        context->checkData();
        ux::require(!context->metrics.json()["hard_hang"].toBool(), "Main-loop hard hang");
        context->finish(parser.value("case"), "PASS");
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        if (context)
            context->finish(parser.value("case"), "FAIL", QString::fromUtf8(failure.what()));
        else
            ux::writeJson(root + "/result.json", {{"id", parser.value("case")},
                                                  {"status", "FAIL"},
                                                  {"message", QString::fromUtf8(failure.what())}});
        return 1;
    }
}
