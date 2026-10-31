#include "core/catalog/Catalog.h"
#include "core/catalog/Identity.h"
#include "core/color/DisplayTransform.h"
#include "core/export/LinearOutput.h"
#include "core/render/CpuRenderer.h"
#include "core/render/CropGeometry.h"
#include "core/render/MaskCoverage.h"
#include "tests/unit/LegacyCatalogFixture.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sqlite3.h>
#include <unistd.h>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Invalid local mask input was accepted.");
}
bool near(double a, double b, double epsilon = 1e-7) {
    return std::abs(a - b) < epsilon;
}
luma::LocalMask mask() {
    luma::LocalMask result;
    result.id = "01234567-89ab-cdef-0123-456789abcdef";
    result.name = "Градиент \"свет\" \\ 色";
    result.x1 = 0;
    result.y1 = 0.5;
    result.x2 = 1;
    result.y2 = 0.5;
    return result;
}
void coverage() {
    auto m = mask();
    require(m.coverage(0, 0.5) == 0 && m.coverage(1, 0.5) == 1 && m.coverage(0.5, 0) == 0.5,
            "Linear coverage direction/feather is wrong.");
    require(near(m.coverage(.25, .5), .15625), "Feather is not smoothstep.");
    m.feather = 0;
    require(m.coverage(.499, 0) == 0 && m.coverage(.5, 1) == 1, "Hard linear transition failed.");
    m.inverted = true;
    m.opacity = 40;
    require(near(m.coverage(.25, 0), .4) && m.coverage(.75, 1) == 0, "Inversion/opacity failed.");
    m.enabled = false;
    require(m.coverage(.25, 0) == 0, "Disabled mask has coverage.");
    m.enabled = true;
    m.shape = luma::MaskShape::radial;
    m.inverted = false;
    m.opacity = 100;
    m.x1 = m.y1 = .5;
    m.x2 = .4;
    m.y2 = .1;
    m.feather = 100;
    require(m.coverage(.5, .5) == 1 && near(m.coverage(.7, .5), .5) && near(m.coverage(.9, .5), 0),
            "Radial feather/ellipse failed.");
    m.angle = 90;
    require(near(m.coverage(.5, .7), .5) && near(m.coverage(.7, .5), 0), "Radial rotation failed.");
    m.feather = 0;
    require(m.coverage(.5, .89) == 1 && m.coverage(.5, .91) == 0, "Hard radial edge failed.");
    require(m.coverage(std::numeric_limits<double>::quiet_NaN(), 0) == 0,
            "Nonfinite coverage escaped.");
}
void documents() {
    auto m = mask();
    m.setParameter("exposure", 1.23456789012345);
    const auto encoded = luma::encodeLocalMasks({m});
    require(luma::decodeLocalMasks(encoded) == std::vector{m},
            "Canonical Unicode roundtrip failed.");
    require(luma::encodeLocalMasks({}) == "LUMA_LOCAL_MASKS 5 0\n", "Neutral document changed.");
    require(luma::decodeLocalMasks("LUMA_LOCAL_MASKS 2 0\n").empty() &&
                luma::decodeLocalMasks("LUMA_LOCAL_MASKS 4 0\n").empty(),
            "Legacy neutral document did not decode.");
    for (auto text :
         {encoded + " ", std::string("LUMA_LOCAL_MASKS 6 0\n"),
          std::string("LUMA_LOCAL_MASKS 1 129\n"), encoded.substr(0, encoded.size() - 2)})
        rejects([&] { luma::decodeLocalMasks(text); });
    rejects([&] { luma::encodeLocalMasks({m, m}); });
    rejects([&] { luma::decodeLocalMasks(std::string(1048577, 'a')); });
    const auto old = m;
    rejects([&] { m.setParameter("exposure", 5.01); });
    rejects([&] { m.setParameter("opacity", std::numeric_limits<double>::infinity()); });
    rejects([&] { m.setParameter("edgeShift", 1); });
    rejects([&] { m.setParameter("unknown", 1); });
    require(m == old, "Failed parameter validation partially changed mask.");
    for (const auto &name :
         {std::string(), std::string(" bad"), std::string("bad\n"), std::string(81, 'x'),
          std::string("\xc0\xaf"), std::string("\xed\xa0\x80")}) {
        auto bad = m;
        bad.name = name;
        rejects([&] { bad.validate(); });
    }
    auto bad = m;
    bad.id = std::string(36, 'a');
    rejects([&] { bad.validate(); });
    bad = m;
    bad.x2 = bad.x1;
    bad.y2 = bad.y1;
    rejects([&] { bad.validate(); });
    std::vector<luma::LocalMask> many;
    for (int i = 0; i < 128; ++i) {
        auto item = m;
        item.id = luma::newID();
        many.push_back(item);
    }
    require(luma::decodeLocalMasks(luma::encodeLocalMasks(many)) == many,
            "128-mask limit not usable.");
    auto extra = m;
    extra.id = luma::newID();
    many.push_back(extra);
    rejects([&] { luma::encodeLocalMasks(many); });
}
void bitmapRefinement() {
    std::vector<std::uint8_t> values(49);
    values[24] = 255;
    const luma::BitmapMask source(7, 7, values);
    const auto expanded = source.refined(1, 0);
    require(expanded.pixels()[16] == 255 && expanded.pixels()[24] == 255 &&
                expanded.pixels()[32] == 255 && expanded.pixels()[0] == 0,
            "Bitmap edge expansion did not use a bounded two-dimensional maximum filter.");
    const auto contracted = source.refined(-1, 0);
    require(std::all_of(contracted.pixels().begin(), contracted.pixels().end(),
                        [](std::uint8_t value) { return value == 0; }),
            "Bitmap edge contraction did not use a bounded two-dimensional minimum filter.");
    const auto feathered = source.refined(0, 1);
    require(feathered.pixels()[24] > 0 && feathered.pixels()[24] < 255 &&
                feathered.pixels()[23] > 0 && feathered.pixels()[0] == 0,
            "Bitmap edge feather did not produce a local soft boundary.");

    auto refined = mask();
    refined.shape = luma::MaskShape::bitmap;
    refined.bitmap = source;
    refined.edgeShift = 70;
    refined.edgeFeather = 35;
    refined.setParameter("exposure", 1);
    const auto encoded = luma::encodeLocalMasks({refined});
    require(luma::decodeLocalMasks(encoded) == std::vector{refined},
            "Bitmap edge refinement did not survive the mask codec.");
    auto baseline = refined;
    baseline.edgeShift = baseline.edgeFeather = 0;
    const double adjacent = 2.5 / 7;
    require(luma::MaskCoverage(baseline).coverage(adjacent, .5) == 0 &&
                luma::MaskCoverage(refined).coverage(adjacent, .5) > 0,
            "Bitmap edge refinement was not compiled into mask coverage.");
}
void geometry() {
    for (const auto size : {std::array{121, 81}, std::array{1, 1}, std::array{1, 17}})
        for (int orientation = 0; orientation < 8; ++orientation)
            for (int turn = 0; turn < 4; ++turn)
                for (int flips = 0; flips < 4; ++flips) {
                    luma::EditDocument edit;
                    edit.crop.quarterTurns = turn;
                    edit.crop.flipHorizontal = flips & 1;
                    edit.crop.flipVertical = flips & 2;
                    if (size[0] > 1) {
                        edit.crop.angle = 11;
                        edit.crop.x = .1;
                        edit.crop.width = .8;
                        edit.transform.horizontal = 17;
                        edit.transform.rotate = -6;
                    }
                    luma::CropGeometry g(size[0], size[1], orientation, edit);
                    for (double x : {0.0, (g.width() - 1) / 2.0, g.width() - 1.0})
                        for (double y : {0.0, (g.height() - 1) / 2.0, g.height() - 1.0}) {
                            auto source = g.source(x, y);
                            auto output = g.output(source[0], source[1]);
                            require(near(output[0], x, 1e-8) && near(output[1], y, 1e-8),
                                    "Mask forward/inverse geometry disagrees.");
                        }
                }
}
void pixels() {
    luma::FloatImage image(17, 11);
    for (std::size_t i = 0; i < image.rgb.size(); ++i)
        image.rgb[i] = static_cast<float>((i * 7) % 41) / 100;
    luma::EditDocument edit;
    auto m = mask();
    edit.localMasks = {m};
    const auto baseline = luma::renderCPU(image, {});
    require(luma::renderCPU(image, edit).rgba == baseline.rgba, "Neutral mask changed pixels.");
    m.setParameter("exposure", 1);
    edit.localMasks = {m};
    luma::LocalAdjustmentPass pass(edit.localMasks);
    const std::array<float, 3> color{-.1F, .18F, 2};
    const auto half = pass.pixel(color, .5, .5);
    for (std::size_t c = 0; c < 3; ++c)
        require(near(half[c], color[c] * 1.5, 1e-6), "Local EV did not blend linear signed HDR.");
    m.enabled = false;
    edit.localMasks = {m};
    require(luma::renderCPU(image, edit).rgba == baseline.rgba, "Disabled local changed pixels.");
    m.enabled = true;
    auto second = m;
    second.id = luma::newID();
    second.setParameter("exposure", 0);
    second.setParameter("contrast", 60);
    require(luma::LocalAdjustmentPass({m, second}).pixel(color, .7, .5) !=
                luma::LocalAdjustmentPass({second, m}).pixel(color, .7, .5),
            "Layer order was ignored.");
    auto composite = m;
    composite.id = luma::newID();
    composite.shape = luma::MaskShape::brush;
    composite.strokes = {{false, .3, 80, {{.1, .2}, {.9, .8}}}, {true, .05, 50, {{.5, .5}}}};
    luma::MaskComponent selector;
    selector.id = luma::newID();
    selector.geometry.shape = luma::MaskShape::luminance;
    selector.geometry.rangeLow = .1;
    selector.geometry.rangeHigh = .9;
    selector.operation = luma::MaskOperation::intersect;
    composite.components = {selector};
    auto colors = second;
    colors.id = luma::newID();
    colors.shape = luma::MaskShape::color;
    colors.colorTolerance = .6;
    edit.localMasks = {m, second, composite, colors};
    edit.exposure = .25;
    edit.crop.angle = 8;
    edit.crop.x = .1;
    edit.crop.width = .8;
    edit.transform.horizontal = 12;
    image.alpha.resize(static_cast<std::size_t>(image.width) * image.height);
    for (std::size_t i = 0; i < image.alpha.size(); ++i)
        image.alpha[i] = static_cast<float>(i % 5) / 4;
    for (int orientation = 0; orientation < 8; ++orientation) {
        luma::DecodedRaw decoded{image, {}, {}, {}};
        decoded.metadata.orientation = orientation;
        const auto linear = luma::renderLinearOutput(decoded, edit);
        const auto preview = luma::renderCPU(image, edit, orientation);
        require(linear.width == preview.width && linear.height == preview.height,
                "Preview/export geometry differs.");
        std::vector<float> rgb;
        for (std::size_t i = 0; i < linear.rgba.size(); i += 4)
            rgb.insert(rgb.end(), linear.rgba.begin() + static_cast<std::ptrdiff_t>(i),
                       linear.rgba.begin() + static_cast<std::ptrdiff_t>(i + 3));
        std::vector<std::uint8_t> expected(preview.rgba.size());
        luma::DisplayTransform().convert(rgb, expected);
        for (std::size_t i = 0; i < expected.size(); ++i)
            require(i % 4 == 3 ? near(preview.rgba[i], std::lround(linear.rgba[i] * 255), 1)
                               : preview.rgba[i] == expected[i],
                    "Preview/linear export pixels disagree.");
        const auto overlay = luma::renderCPU(image, edit, orientation, {}, {}, {},
                                             luma::DetailDiagnostic::none, 1, {}, {m.id});
        bool changed = false;
        for (std::size_t i = 0; i < overlay.rgba.size(); ++i) {
            if (i % 4 == 3)
                require(overlay.rgba[i] == preview.rgba[i], "Overlay changed alpha.");
            else
                changed |= overlay.rgba[i] != preview.rgba[i];
        }
        require(changed && luma::renderLinearOutput(decoded, edit).rgba == linear.rgba,
                "Overlay leaked into export.");
    }
}
void overlayAlpha() {
    luma::FloatImage image(17, 11);
    std::fill(image.rgb.begin(), image.rgb.end(), 0.18F);
    image.alpha.resize(187);
    for (int y = 0; y < 11; ++y)
        for (int x = 0; x < 17; ++x)
            image.alpha[static_cast<std::size_t>(y) * 17 + x] = x < 8 ? 1.F : 0.F;
    auto m = mask();
    m.feather = 0;
    luma::EditDocument edit;
    edit.localMasks = {m};
    edit.crop.angle = 13;
    const auto plain = luma::renderCPU(image, edit);
    const auto overlay =
        luma::renderCPU(image, edit, 0, {}, {}, {}, luma::DetailDiagnostic::none, 1, {}, {m.id});
    // The selected half is fully transparent. Its hidden mask values must not tint visible
    // neighbors.
    require(plain.rgba == overlay.rgba,
            "Transparent masked samples contaminated the visible overlay.");
}
void sql(const std::filesystem::path &path, const char *query) {
    sqlite3 *db = nullptr;
    require(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "SQLite open failed.");
    const int code = sqlite3_exec(db, query, nullptr, nullptr, nullptr);
    const std::string error = sqlite3_errmsg(db);
    sqlite3_close(db);
    if (code != SQLITE_OK)
        throw std::runtime_error(error);
}
void persistence(const std::filesystem::path &root) {
    std::ofstream(root / "fixture.dng") << "local fixture";
    std::ofstream(root / "other.dng") << "other fixture";
    const auto path = root / "local.sqlite";
    std::string id;
    auto edits = luma::EditDocument{};
    {
        luma::Catalog db(path);
        luma::PhotoMetadata metadata;
        metadata.width = 17;
        metadata.height = 11;
        id = db.addAndSelect(root / "fixture.dng", "checksum", metadata).id;
        edits.exposure = .7;
        db.saveEdits(id, edits);
    }
    sql(path, fixture::removePostV11Tables);
    sql(path, "DROP TRIGGER initialize_local_masks;DROP TABLE edit_local_masks;DELETE FROM "
              "schema_migrations WHERE version>=10;PRAGMA user_version=9;");
    {
        luma::Catalog db(path);
        auto snap = db.assetSnapshot(id);
        require(snap && snap->revision == 1 && snap->asset.edits == edits,
                "Schema 9 migration changed global edits or revision.");
        auto composed = mask();
        luma::MaskComponent brush;
        brush.id = luma::newID();
        brush.geometry.shape = luma::MaskShape::brush;
        brush.geometry.strokes = {{false, .1, 75, {{.25, .5}, {.75, .5}}}};
        composed.components = {brush};
        edits.localMasks = {composed};
        edits.localMasks[0].setParameter("exposure", .75);
        db.saveEdits(id, edits);
    }
    {
        luma::Catalog db(path);
        require(db.activeAsset()->edits == edits, "Restart lost masks.");
        luma::EditDocument global;
        global.exposure = -.5;
        db.applyBatchEdits({id}, global, 1); // Light group.
        require(db.assetByID(id)->edits.localMasks == edits.localMasks,
                "Global batch erased local masks.");
        db.restoreBatchEdits(false);
        require(db.assetByID(id)->edits == edits, "Batch Undo lost masks.");
        db.restoreBatchEdits(true);
        auto changed = db.assetByID(id)->edits;
        changed.localMasks[0].components[0].geometry.strokes[0].points[0].x = .35;
        db.saveEdits(id, changed);
        rejects([&] { db.restoreBatchEdits(false); });
    }
    sql(path, "UPDATE edit_local_masks SET document='broken';");
    {
        luma::Catalog db(path);
        rejects([&] { db.activeAsset(); });
    }
    sql(path, "DELETE FROM edit_local_masks;");
    {
        luma::Catalog db(path);
        rejects([&] { db.activeAsset(); });
        rejects([&] { db.saveEdits(id, edits); });
    }
    // Refuse future parent data atomically; do not mark a failed migration as current.
    const auto bad = root / "future.sqlite";
    {
        luma::Catalog db(bad);
        luma::PhotoMetadata metadata;
        metadata.width = 2;
        metadata.height = 2;
        db.addAndSelect(root / "other.dng", "other", metadata);
    }
    sql(bad, fixture::removePostV11Tables);
    sql(bad, "DROP TRIGGER initialize_local_masks;DROP TABLE edit_local_masks;DELETE FROM "
             "schema_migrations WHERE version>=10;PRAGMA user_version=9;PRAGMA "
             "ignore_check_constraints=ON;UPDATE edit_documents SET schema_version=99;");
    rejects([&] { luma::Catalog db(bad); });
    sql(bad, "PRAGMA ignore_check_constraints=ON;UPDATE edit_documents SET schema_version=8;");
    {
        luma::Catalog db(bad);
        require(db.activeAsset()->edits.localMasks.empty(), "Failed migration did not roll back.");
    }
}
} // namespace
void completeMaskTests();
int main() {
    const auto root =
        std::filesystem::temp_directory_path() / ("luma-local-" + std::to_string(getpid()));
    try {
        std::filesystem::create_directories(root);
        completeMaskTests();
        coverage();
        documents();
        bitmapRefinement();
        geometry();
        pixels();
        overlayAlpha();
        persistence(root);
        std::filesystem::remove_all(root);
        std::cout << "Local masks: coverage, HDR, geometry, preview/export, migration and batch "
                     "conflict passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
}
