#include "core/catalog/Catalog.h"
#include "core/catalog/Identity.h"
#include "core/render/AdvancedColor.h"
#include "core/render/LocalAdjustments.h"
#include "core/render/MaskReference.h"
#include "tests/unit/LegacyCatalogFixture.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sqlite3.h>
#include <unistd.h>
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool near(double a, double b, double epsilon = 1e-6) {
    return std::abs(a - b) < epsilon;
}
template <class F> void rejects(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Invalid complete mask data accepted.");
}
luma::LocalMask base(luma::MaskShape shape) {
    luma::LocalMask mask;
    mask.id = luma::newID();
    mask.name = "Complete mask";
    mask.shape = shape;
    return mask;
}
void brush() {
    auto mask = base(luma::MaskShape::brush);
    mask.feather = 0;
    mask.strokes = {luma::BrushStroke{false, .1, 100, {{.5, .5}}}};
    luma::MaskCoverage coverage(mask, 2);
    require(coverage.coverage(.549, .5) == 1 && coverage.coverage(.5, .599) == 1 &&
                coverage.coverage(.551, .5) == 0 && coverage.coverage(.5, .601) == 0,
            "Brush is not circular in source pixel units.");
    mask.feather = 100;
    require(near(mask.coverage(.525, .5, {}, 2), .5), "Brush feather failed.");
    mask.feather = 0;
    mask.strokes[0].opacity = 50;
    mask.strokes.push_back(mask.strokes[0]);
    mask.strokes.push_back({true, .1, 50, {{.5, .5}}});
    require(near(mask.coverage(.5, .5), .375), "Brush paint/erase opacity ordering failed.");
    mask.strokes = {{false, .04, 70, {{.1, .5}, {.9, .5}}}};
    mask.feather = 75;
    auto dense = mask;
    dense.strokes[0].points.clear();
    for (int i = 0; i <= 100; ++i)
        dense.strokes[0].points.push_back({.1 + i * .008, .5});
    const luma::MaskCoverage sparseCoverage(mask, 1.5), denseCoverage(dense, 1.5);
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 40; ++x)
            require(near(sparseCoverage.coverage(x / 40.0, y / 20.0),
                         denseCoverage.coverage(x / 40.0, y / 20.0), 1e-12),
                    "Brush depends on pointer event frequency or loses fast sweeps.");
    mask.inverted = true;
    mask.opacity = 25;
    require(near(mask.coverage(0, 0), .25), "Brush invert/global opacity failed.");
    auto bad = mask;
    bad.strokes[0].points.clear();
    rejects([&] { bad.validate(); });
    bad = mask;
    bad.strokes[0].radius = 0;
    rejects([&] { bad.validate(); });
    bad = mask;
    bad.strokes[0].points[0].x = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { bad.validate(); });
}
double bruteBrush(const luma::MaskGeometry &mask, double x, double y, double aspect) {
    const double xs = std::max(1.0, aspect), ys = std::max(1.0, 1 / aspect);
    x *= xs;
    y *= ys;
    double coverage = 0;
    for (const auto &stroke : mask.strokes) {
        double maximum = 0;
        for (std::size_t i = 0; i < stroke.points.size(); ++i) {
            const auto &a = stroke.points[i == 0 ? 0 : i - 1], &b = stroke.points[i];
            const double ax = a.x * xs, ay = a.y * ys, dx = (b.x - a.x) * xs, dy = (b.y - a.y) * ys,
                         length = dx * dx + dy * dy;
            const double t =
                length == 0
                    ? 0
                    : std::max(0.0, std::min(1.0, ((x - ax) * dx + (y - ay) * dy) / length));
            const double d = std::hypot(x - ax - t * dx, y - ay - t * dy) / stroke.radius;
            const double hard = 1 - mask.feather / 100;
            double amount = d <= hard ? 1 : 0;
            if (d > hard && d < 1 && mask.feather > 0) {
                const double p = (d - hard) / (1 - hard);
                amount = 1 - (3 * p * p - 2 * p * p * p);
            }
            maximum = std::max(maximum, amount);
        }
        const double amount = maximum * stroke.opacity / 100;
        coverage = stroke.erase ? coverage * (1 - amount) : coverage + (1 - coverage) * amount;
    }
    return coverage;
}
void spatialIndex() {
    auto mask = base(luma::MaskShape::brush);
    mask.feather = 63;
    for (int s = 0; s < 7; ++s) {
        luma::BrushStroke stroke;
        stroke.erase = s % 3 == 2;
        stroke.radius = .02 + s * .009;
        stroke.opacity = 40 + s * 7;
        for (int p = 0; p < 31; ++p)
            stroke.points.push_back({.1 + p * .025, .2 + s * .08 + .06 * std::sin(p * .4 + s)});
        mask.strokes.push_back(stroke);
    }
    for (double aspect : {.5, 1.0, 2.0}) {
        const luma::MaskCoverage indexed(mask, aspect);
        for (int y = 0; y <= 31; ++y)
            for (int x = 0; x <= 43; ++x)
                require(near(indexed.coverage(x / 43.0, y / 31.0),
                             bruteBrush(mask, x / 43.0, y / 31.0, aspect), 1e-12),
                        "Brush BVH dropped, duplicated or reordered a segment.");
    }
}
void operations() {
    auto mask = base(luma::MaskShape::linear);
    mask.x1 = mask.x2 = .5;
    mask.y1 = 0;
    mask.y2 = 1;
    luma::MaskComponent component;
    component.id = luma::newID();
    component.geometry.shape = luma::MaskShape::radial;
    component.geometry.x1 = component.geometry.y1 = .5;
    component.geometry.x2 = component.geometry.y2 = .25;
    mask.components = {component};
    mask.components[0].operation = luma::MaskOperation::add;
    require(near(mask.coverage(.625, .5), .75), "Soft Add is not a+b-ab.");
    mask.components[0].operation = luma::MaskOperation::subtract;
    require(near(mask.coverage(.625, .5), .25), "Soft Subtract is not a(1-b).");
    mask.components[0].operation = luma::MaskOperation::intersect;
    require(near(mask.coverage(.625, .5), .25), "Soft Intersect is not ab.");
    mask.inverted = true;
    mask.opacity = 40;
    require(near(mask.coverage(.625, .5), .3),
            "Final mask invert/opacity applied before components.");
    mask.components[0].enabled = false;
    require(near(mask.coverage(.625, .5), .2), "Disabled component affected selection.");
    mask.enabled = false;
    require(mask.coverage(.625, .5) == 0, "Disabled composite mask has coverage.");
    mask.enabled = true;
    mask.inverted = false;
    mask.opacity = 100;
    mask.components[0].enabled = true;
    mask.components[0].inverted = true;
    const double raw = 1 - std::pow(0.25, 2) * (3 - 2 * .25); // radial radius=.25, feather=100.
    require(near(mask.coverage(.5625, .5), .5 * (1 - raw)),
            "Component inversion applied to wrong operand.");
    auto second = mask.components[0];
    second.id = luma::newID();
    second.inverted = false;
    second.operation = luma::MaskOperation::add;
    mask.components.push_back(second);
    const double first = mask.coverage(.625, .5);
    std::swap(mask.components[0], mask.components[1]);
    require(!near(first, mask.coverage(.625, .5)), "Boolean component order was ignored.");
    auto bad = mask;
    bad.components[0].id = bad.id;
    rejects([&] { bad.validate(); });
    bad = mask;
    bad.components[0].operation = static_cast<luma::MaskOperation>(3);
    rejects([&] { bad.validate(); });
}
std::array<float, 3> gray(double encoded) {
    const auto value = static_cast<float>(luma::decodeSRGB(encoded));
    return {value, value, value};
}
void ranges() {
    auto mask = base(luma::MaskShape::luminance);
    mask.rangeLow = .4;
    mask.rangeHigh = .6;
    mask.feather = 0;
    require(mask.coverage(.2, .3, gray(.5)) == 1 && mask.coverage(.8, .7, gray(.3)) == 0,
            "Luminance range does not use encoded global luminance.");
    require(mask.coverage(0, 0, {1, 0, 0}) == 1 && mask.coverage(0, 0, {0, 1, 0}) == 0,
            "Luminance selector used RGB brightness instead of luminance.");
    mask.feather = 100;
    require(near(mask.coverage(0, 0, gray(.275)), .5), "Luminance range softness failed.");
    mask.rangeLow = 0;
    mask.rangeHigh = 1;
    require(mask.coverage(0, 0, {-2, -2, -2}) == 1 && mask.coverage(0, 0, {8, 8, 8}) == 1,
            "Full luminance range lost signed/HDR pixels.");
    mask.shape = luma::MaskShape::color;
    mask.referenceColor = {1, 0, 0};
    mask.colorTolerance = .1;
    mask.feather = 0;
    require(mask.coverage(0, 0, {1, 0, 0}) == 1 && mask.coverage(0, 0, {0, 1, 0}) == 0,
            "Color range failed red/green separation.");
    mask.referenceColor = {.5, .5, .5};
    mask.colorTolerance = .2;
    mask.feather = 100;
    require(near(mask.coverage(0, 0, gray(.6)), .5), "Color range softness failed.");
    mask.colorTolerance = 0;
    mask.referenceColor = {0, 0, 0};
    require(mask.coverage(0, 0, {0, 0, 0}) == 1 && mask.coverage(0, 0, gray(.001)) == 0,
            "Exact color tolerance failed.");
    auto exposure = base(luma::MaskShape::linear);
    exposure.x1 = 0;
    exposure.x2 = 1;
    exposure.y1 = exposure.y2 = .5;
    exposure.feather = 0;
    exposure.setParameter("exposure", 3);
    auto selected = base(luma::MaskShape::luminance);
    selected.rangeLow = .4;
    selected.rangeHigh = .6;
    selected.feather = 0;
    selected.setParameter("exposure", 1);
    const auto output =
        luma::LocalAdjustmentPass({exposure, selected}).pixel({.18F, .18F, .18F}, .9, .5);
    require(near(output[0], 2.88), "Range selection fed back from an earlier local layer.");
}
void referenceSampling() {
    luma::FloatImage image(2, 1);
    image.rgb = {1, 0, 0, 0, 0, 1};
    image.alpha = {1, 0};
    const auto sample = luma::sampleMaskReference(image, {}, {}, {}, .5, .5);
    require(sample == std::array<float, 3>{1, 0, 0},
            "Range eyedropper sampled hidden transparent RGB.");
    rejects([&] { luma::sampleMaskReference(image, {}, {}, {}, 1, .5); });
    luma::FloatImage constant(17, 11);
    std::fill(constant.rgb.begin(), constant.rgb.end(), .18F);
    luma::EditDocument edit;
    edit.exposure = 1;
    edit.noiseLuminance = 20;
    edit.sharpenAmount = 80;
    const auto developed = luma::sampleMaskReference(constant, edit, {}, {}, .4, .6);
    require(near(developed[0], .36), "Range sample differs from global Develop/Detail.");
}
void serialization() {
    const std::string legacy = "LUMA_LOCAL_MASKS 1 1\n\"01234567-89ab-cdef-0123-456789abcdef\" "
                               "\"Legacy\" 0 1 0 0.5 0.25 0.5 0.75 0 100 100 1.25 0 0 0 0 0 0 0\n";
    auto old = luma::decodeLocalMasks(legacy);
    require(old.size() == 1 && old[0].components.empty() && old[0].parameter("exposure") == 1.25,
            "Legacy gradient decode failed.");
    auto mask = base(luma::MaskShape::brush);
    mask.strokes = {{false, .08, 75, {{.1, .2}, {.3, .4}}}, {true, .02, 90, {{.2, .3}}}};
    for (auto shape : {luma::MaskShape::linear, luma::MaskShape::radial, luma::MaskShape::brush,
                       luma::MaskShape::luminance, luma::MaskShape::color}) {
        luma::MaskComponent c;
        c.id = luma::newID();
        c.geometry.shape = shape;
        if (shape == luma::MaskShape::radial) {
            c.geometry.x2 = c.geometry.y2 = .2;
        }
        c.operation = luma::MaskOperation::intersect;
        mask.components.push_back(c);
    }
    const auto encoded = luma::encodeLocalMasks({mask, old[0]});
    require(encoded.starts_with("LUMA_LOCAL_MASKS 3 2\n") &&
                luma::decodeLocalMasks(encoded) == std::vector{mask, old[0]},
            "Version-3 masks did not roundtrip losslessly.");
    auto version2 = encoded;
    version2.replace(0, std::string("LUMA_LOCAL_MASKS 3").size(), "LUMA_LOCAL_MASKS 2");
    require(luma::decodeLocalMasks(version2) == std::vector{mask, old[0]},
            "Version-2 vector masks lost data when read by the version-3 codec.");
    rejects([&] { luma::decodeLocalMasks(encoded + "junk"); });
    auto bad = mask;
    bad.strokes[0].points.resize(16385);
    rejects([&] { luma::encodeLocalMasks({bad}); });
}
void sql(const std::filesystem::path &path, const std::string &query) {
    sqlite3 *db = nullptr;
    require(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "Cannot open migration fixture.");
    const int code = sqlite3_exec(db, query.c_str(), nullptr, nullptr, nullptr);
    const std::string error = sqlite3_errmsg(db);
    sqlite3_close(db);
    if (code != SQLITE_OK)
        throw std::runtime_error(error);
}
std::string scalar(const std::filesystem::path &path, const std::string &query) {
    sqlite3 *db = nullptr;
    require(sqlite3_open(path.c_str(), &db) == SQLITE_OK, "Cannot inspect migration fixture.");
    sqlite3_stmt *s = nullptr;
    require(sqlite3_prepare_v2(db, query.c_str(), -1, &s, nullptr) == SQLITE_OK &&
                sqlite3_step(s) == SQLITE_ROW,
            "Missing migration proof.");
    const std::string result = reinterpret_cast<const char *>(sqlite3_column_text(s, 0));
    sqlite3_finalize(s);
    sqlite3_close(db);
    return result;
}
void migration() {
    const auto root =
        std::filesystem::temp_directory_path() / ("luma-components-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    try {
        std::ofstream(root / "one.dng") << "one";
        std::ofstream(root / "two.dng") << "two";
        const auto path = root / "catalog.sqlite";
        std::string one, two;
        {
            luma::Catalog db(path);
            luma::PhotoMetadata meta;
            meta.width = 17;
            meta.height = 11;
            one = db.addAndSelect(root / "one.dng", "one", meta).id;
            two = db.addAndSelect(root / "two.dng", "two", meta).id;
            db.saveExposure(one, .7);
        }
        const std::string legacy =
            "LUMA_LOCAL_MASKS 1 1\n\"01234567-89ab-cdef-0123-456789abcdef\" \"Legacy\" 0 1 0 0.5 "
            "0.25 0.5 0.75 0 100 100 1.25 0 0 0 0 0 0 0\n";
        sql(path, fixture::removePostV11Tables);
        sql(path, "UPDATE edit_local_masks SET document='" + legacy +
                      "';DROP TRIGGER initialize_local_masks;CREATE TRIGGER initialize_local_masks "
                      "AFTER INSERT ON edit_documents BEGIN INSERT INTO edit_local_masks "
                      "VALUES(NEW.asset_id,'LUMA_LOCAL_MASKS 1 0'||char(10));END;DELETE FROM "
                      "schema_migrations WHERE version>=11;PRAGMA user_version=10;");
        const auto first = std::min(one, two), last = std::max(one, two);
        sql(path, "UPDATE edit_local_masks SET document='broken' WHERE asset_id='" + last + "';");
        rejects([&] { luma::Catalog db(path); });
        require(scalar(path, "PRAGMA user_version") == "10" &&
                    scalar(path, "SELECT document FROM edit_local_masks WHERE asset_id='" + first +
                                     "'") == legacy,
                "Failed catalog-10 migration did not roll back earlier documents.");
        sql(path, "UPDATE edit_local_masks SET document='" + legacy + "';");
        {
            luma::Catalog db(path);
            const auto record = db.assetSnapshot(one);
            require(record && record->revision == 1 && record->asset.edits.exposure == .7 &&
                        record->asset.edits.localMasks == luma::decodeLocalMasks(legacy),
                    "Catalog-10 migration changed revision, global edits or gradient data.");
        }
        require(scalar(path, "PRAGMA user_version") == "21" &&
                    scalar(path, "SELECT document FROM edit_local_masks LIMIT 1")
                        .starts_with("LUMA_LOCAL_MASKS 3 1\n"),
                "Migration did not persist version 3.");
        std::filesystem::remove_all(root);
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }
}
} // namespace
void completeMaskTests() {
    brush();
    spatialIndex();
    operations();
    ranges();
    referenceSampling();
    serialization();
    migration();
}
