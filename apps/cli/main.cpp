#include "apps/cli/Json.h"
#include "apps/cli/LibraryCommands.h"
#include "apps/shared/SessionService.h"
#include "core/lens/LensProfiles.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
using luma::cli::quoted;
template <typename E, std::size_t N>
E exportChoice(const std::string &value, const std::array<const char *, N> &choices) {
    for (std::size_t i = 0; i < N; ++i)
        if (value == choices[i])
            return static_cast<E>(i);
    throw std::runtime_error("Unknown export option value: " + value);
}
double exportNumber(const std::string &text) {
    std::size_t consumed = 0;
    const auto value = std::stod(text, &consumed);
    if (consumed != text.size())
        throw std::runtime_error("Invalid export numeric value.");
    return value;
}
void printSession(const luma::PhotoSession &session) {
    const auto &asset = session.asset;
    const auto optics = luma::resolveLens(asset.metadata, asset.edits.lens);
    auto number = [](std::optional<double> value) {
        return value ? std::to_string(*value) : std::string("null");
    };
    std::cout << "{\"asset\":" << quoted(asset.id)
              << ",\"camera\":" << quoted(asset.metadata.camera)
              << ",\"lens\":" << quoted(asset.metadata.lens)
              << ",\"iso\":" << number(asset.metadata.iso)
              << ",\"shutter\":" << number(asset.metadata.shutterSeconds)
              << ",\"aperture\":" << number(asset.metadata.aperture)
              << ",\"focalLength\":" << number(asset.metadata.focalLength)
              << ",\"width\":" << asset.metadata.width << ",\"height\":" << asset.metadata.height
              << ",\"exposure\":" << std::setprecision(17) << asset.edits.exposure
              << ",\"temperature\":" << asset.edits.temperature << ",\"tint\":" << asset.edits.tint
              << ",\"customWhiteBalance\":" << (asset.edits.customWhiteBalance ? "true" : "false")
              << ",\"contrast\":" << asset.edits.contrast
              << ",\"highlights\":" << asset.edits.highlights
              << ",\"shadows\":" << asset.edits.shadows << ",\"whites\":" << asset.edits.whites
              << ",\"blacks\":" << asset.edits.blacks << ",\"vibrance\":" << asset.edits.vibrance
              << ",\"saturation\":" << asset.edits.saturation
              << ",\"enabledCorrections\":" << asset.edits.enabledCorrections
              << ",\"editSchemaVersion\":" << luma::EditDocument::schemaVersion
              << ",\"curveParametric\":{\"shadows\":" << asset.edits.curveShadows
              << ",\"darks\":" << asset.edits.curveDarks
              << ",\"lights\":" << asset.edits.curveLights
              << ",\"highlights\":" << asset.edits.curveHighlights << "},\"curves\":{";
    for (std::size_t c = 0; c < luma::curveChannels.size(); ++c) {
        if (c != 0)
            std::cout << ',';
        std::cout << quoted(std::string(luma::curveChannels[c])) << ":[";
        const auto &points = asset.edits.curves[c].points;
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (i != 0)
                std::cout << ',';
            std::cout << '[' << points[i].x << ',' << points[i].y << ']';
        }
        std::cout << ']';
    }
    std::cout << "},\"hsl\":{";
    for (std::size_t b = 0; b < luma::hslBands.size(); ++b) {
        if (b != 0)
            std::cout << ',';
        const auto &band = asset.edits.hsl[b];
        std::cout << quoted(std::string(luma::hslBands[b])) << ":{\"hue\":" << band.hue
                  << ",\"saturation\":" << band.saturation << ",\"luminance\":" << band.luminance
                  << '}';
    }
    std::cout << "},\"geometry\":{";
    for (std::size_t i = 0; i < luma::transformParameters.size(); ++i) {
        if (i)
            std::cout << ',';
        const auto name = luma::transformParameters[i];
        std::cout << quoted(std::string(name)) << ':' << asset.edits.transform.parameter(name);
    }
    std::cout << "},\"sharpening\":{\"amount\":" << asset.edits.sharpenAmount
              << ",\"radius\":" << asset.edits.sharpenRadius
              << ",\"detail\":" << asset.edits.sharpenDetail
              << ",\"masking\":" << asset.edits.sharpenMasking
              << "},\"noiseReduction\":{\"luminance\":" << asset.edits.noiseLuminance
              << ",\"luminanceDetail\":" << asset.edits.noiseLuminanceDetail
              << ",\"luminanceContrast\":" << asset.edits.noiseLuminanceContrast
              << ",\"color\":" << asset.edits.noiseColor
              << ",\"colorDetail\":" << asset.edits.noiseColorDetail
              << ",\"colorSmoothness\":" << asset.edits.noiseColorSmoothness
              << "},\"crop\":{\"x\":" << asset.edits.crop.x << ",\"y\":" << asset.edits.crop.y
              << ",\"width\":" << asset.edits.crop.width
              << ",\"height\":" << asset.edits.crop.height
              << ",\"angle\":" << asset.edits.crop.angle
              << ",\"aspect\":" << asset.edits.crop.aspect
              << ",\"quarterTurns\":" << asset.edits.crop.quarterTurns
              << ",\"flipHorizontal\":" << (asset.edits.crop.flipHorizontal ? "true" : "false")
              << ",\"flipVertical\":" << (asset.edits.crop.flipVertical ? "true" : "false")
              << "},\"optics\":{\"profile\":" << quoted(asset.edits.lens.profile)
              << ",\"resolvedProfile\":"
              << quoted(optics.profile ? std::string(optics.profile->id) : "")
              << ",\"status\":" << quoted(optics.status)
              << ",\"distortion\":" << asset.edits.lens.distortion
              << ",\"vignetting\":" << asset.edits.lens.vignetting << ",\"chromaticAberration\":"
              << (asset.edits.lens.chromaticAberration ? "true" : "false")
              << "},\"checksum\":" << quoted(asset.checksum) << "}\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        std::filesystem::path catalog;
        std::vector<std::string> arguments;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--catalog" && i + 1 < argc) {
                catalog = argv[++i];
            } else {
                arguments.emplace_back(argv[i]);
            }
        }
        if (arguments.empty() || arguments[0] == "--help") {
            std::cout
                << "Apertone Library and Develop\nUsage: apertone open RAW --catalog FILE\n"
                << "       apertone exposure EV --catalog FILE\n"
                << "       apertone restore --catalog FILE\n"
                << "       apertone lens profiles | lens profile auto|ID --catalog FILE\n"
                << "       apertone develop PARAM=VALUE... --catalog FILE\n"
                << "       apertone curve rgb|red|green|blue X:Y X:Y... --catalog FILE\n"
                << "       apertone enable|disable CORRECTION --catalog FILE\n"
                << "       apertone wb as-shot --catalog FILE\n"
                << "       apertone export DEST [--quality N] [--overwrite] --catalog FILE\n"
                << "       apertone catalog verify --catalog FILE\n"
                << "       apertone import add SOURCES... | import copy DEST SOURCES... "
                   "--catalog FILE\n"
                << "       apertone library [--search TEXT] [--rating N] [--flag N] [--label "
                   "COLOR] [--collection ID] [--offset N] [--limit N] --catalog FILE\n"
                << "       apertone rate|flag|label VALUE IDS... --catalog FILE\n"
                << "       apertone collections | collection create NAME | collection rename ID "
                   "NAME --catalog FILE\n"
                << "       apertone collection add|remove ID ASSETS... | collection delete ID "
                   "--catalog FILE\n"
                << "       apertone recover --catalog FILE\n";
            return 0;
        }
        if (catalog.empty()) {
            throw std::runtime_error("Specify --catalog FILE explicitly.");
        }
        if (luma::cli::libraryCommand(catalog, arguments))
            return 0;
        luma::SessionService service(catalog);
        if (arguments[0] == "open" && arguments.size() == 2) {
            printSession(service.open(arguments[1]));
        } else if (arguments[0] == "restore" && arguments.size() == 1) {
            const auto session = service.restore();
            if (session) {
                printSession(*session);
            } else {
                std::cout << "{\"asset\":null}\n";
            }
        } else if (arguments[0] == "exposure" && arguments.size() == 2) {
            std::size_t consumed = 0;
            const double ev = std::stod(arguments[1], &consumed);
            if (consumed != arguments[1].size()) {
                throw std::runtime_error("Invalid exposure number.");
            }
            luma::Catalog connection(catalog);
            const auto asset = connection.activeAsset();
            if (!asset) {
                throw std::runtime_error("No active photo in this catalog.");
            }
            service.save(asset->id, ev);
            std::cout << "{\"saved\":true}\n";
        } else if (arguments[0] == "lens" && arguments.size() == 2 && arguments[1] == "profiles") {
            std::cout << '[';
            bool first = true;
            for (const auto &profile : luma::lensProfiles()) {
                if (!first)
                    std::cout << ',';
                first = false;
                std::cout << "{\"id\":" << quoted(std::string(profile.id))
                          << ",\"name\":" << quoted(std::string(profile.name)) << '}';
            }
            std::cout << "]\n";
        } else if (arguments[0] == "lens" && arguments.size() == 3 && arguments[1] == "profile") {
            auto session = service.restore();
            if (!session)
                throw std::runtime_error("Open a RAW before editing.");
            session->asset.edits.lens.profile = arguments[2] == "auto" ? "" : arguments[2];
            session->asset.edits.validate();
            if (!session->asset.edits.lens.profile.empty() &&
                !luma::resolveLens(session->asset.metadata, session->asset.edits.lens).profile)
                throw std::runtime_error("Lens profile is unavailable for this camera.");
            service.save(session->asset.id, session->asset.edits);
            printSession(*session);
        } else if (arguments[0] == "develop" && arguments.size() >= 2) {
            luma::Catalog connection(catalog);
            auto asset = connection.activeAsset();
            if (!asset)
                throw std::runtime_error("No active photo in this catalog.");
            std::vector<std::pair<std::string, double>> values;
            for (std::size_t i = 1; i < arguments.size(); ++i) {
                const auto separator = arguments[i].find('=');
                if (separator == std::string::npos)
                    throw std::runtime_error("Expected PARAM=VALUE.");
                const auto name = arguments[i].substr(0, separator);
                const auto text = arguments[i].substr(separator + 1);
                std::size_t consumed = 0;
                const auto value = std::stod(text, &consumed);
                if (consumed != text.size())
                    throw std::runtime_error("Invalid edit number.");
                values.emplace_back(name, value);
            }
            asset->edits.setParameters(values);
            service.save(asset->id, asset->edits);
            std::cout << "{\"saved\":true}\n";
        } else if (arguments[0] == "curve" && arguments.size() >= 4) {
            luma::Catalog connection(catalog);
            auto asset = connection.activeAsset();
            if (!asset)
                throw std::runtime_error("No active photo in this catalog.");
            auto &points =
                asset->edits.curves[luma::EditDocument::curveChannel(arguments[1])].points;
            points.clear();
            for (std::size_t i = 2; i < arguments.size(); ++i) {
                const auto separator = arguments[i].find(':');
                if (separator == std::string::npos)
                    throw std::runtime_error("Expected curve points as X:Y.");
                auto number = [](const std::string &text) {
                    std::size_t count = 0;
                    const double value = std::stod(text, &count);
                    if (count != text.size())
                        throw std::runtime_error("Invalid curve coordinate.");
                    return value;
                };
                points.push_back({number(arguments[i].substr(0, separator)),
                                  number(arguments[i].substr(separator + 1))});
            }
            service.save(asset->id, asset->edits);
            std::cout << "{\"saved\":true}\n";
        } else if ((arguments[0] == "enable" || arguments[0] == "disable") &&
                   arguments.size() == 2) {
            luma::Catalog connection(catalog);
            auto asset = connection.activeAsset();
            if (!asset)
                throw std::runtime_error("No active photo in this catalog.");
            asset->edits.setEnabled(luma::EditDocument::correction(arguments[1]),
                                    arguments[0] == "enable");
            service.save(asset->id, asset->edits);
            std::cout << "{\"saved\":true}\n";
        } else if (arguments == std::vector<std::string>{"wb", "as-shot"}) {
            luma::Catalog connection(catalog);
            auto asset = connection.activeAsset();
            if (!asset)
                throw std::runtime_error("No active photo in this catalog.");
            asset->edits.customWhiteBalance = false;
            service.save(asset->id, asset->edits);
            std::cout << "{\"saved\":true}\n";
        } else if (arguments[0] == "export" && arguments.size() >= 2) {
            luma::Catalog connection(catalog);
            const auto asset = connection.activeAsset();
            if (!asset)
                throw std::runtime_error("No active photo in this catalog.");
            std::filesystem::path destination(arguments[1]);
            auto extension = destination.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            luma::ExportOptions options;
            if (extension == ".png")
                options.format = luma::ExportFormat::png;
            else if (extension == ".tif" || extension == ".tiff")
                options.format = luma::ExportFormat::tiff16;
            else if (extension == ".dng")
                options.format = luma::ExportFormat::dng;
            else if (extension == ".heic")
                options.format = luma::ExportFormat::heicHDR;
            for (std::size_t i = 2; i < arguments.size(); ++i) {
                const auto key = arguments[i];
                if (key == "--overwrite") {
                    options.overwrite = true;
                    continue;
                }
                if (++i == arguments.size())
                    throw std::runtime_error("Missing export option value.");
                const auto &value = arguments[i];
                if (key == "--quality") {
                    std::size_t consumed = 0;
                    options.quality = std::stoi(value, &consumed);
                    if (consumed != value.size())
                        throw std::runtime_error("Invalid JPEG quality.");
                } else if (key == "--format")
                    options.format = exportChoice<luma::ExportFormat>(
                        value, std::array{"jpeg", "png", "tiff16", "tiff8", "dng", "heic-hdr"});
                else if (key == "--color-space")
                    options.colorSpace = exportChoice<luma::ExportColorSpace>(
                        value, std::array{"srgb", "p3", "adobe-rgb", "prophoto"});
                else if (key == "--compression")
                    options.compression = exportChoice<luma::TiffCompression>(
                        value, std::array{"none", "lzw", "zip"});
                else if (key == "--resize")
                    options.resize = exportChoice<luma::ResizeMode>(
                        value, std::array{"original", "width", "height", "long", "short", "mp",
                                          "percent"});
                else if (key == "--size")
                    options.resizeValue = exportNumber(value);
                else if (key == "--output-sharpen")
                    options.outputMedium = exportChoice<luma::OutputMedium>(
                        value, std::array{"off", "screen", "matte", "glossy"});
                else if (key == "--output-amount")
                    options.outputAmount = exportChoice<luma::SharpenAmount>(
                        value, std::array{"low", "standard", "high"});
                else if (key == "--metadata")
                    options.metadata = exportChoice<luma::MetadataPolicy>(
                        value, std::array{"none", "all", "copyright", "remove-location"});
                else if (key == "--watermark-text") {
                    options.watermark.kind = luma::WatermarkKind::text;
                    options.watermark.text = value;
                } else if (key == "--watermark-image") {
                    options.watermark.kind = luma::WatermarkKind::image;
                    options.watermark.image = value;
                } else if (key == "--watermark-opacity")
                    options.watermark.opacity = exportNumber(value);
                else if (key == "--watermark-size")
                    options.watermark.size = exportNumber(value);
                else if (key == "--watermark-margin")
                    options.watermark.margin = exportNumber(value);
                else if (key == "--watermark-anchor") {
                    std::size_t consumed = 0;
                    options.watermark.anchor = std::stoi(value, &consumed);
                    if (consumed != value.size())
                        throw std::runtime_error("Invalid watermark anchor.");
                } else
                    throw std::runtime_error("Unknown export option: " + key);
            }
            service.exportPhoto(*asset, asset->edits, destination, options);
            std::cout << "{\"exported\":" << quoted(destination.string()) << "}\n";
        } else if (arguments == std::vector<std::string>{"catalog", "verify"}) {
            service.verify();
            std::cout << "{\"integrity\":\"ok\"}\n";
        } else {
            throw std::runtime_error("Unknown command. Use --help.");
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Apertone: " << error.what() << '\n';
        return 1;
    }
}
