#pragma once
#include <filesystem>
#include <string>
#include <utility>

namespace luma {
// Preserve the original public format values used by desktop/CLI integration.
enum class ExportFormat { jpeg, png, tiff16, tiff8, dng, heicHDR };
enum class ExportColorSpace { srgb, displayP3, adobeRGB, proPhoto };
enum class TiffCompression { none, lzw, zip };
enum class ResizeMode { original, width, height, longEdge, shortEdge, megapixels, percentage };
enum class OutputMedium { off, screen, matte, glossy };
enum class SharpenAmount { low, standard, high };
enum class MetadataPolicy { none, all, copyrightOnly, removeLocation };
enum class WatermarkKind { none, text, image };
struct WatermarkOptions {
    WatermarkKind kind = WatermarkKind::none;
    std::string text;
    std::filesystem::path image;
    double opacity = 70;
    double size = 15;  // Percent of output long edge; text/image is fitted inside this box.
    double margin = 3; // Percent of output short edge.
    int anchor = 8;    // Row-major 3 x 3: top-left through bottom-right.
    bool operator==(const WatermarkOptions &) const = default;
    void validate() const;
};
struct ExportOptions {
    ExportFormat format = ExportFormat::jpeg;
    int quality = 92;
    bool overwrite = false;
    ExportColorSpace colorSpace = ExportColorSpace::srgb;
    TiffCompression compression = TiffCompression::none;
    ResizeMode resize = ResizeMode::original;
    double resizeValue = 2048;
    OutputMedium outputMedium = OutputMedium::off;
    SharpenAmount outputAmount = SharpenAmount::standard;
    MetadataPolicy metadata = MetadataPolicy::none;
    WatermarkOptions watermark{};
    bool operator==(const ExportOptions &) const = default;
    void validate() const;
    bool advanced() const;
    std::pair<int, int> dimensions(int width, int height) const;
    std::string extension() const;
};
} // namespace luma
