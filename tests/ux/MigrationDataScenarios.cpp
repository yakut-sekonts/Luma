#include "core/edits/LocalMasks.h"
#include "tests/ux/UxContext.h"
#include <QColorSpace>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

namespace ux {
bool migrationDataScenario(Context &c) {
    auto &d = *c.driver;
    QFile source(c.stateRoot + "/historical-baseline.json");
    require(source.open(QIODevice::ReadOnly), "Historical baseline missing");
    const auto baseline = QJsonDocument::fromJson(source.readAll()).object();
    const auto tables = baseline["tables"].toObject();
    require(baseline["schema"].toInt() == 11, "Migration baseline is not schema 11");
    luma::Catalog catalog(c.catalogFile.toStdString(), luma::CatalogOpenMode::existingOnly);
    auto rows = catalog.libraryPage({}, 0, 4);
    require(rows.size() == 3 && c.library.count() == 3,
            "Migration lost or introduced historical assets");
    std::sort(rows.begin(), rows.end(),
              [](const auto &a, const auto &b) { return a.filename < b.filename; });
    QJsonArray identities;
    int versions = 0;
    for (const auto &row : rows) {
        const auto id = QString::fromStdString(row.id);
        const auto saved = catalog.assetSnapshot(row.id);
        if (!saved)
            throw std::runtime_error("Migrated asset disappeared before opening");
        const auto &snapshot = *saved;
        snapshot.asset.edits.validate();
        bool found = false;
        for (const auto &entry : tables["edit_local_masks"].toObject()["rows"].toArray()) {
            const auto values = entry.toArray();
            if (values[0].toString() != id)
                continue;
            const auto legacy = luma::decodeLocalMasks(values[1].toString().toStdString());
            require(snapshot.asset.edits.localMasks == legacy,
                    "Migration changed decoded historical masks");
            found = true;
        }
        require(found, "Historical asset has no mask baseline");
        for (const auto &version : catalog.editVersions(row.id)) {
            catalog.editVersion(row.id, version.id, version.revision).validate();
            ++versions;
        }
        d.click("main.libraryButton");
        d.wait([&] { return !c.library.loading() && !c.library.working(); },
               "Migrated Library ready");
        d.doubleClick("library.card." + id);
        c.loaded();
        require(c.editor.photoID() == id && c.editor.currentSettings() == snapshot.asset.edits,
                "Opening migrated photo changed its saved document");
        const QImage expected(c.stateRoot + "/originals/" + QString::fromStdString(row.filename) +
                              ".png");
        const QImage output(c.exportPNG(QString::fromStdString(row.filename)));
        require(!expected.isNull() && !output.isNull() && expected.size() == output.size() &&
                    expected.colorSpace() == output.colorSpace() &&
                    output.colorSpace() == QColorSpace(QColorSpace::SRgb),
                "Migrated export dimensions or color space differ from 0.5.1");
        require(expected.convertToFormat(QImage::Format_RGBA8888) ==
                    output.convertToFormat(QImage::Format_RGBA8888),
                "Migrated export pixels differ from actual stable 0.5.1 export");
        d.key(Qt::Key_Escape);
        d.wait([&] { return !d.object("exportPanel")->property("visible").toBool(); },
               "Migration export panel closed");
        const auto afterExport = catalog.assetSnapshot(row.id);
        require(afterExport && afterExport->asset.edits == snapshot.asset.edits,
                "Export changed migrated edits");
        identities.append(id);
    }
    writeJson(c.root + "/serialized-audit.json", {{"status", "PASS"},
                                                  {"pid", QCoreApplication::applicationPid()},
                                                  {"asset_ids", identities},
                                                  {"versions_validated", versions},
                                                  {"legacy_masks_equal", true},
                                                  {"stable_export_pixels_exact", true}});
    d.quitShortcut();
    d.wait([&] { return !c.window->isVisible() && c.editor.readyForUpdate(); },
           "Migration clean shutdown and worker drain");
    c.checkData();
    return true;
}
} // namespace ux
