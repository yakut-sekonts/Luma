#include "core/catalog/Identity.h"
#include "core/color/DisplayTransform.h"
#include "core/export/DngWriter.h"
#include "core/export/LinearOutput.h"
#include "core/render/DevelopPipeline.h"
#include "platform/macos/ExportColor.h"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <libraw/libraw.h>
#include <limits>
#include <memory>
#include <tuple>

void exportNativeTests();
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Invalid export operation was accepted.");
}
bool near(double a, double b, double tolerance = 1e-6) {
    return std::abs(a - b) <= tolerance;
}
void settings() {
    luma::ExportOptions options;
    require(!options.advanced(), "Default exports no longer use their exact previous path.");
    for (const auto format :
         {luma::ExportFormat::jpeg, luma::ExportFormat::png, luma::ExportFormat::tiff16}) {
        options.format = format;
        require(!options.advanced(), "Existing default format unexpectedly uses advanced output.");
    }
    options.format = luma::ExportFormat::png;
    for (const auto &[mode, value, width, height] :
         {std::tuple{luma::ResizeMode::width, 60.0, 60, 40},
          {luma::ResizeMode::height, 40.0, 60, 40},
          {luma::ResizeMode::longEdge, 90.0, 90, 60},
          {luma::ResizeMode::shortEdge, 40.0, 60, 40},
          {luma::ResizeMode::megapixels, 0.0096, 120, 80},
          {luma::ResizeMode::percentage, 50.0, 60, 40}}) {
        options.resize = mode;
        options.resizeValue = value;
        require(options.dimensions(120, 80) == std::pair{width, height},
                "Resize mode produced incorrect dimensions.");
    }
    options.resizeValue = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { options.validate(); });
    options.resizeValue = 1e9;
    rejects([&] { options.dimensions(120, 80); });
    options = {};
    options.quality = 0;
    rejects([&] { options.validate(); });
    options = {};
    options.format = static_cast<luma::ExportFormat>(99);
    rejects([&] { options.validate(); });
    options = {};
    options.compression = luma::TiffCompression::zip;
    rejects([&] { options.validate(); });
    options = {};
    options.format = luma::ExportFormat::dng;
    rejects([&] { options.dimensions(21, 32); });
    require(options.dimensions(22, 32) == std::pair{22, 32},
            "DNG minimum reader boundary is wrong.");
    options = {};
    options.format = luma::ExportFormat::heicHDR;
    require(options.advanced() && options.extension() == ".heic",
            "Adaptive HDR does not use its explicit HEIC export path.");
    options.colorSpace = luma::ExportColorSpace::displayP3;
    rejects([&] { options.validate(); });
    options = {};
    options.watermark.kind = luma::WatermarkKind::text;
    rejects([&] { options.validate(); });
    options.watermark.text = "Luma";
    options.watermark.opacity = 101;
    rejects([&] { options.validate(); });
    options.watermark.opacity = 50;
    options.watermark.anchor = 9;
    rejects([&] { options.validate(); });
    options.watermark.anchor = 8;
    options.validate();
}
void resampling() {
    luma::LinearOutput input(2, 2);
    input.rgba = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0};
    luma::ExportOptions options;
    options.resize = luma::ResizeMode::width;
    options.resizeValue = 1;
    const auto small = luma::resizeOutput(input, options);
    require(small.width == 1 && small.height == 1 && near(small.rgba[0], .5) &&
                near(small.rgba[1], .5) && near(small.rgba[2], 0) && near(small.rgba[3], .5),
            "Area resampling mixed invisible RGB into visible pixels.");
    options.resizeValue = 3;
    const auto large = luma::resizeOutput(input, options);
    const auto center = std::size_t{(1 * 3 + 1) * 4};
    require(near(large.rgba[center], .5) && near(large.rgba[center + 1], .5) &&
                near(large.rgba[center + 2], 0) && near(large.rgba[center + 3], .5),
            "Bilinear enlargement did not preserve alpha/color coverage.");
    luma::LinearOutput constant(19, 13);
    for (std::size_t i = 0; i < constant.rgba.size(); i += 4) {
        constant.rgba[i] = 2;
        constant.rgba[i + 1] = -1;
        constant.rgba[i + 2] = .5;
        constant.rgba[i + 3] = 1;
    }
    for (double width : {3.0, 19.0, 37.0}) {
        options.resizeValue = width;
        const auto resized = luma::resizeOutput(constant, options);
        for (std::size_t i = 0; i < resized.rgba.size(); i += 4)
            require(near(resized.rgba[i], 2) && near(resized.rgba[i + 1], -1) &&
                        near(resized.rgba[i + 2], .5) && near(resized.rgba[i + 3], 1),
                    "Resize clipped signed/HDR samples or changed a constant field.");
    }
    options = {};
    require(luma::resizeOutput(constant, options).rgba == constant.rgba,
            "Original size changed exact linear pixels.");
    luma::CancellationSource cancelled;
    cancelled.cancel();
    options.resize = luma::ResizeMode::width;
    options.resizeValue = 5;
    rejects([&] { luma::resizeOutput(constant, options, cancelled.token()); });
}
void sharpening() {
    luma::LinearOutput image(64, 32);
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) {
            const auto i = (static_cast<std::size_t>(y) * image.width + x) * 4;
            const float value = x < 32 ? .2F : .8F;
            image.rgba[i] = value;
            image.rgba[i + 1] = value * .5F;
            image.rgba[i + 2] = value * .25F;
            image.rgba[i + 3] = 1;
        }
    luma::ExportOptions options;
    auto disabled = image;
    luma::sharpenOutput(disabled, options);
    require(disabled.rgba == image.rgba, "Disabled output sharpening changed pixels.");
    for (auto medium :
         {luma::OutputMedium::screen, luma::OutputMedium::matte, luma::OutputMedium::glossy}) {
        options.outputMedium = medium;
        double previous = 0;
        for (auto amount :
             {luma::SharpenAmount::low, luma::SharpenAmount::standard, luma::SharpenAmount::high}) {
            options.outputAmount = amount;
            auto sharpened = image;
            luma::sharpenOutput(sharpened, options);
            const auto edge = std::size_t{(16 * 64 + 32) * 4};
            const double delta = sharpened.rgba[edge] - image.rgba[edge];
            require(delta > previous && near(sharpened.rgba[edge + 1] / sharpened.rgba[edge], .5) &&
                        near(sharpened.rgba[edge + 2] / sharpened.rgba[edge], .25),
                    "Output sharpening strength/color ratios are wrong.");
            previous = delta;
        }
    }
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        image.rgba[i] = .3F;
        image.rgba[i + 1] = .2F;
        image.rgba[i + 2] = .1F;
        image.rgba[i + 3] = (i / 4) % 3 ? 1 : 0;
    }
    const auto before = image;
    luma::sharpenOutput(image, options);
    for (std::size_t i = 0; i < image.rgba.size(); ++i)
        require(near(image.rgba[i], before.rgba[i]),
                "Output sharpening introduced alpha-edge halos on a constant field.");
}
void profiles() {
    const auto input = luma::linearSRGBProfile();
    const auto matrix = luma::rgbToXYZD50(input);
    const auto identity = luma::multiplyColorMatrices(luma::inverseColorMatrix(matrix), matrix);
    for (std::size_t i = 0; i < 9; ++i)
        require(near(identity[i], i % 4 == 0 ? 1 : 0, 1e-10), "ICC matrix inverse is incorrect.");
    const std::array<float, 3> gray{.18F, .18F, .18F};
    for (const auto space : {luma::ExportColorSpace::srgb, luma::ExportColorSpace::displayP3,
                             luma::ExportColorSpace::adobeRGB, luma::ExportColorSpace::proPhoto}) {
        const luma::macos::ExportColor color(space);
        std::array<float, 3> encoded{}, linear{};
        color.convert(gray, encoded, false);
        color.convert(gray, linear, true);
        const double expected = space == luma::ExportColorSpace::proPhoto ? std::pow(.18, 1 / 1.8)
                                : space == luma::ExportColorSpace::adobeRGB
                                    ? std::pow(.18, 1 / 2.19921875)
                                    : 1.055 * std::pow(.18, 1 / 2.4) - .055;
        for (std::size_t c = 0; c < 3; ++c)
            require(near(encoded[c], expected, .002) && near(linear[c], .18, .001),
                    "Named output ICC transfer function or neutral white is incorrect.");
        const auto &m = color.colorants();
        require(near(m[0] + m[1] + m[2], .9642, .002) && near(m[3] + m[4] + m[5], 1, .002) &&
                    near(m[6] + m[7] + m[8], .8249, .002),
                "Output ICC colorants do not map neutral RGB to D50 PCS.");
    }
}
void dng() {
    struct Folder {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() / ("luma-export-" + luma::newID());
        Folder() {
            require(std::filesystem::create_directory(path),
                    "Cannot create owned export fixture directory.");
        }
        ~Folder() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } folder;
    luma::LinearOutput image(64, 48);
    const std::array<std::array<float, 3>, 6> palette{{{.08F, .12F, .3F},
                                                       {.8F, .05F, .03F},
                                                       {.1F, .7F, .15F},
                                                       {.18F, .18F, .18F},
                                                       {.9F, .9F, .9F},
                                                       {0, 0, 0}}};
    for (std::size_t i = 0; i < image.rgba.size() / 4; ++i) {
        for (std::size_t c = 0; c < 3; ++c)
            image.rgba[i * 4 + c] = palette[i % palette.size()][c];
        image.rgba[i * 4 + 3] = 1;
    }
    image.rgba[3] = 0;
    image.rgba[7] = .5F;
    int index = 0;
    for (auto space : {luma::ExportColorSpace::srgb, luma::ExportColorSpace::displayP3,
                       luma::ExportColorSpace::adobeRGB, luma::ExportColorSpace::proPhoto}) {
        const luma::macos::ExportColor color(space);
        const auto path = folder.path / ("rendered-" + std::to_string(index++) + ".dng");
        luma::writeLinearDNG(
            image, color.colorants(),
            [&](auto input, auto output) { color.convert(input, output, true); }, path,
            "Luma Test Profile");
        // LibRaw is an independent TIFF/DNG parser, separate from the writer.
        const auto raw = std::make_unique<LibRaw>();
        require(raw->open_file(path.c_str()) == LIBRAW_SUCCESS && raw->unpack() == LIBRAW_SUCCESS &&
                    raw->imgdata.idata.dng_version != 0 && raw->imgdata.idata.filters == 0 &&
                    raw->imgdata.idata.colors == 3 && raw->imgdata.rawdata.color4_image,
                "Independent LibRaw parser rejected LinearRaw output.");
        require(raw->imgdata.sizes.width == 64 && raw->imgdata.sizes.height == 48 &&
                    raw->imgdata.color.maximum == 65535,
                "DNG samples/depth/geometry are incorrect.");
        const auto decoded = luma::RawDecoder().decode(path);
        require(decoded.diagnostics.decoder == "LibRaw" &&
                    !decoded.diagnostics.decoderVersion.empty() &&
                    decoded.diagnostics.source == luma::DecodeSource::renderedLinearDNG &&
                    decoded.diagnostics.reconstruction == luma::DecodeReconstruction::none &&
                    decoded.diagnostics.color == luma::DecodeColorPath::dngProfileToLinearSRGB &&
                    decoded.diagnostics.activeWidth == 64 && decoded.diagnostics.activeHeight == 48,
                "Rendered DNG decoder diagnostics are incomplete or incorrect.");
        const auto inspected = luma::RawDecoder().inspect(path);
        require(inspected.width == 64 && inspected.height == 48 &&
                    decoded.metadata.orientation == 0,
                "Rendered DNG could not be reimported with upright geometry.");
        const luma::DevelopPipeline pipeline({}, decoded.calibration);
        for (std::size_t i = 0; i < image.rgba.size() / 4; ++i) {
            const auto actual =
                pipeline.pixel({decoded.image.rgb[i * 3], decoded.image.rgb[i * 3 + 1],
                                decoded.image.rgb[i * 3 + 2]});
            for (std::size_t c = 0; c < 3; ++c)
                require(near(actual[c], image.rgba[i * 4 + c] * image.rgba[i * 4 + 3], .0005),
                        "DNG reimport changed linear sRGB colors/opacity after selected-space "
                        "conversion.");
        }
        auto url = CFURLCreateFromFileSystemRepresentation(
            nullptr, reinterpret_cast<const UInt8 *>(path.c_str()),
            static_cast<CFIndex>(path.string().size()), false);
        auto source = url ? CGImageSourceCreateWithURL(url, nullptr) : nullptr;
        if (url)
            CFRelease(url);
        require(source && CGImageSourceGetCount(source) > 0,
                "ImageIO did not recognize DNG output.");
        CFRelease(source);
    }
    rejects([&] {
        luma::writeLinearDNG(image, {}, [](auto, auto) {}, folder.path / "invalid.dng", "Invalid");
    });
}

} // namespace
int main() {
    try {
        settings();
        resampling();
        sharpening();
        profiles();
        dng();
        exportNativeTests();
        std::cout << "Export options, alpha/HDR resize, output sharpening and four named ICC "
                     "spaces passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
