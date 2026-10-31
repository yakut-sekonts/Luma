#include "core/export/ExportOptions.h"
#include "core/image/FloatImage.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace luma {
namespace {
template <typename E> bool within(E value, E last) {
    return static_cast<int>(value) >= 0 && value <= last;
}
} // namespace
void WatermarkOptions::validate() const {
    if (!within(kind, WatermarkKind::image) || !std::isfinite(opacity) || opacity < 0 ||
        opacity > 100 || !std::isfinite(size) || size < 1 || size > 100 || !std::isfinite(margin) ||
        margin < 0 || margin > 25 || anchor < 0 || anchor > 8 || text.size() > 1024 ||
        text.find('\0') != std::string::npos || text.find_first_of("\r\n") != std::string::npos ||
        image.native().size() > 4096 || image.native().find('\0') != std::string::npos)
        throw std::runtime_error("Invalid watermark settings.");
    if (kind == WatermarkKind::text && text.find_first_not_of(" \t\r\n") == std::string::npos)
        throw std::runtime_error("Watermark text is empty.");
    if (kind == WatermarkKind::image && image.empty())
        throw std::runtime_error("Select a watermark image.");
}
void ExportOptions::validate() const {
    if (quality < 1 || quality > 100)
        throw std::runtime_error("Lossy export quality must be 1–100.");
    if (!within(format, ExportFormat::heicHDR) || !within(colorSpace, ExportColorSpace::proPhoto) ||
        !within(compression, TiffCompression::zip) || !within(resize, ResizeMode::percentage) ||
        !within(outputMedium, OutputMedium::glossy) || !within(outputAmount, SharpenAmount::high) ||
        !within(metadata, MetadataPolicy::removeLocation) || !std::isfinite(resizeValue) ||
        resizeValue <= 0 || resizeValue > 1e9)
        throw std::runtime_error("Invalid export settings.");
    if (compression != TiffCompression::none && format != ExportFormat::tiff8 &&
        format != ExportFormat::tiff16)
        throw std::runtime_error("TIFF compression requires TIFF output.");
    if (format == ExportFormat::heicHDR && colorSpace != ExportColorSpace::srgb)
        throw std::runtime_error("Adaptive HDR HEIC requires the linear sRGB working space.");
    watermark.validate();
}
bool ExportOptions::advanced() const {
    validate();
    return format == ExportFormat::tiff8 || format == ExportFormat::dng ||
           format == ExportFormat::heicHDR || colorSpace != ExportColorSpace::srgb ||
           compression != TiffCompression::none || resize != ResizeMode::original ||
           outputMedium != OutputMedium::off || metadata != MetadataPolicy::none ||
           (watermark.kind != WatermarkKind::none && watermark.opacity > 0);
}
std::pair<int, int> ExportOptions::dimensions(int width, int height) const {
    validate();
    pixelCount(width, height);
    double scale = 1;
    switch (resize) {
    case ResizeMode::original:
        break;
    case ResizeMode::width:
        scale = resizeValue / width;
        break;
    case ResizeMode::height:
        scale = resizeValue / height;
        break;
    case ResizeMode::longEdge:
        scale = resizeValue / std::max(width, height);
        break;
    case ResizeMode::shortEdge:
        scale = resizeValue / std::min(width, height);
        break;
    case ResizeMode::megapixels:
        scale = std::sqrt(resizeValue * 1e6 / (static_cast<double>(width) * height));
        break;
    case ResizeMode::percentage:
        scale = resizeValue / 100;
        break;
    }
    const double w = std::max(1.0, std::round(width * scale)),
                 h = std::max(1.0, std::round(height * scale));
    if (!std::isfinite(w) || !std::isfinite(h) || w > 20000 || h > 20000)
        throw std::runtime_error("Resized image exceeds supported dimensions.");
    const int outputWidth = static_cast<int>(w), outputHeight = static_cast<int>(h);
    pixelCount(outputWidth, outputHeight);
    if (format == ExportFormat::dng && (outputWidth < 22 || outputHeight < 22))
        throw std::runtime_error(
            "Linear RGB DNG requires at least 22 pixels per edge for RAW-reader compatibility.");
    if (format == ExportFormat::dng &&
        static_cast<std::uint64_t>(outputWidth + 8) * (outputHeight + 8) * 8 > 512'000'000)
        throw std::runtime_error("Linear RGB DNG exceeds the RAW reader memory budget (512 MB).");
    return {outputWidth, outputHeight};
}
std::string ExportOptions::extension() const {
    validate();
    switch (format) {
    case ExportFormat::jpeg:
        return ".jpg";
    case ExportFormat::png:
        return ".png";
    case ExportFormat::tiff8:
    case ExportFormat::tiff16:
        return ".tif";
    case ExportFormat::dng:
        return ".dng";
    case ExportFormat::heicHDR:
        return ".heic";
    }
    throw std::runtime_error("Unsupported export format.");
}
} // namespace luma
