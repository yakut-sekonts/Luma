#include "core/edits/EditSnapshot.h"
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <tuple>

namespace luma {
namespace {
// Version 1 retains its fixed field order; version 2 appends the creative codec.
// Old snapshots decode with neutral creative values and re-encode as version 1.
template <typename Document> auto fields(Document &d) {
    return std::tie(
        d.exposure, d.temperature, d.tint, d.customWhiteBalance, d.contrast, d.highlights,
        d.shadows, d.whites, d.blacks, d.vibrance, d.saturation, d.enabledCorrections,
        d.sharpenAmount, d.sharpenRadius, d.sharpenDetail, d.sharpenMasking, d.noiseLuminance,
        d.noiseLuminanceDetail, d.noiseLuminanceContrast, d.noiseColor, d.noiseColorDetail,
        d.noiseColorSmoothness, d.crop.x, d.crop.y, d.crop.width, d.crop.height, d.crop.angle,
        d.crop.aspect, d.crop.quarterTurns, d.crop.flipHorizontal, d.crop.flipVertical,
        d.lens.distortion, d.lens.vignetting, d.lens.chromaticAberration, d.transform.rotate,
        d.transform.horizontal, d.transform.vertical, d.transform.aspect, d.transform.scale,
        d.transform.xOffset, d.transform.yOffset, d.transform.constrainCrop, d.transform.guided,
        d.curveShadows, d.curveDarks, d.curveLights, d.curveHighlights);
}
void require(bool condition) {
    if (!condition)
        throw std::runtime_error("Invalid processing version document.");
}
} // namespace
static std::string encodeSnapshot(const EditDocument &edits, int version) {
    static_assert(EditDocument::schemaVersion == 8, "Update the full-document snapshot codec.");
    edits.validate();
    const auto masks = encodeLocalMasks(edits.localMasks);
    const auto retouch = encodeRetouch(edits.retouch);
    if (retouch.size() > 2 * 1024 * 1024)
        throw std::runtime_error("Processing version exceeds its storage limit.");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "LUMA_EDIT_SNAPSHOT " << version << " 8\n";
    std::apply([&](const auto &...value) { ((out << value << ' '), ...); }, fields(edits));
    out << '\n' << std::quoted(edits.lens.profile) << '\n';
    for (const auto value : edits.transform.guide)
        out << value << ' ';
    out << '\n';
    for (const auto &curve : edits.curves) {
        out << curve.points.size();
        for (const auto &point : curve.points)
            out << ' ' << point.x << ' ' << point.y;
        out << '\n';
    }
    for (const auto &band : edits.hsl)
        out << band.hue << ' ' << band.saturation << ' ' << band.luminance << '\n';
    out << std::quoted(masks) << '\n' << std::quoted(retouch) << '\n';
    if (version >= 2)
        out << std::quoted(encodeCreative(edits.creative)) << '\n';
    auto result = out.str();
    if (result.size() > maximumEditSnapshotBytes)
        throw std::runtime_error("Processing version exceeds its storage limit.");
    return result;
}
std::string encodeEditSnapshot(const EditDocument &edits) {
    return encodeSnapshot(edits, 2);
}
EditDocument decodeEditSnapshot(std::string_view document) {
    require(!document.empty() && document.size() <= maximumEditSnapshotBytes);
    std::istringstream in{std::string(document)};
    in.imbue(std::locale::classic());
    std::string magic;
    int version = 0, schema = 0;
    require(static_cast<bool>(in >> magic >> version >> schema));
    if (magic != "LUMA_EDIT_SNAPSHOT" || (version != 1 && version != 2) || schema != 8)
        throw std::runtime_error("Unsupported processing version document.");
    EditDocument result;
    std::apply([&](auto &...value) { ((in >> value), ...); }, fields(result));
    in >> std::quoted(result.lens.profile);
    for (auto &value : result.transform.guide)
        in >> value;
    require(static_cast<bool>(in));
    for (auto &curve : result.curves) {
        std::size_t count = 0;
        require(static_cast<bool>(in >> count) && count >= 2 && count <= 32);
        curve.points.resize(count);
        for (auto &point : curve.points)
            in >> point.x >> point.y;
        require(static_cast<bool>(in));
    }
    for (auto &band : result.hsl)
        in >> band.hue >> band.saturation >> band.luminance;
    std::string masks, retouch;
    require(static_cast<bool>(in >> std::quoted(masks) >> std::quoted(retouch)));
    result.localMasks = decodeLocalMasks(masks);
    result.retouch = decodeRetouch(retouch);
    if (version >= 2) {
        std::string creative;
        require(static_cast<bool>(in >> std::quoted(creative)));
        result.creative = decodeCreative(creative);
    }
    result.validate();
    // Also rejects trailing data, invalid integer spellings and alternate field layouts.
    require(encodeSnapshot(result, version) == document);
    return result;
}
} // namespace luma
