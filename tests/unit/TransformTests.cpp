#include "core/render/CropGeometry.h"
#include "core/render/PerspectiveCorrection.h"
#include "platform/macos/ImageExporter.h"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <unistd.h>

namespace {
template <class T> class NativeObject {
  public:
    explicit NativeObject(T value) : value_(value) {
        if (!value)
            throw std::runtime_error("Cannot allocate image verification object.");
    }
    ~NativeObject() {
        CFRelease(value_);
    }
    T get() const {
        return value_;
    }
    NativeObject(const NativeObject &) = delete;
    NativeObject &operator=(const NativeObject &) = delete;

  private:
    T value_;
};
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Expected invalid projective geometry to be refused.");
}
bool near(double a, double b, double epsilon = 1e-9) {
    return std::abs(a - b) < epsilon;
}
luma::Homography observed() {
    return {{0.85, -0.06, 0, 0.04, 0.85, 0, 0.13, -0.12, 1}};
}
std::array<double, 2> distorted(double x, double y) {
    // Independent physical fixture: project an upright planar building grid.
    const double denominator = 1 + 0.13 * x - 0.12 * y;
    return {(0.85 * x - 0.06 * y) / denominator, (0.04 * x + 0.85 * y) / denominator};
}
luma::PerspectiveGuide line(luma::GuideAxis axis, double position) {
    auto a =
        axis == luma::GuideAxis::vertical ? distorted(position, -0.75) : distorted(-0.75, position);
    auto b =
        axis == luma::GuideAxis::vertical ? distorted(position, 0.75) : distorted(0.75, position);
    for (std::size_t i = 0; i < 2; ++i) {
        a[i] = (a[i] + 1) / 2;
        b[i] = (b[i] + 1) / 2;
    }
    return {axis, a, b};
}
void parallel(const luma::TransformSettings &solution, const luma::PerspectiveGuide &guide,
              double epsilon) {
    const auto matrix = solution.guideMatrix();
    const auto a = matrix.point(guide.first[0] * 2 - 1, guide.first[1] * 2 - 1);
    const auto b = matrix.point(guide.second[0] * 2 - 1, guide.second[1] * 2 - 1);
    const auto component = guide.axis == luma::GuideAxis::vertical ? 0U : 1U;
    require(near(a[component], b[component], epsilon),
            "Guided line did not become vertical/horizontal.");
}
void mathematics() {
    const auto matrix = observed(), inverse = matrix.inverse();
    for (double x : {-1.0, -0.3, 0.0, 0.7, 1.0})
        for (double y : {-1.0, 0.2, 1.0}) {
            const auto expected = distorted(x, y), point = matrix.point(x, y),
                       source = inverse.point(point[0], point[1]);
            require(near(point[0], expected[0]) && near(point[1], expected[1]) &&
                        near(source[0], x) && near(source[1], y),
                    "Homography direction or inverse is wrong.");
        }
    std::vector<luma::PerspectiveGuide> guides{
        line(luma::GuideAxis::vertical, -0.55), line(luma::GuideAxis::vertical, 0.55),
        line(luma::GuideAxis::horizontal, -0.5), line(luma::GuideAxis::horizontal, 0.5)};
    for (const auto &subset : {guides, std::vector<luma::PerspectiveGuide>{guides[0], guides[1]},
                               std::vector<luma::PerspectiveGuide>{guides[2], guides[3]}}) {
        const auto solved = luma::solvePerspectiveGuides(subset, 4.0 / 3);
        for (const auto &guide : subset)
            parallel(solved, guide, 1e-10);
    }
    rejects([&] { luma::solvePerspectiveGuides({guides[0]}, 1.5); });
    rejects([&] { luma::solvePerspectiveGuides({guides[0], guides[0]}, 1.5); });
    auto shortLine = guides[0];
    shortLine.second = shortLine.first;
    rejects([&] { shortLine.validate(); });
    auto outside = guides[0];
    outside.first[0] = -0.1;
    rejects([&] { outside.validate(); });
    luma::Homography singular{{0, 0, 0, 0, 0, 0, 0, 0, 1}};
    rejects([&] { singular.inverse(); });
    auto nonfinite = matrix;
    nonfinite.values[2] = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { nonfinite.validate(); });
    luma::EditDocument edits;
    const auto original = edits;
    rejects([&] { edits.setParameter("transform.constrainCrop", 0.5); });
    rejects([&] { edits.setParameter("transform.rotate", 46); });
    rejects([&] { edits.setParameter("transform.scale", 49); });
    rejects([&] { edits.setParameter("transform.m11", 0); });
    require(edits == original, "Rejected Transform partially mutated the document.");
    edits.setParameters({{"transform.m11", 0},
                         {"transform.m22", 0},
                         {"transform.m12", 1},
                         {"transform.m21", -1},
                         {"transform.guided", 1}});
    require(edits.transform.guided && edits.transform.guide[0] == 0,
            "Compound matrix validation depended on parameter order.");
}
void geometry() {
    luma::FloatImage image(121, 81);
    for (std::size_t i = 0; i < image.rgb.size(); ++i)
        image.rgb[i] = static_cast<float>((i * 17) % 53) / 52;
    for (int orientation = 0; orientation < 8; ++orientation) {
        luma::EditDocument baseline;
        const auto old = luma::renderCPU(image, baseline, orientation);
        auto disabled = baseline;
        disabled.transform.rotate = 17;
        disabled.setEnabled(luma::Correction::transform, false);
        require(luma::renderCPU(image, disabled, orientation).rgba == old.rgba,
                "Disabled Transform changed old pixels.");
        for (int variant = 0; variant < 4; ++variant) {
            auto edits = baseline;
            edits.transform.rotate = variant % 2 ? -12 : 9;
            edits.transform.horizontal = variant * 15 - 20;
            edits.transform.vertical = 20 - variant * 11;
            edits.transform.scale = 80 + variant * 20;
            edits.transform.aspect = variant * 10 - 15;
            edits.transform.xOffset = variant * 6 - 9;
            edits.transform.yOffset = 9 - variant * 5;
            edits.crop.quarterTurns = variant;
            edits.crop.angle = variant * 3;
            edits.crop.flipHorizontal = variant % 2;
            edits.crop.x = 0.1;
            edits.crop.width = 0.8;
            const luma::CropGeometry transformed(image.width, image.height, orientation, edits);
            require(transformed.opaque() && transformed.transformZoom() >= 1,
                    "Constrain Crop lost opacity.");
            for (int y = 0; y < transformed.height(); ++y)
                for (int x = 0; x < transformed.width(); ++x) {
                    const auto point = transformed.source(x, y);
                    require(point[0] >= -1e-7 && point[1] >= -1e-7 && point[0] <= 120 + 1e-7 &&
                                point[1] <= 80 + 1e-7,
                            "Constrained homography/crop/orientation escaped source bounds.");
                    require(transformed.sample(
                                x, y, [](int, int) { return std::array<float, 3>{2, -1, 0.5}; }) ==
                                std::array<float, 3>{2, -1, 0.5},
                            "Geometry clipped signed HDR values.");
                }
        }
    }
    luma::EditDocument edits;
    edits.transform.scale = 50;
    edits.transform.constrainCrop = false;
    const auto rendered = luma::renderCPU(image, edits);
    const auto center = (static_cast<std::size_t>(40) * 121 + 60) * 4;
    require(rendered.rgba[3] == 0 && rendered.rgba[center + 3] == 255,
            "Exterior was replicated instead of transparent.");
    edits.transform.horizontal = edits.transform.vertical = 100;
    rejects([&] { luma::CropGeometry unsafe(121, 81, 0, edits); });
    edits.transform.constrainCrop = true;
    require(luma::CropGeometry(121, 81, 0, edits).opaque(),
            "Constrain could not remove an inverse pole.");
}
luma::DisplayImage building() {
    constexpr int width = 320, height = 240;
    luma::DisplayImage image{width, height, std::vector<std::uint8_t>(width * height * 4, 255)};
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const double u = 2.0 * x / (width - 1) - 1, v = 2.0 * y / (height - 1) - 1;
            const double a = 0.85 - u * 0.13, b = -0.06 + u * 0.12, c = 0.04 - v * 0.13,
                         d = 0.85 + v * 0.12;
            const double sx = (u * d - b * v) / (a * d - b * c),
                         sy = (a * v - u * c) / (a * d - b * c);
            const bool edge = (std::abs(std::abs(sx) - 0.55) < 0.009 && std::abs(sy) < 0.8) ||
                              (std::abs(std::abs(sy) - 0.5) < 0.009 && std::abs(sx) < 0.8);
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
            image.rgba[offset] = image.rgba[offset + 1] = image.rgba[offset + 2] = edge ? 12 : 220;
        }
    return image;
}
void automatic() {
    auto image = building();
    const auto guides = luma::detectPerspectiveGuides(image);
    require(guides.size() == 4, "Auto did not find both confident building directions.");
    const auto solved = luma::solvePerspectiveGuides(guides, 319.0 / 239);
    for (const auto &guide :
         {line(luma::GuideAxis::vertical, -0.55), line(luma::GuideAxis::vertical, 0.55),
          line(luma::GuideAxis::horizontal, -0.5), line(luma::GuideAxis::horizontal, 0.5)})
        parallel(solved, guide, 0.025);
    luma::EditDocument edits;
    edits.transform = solved;
    require(luma::CropGeometry(320, 240, 0, edits).opaque(), "Detected Auto solution is unsafe.");
    std::fill(image.rgba.begin(), image.rgba.end(), 128);
    rejects([&] { luma::detectPerspectiveGuides(image); });
    std::uint32_t random = 17;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        random = random * 1664525 + 1013904223;
        image.rgba[i] = image.rgba[i + 1] = image.rgba[i + 2] =
            static_cast<std::uint8_t>(random >> 24);
        image.rgba[i + 3] = 255;
    }
    rejects([&] { luma::detectPerspectiveGuides(image); });
    luma::CancellationSource stop;
    stop.cancel();
    rejects([&] { luma::detectPerspectiveGuides(image, stop.token()); });
}
void encodedTransparency() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("luma-transform-alpha-" + std::to_string(getpid()));
    require(std::filesystem::create_directory(root), "Verification directory already exists.");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{root};
    luma::DecodedRaw decoded{luma::FloatImage(121, 81), {}, {}, {}};
    std::fill(decoded.image.rgb.begin(), decoded.image.rgb.end(), 0.25F);
    luma::EditDocument edits;
    edits.transform.scale = 50;
    edits.transform.constrainCrop = false;
    for (const auto format :
         {luma::ExportFormat::png, luma::ExportFormat::tiff16, luma::ExportFormat::jpeg}) {
        const auto path = root / (format == luma::ExportFormat::png      ? "alpha.png"
                                  : format == luma::ExportFormat::tiff16 ? "alpha.tif"
                                                                         : "opaque.jpg");
        luma::macos::writeImage(decoded, edits, path, {format, 92, false});
        const auto name = path.string();
        NativeObject url(CFURLCreateFromFileSystemRepresentation(
            nullptr, reinterpret_cast<const UInt8 *>(name.data()),
            static_cast<CFIndex>(name.size()), false));
        NativeObject source(CGImageSourceCreateWithURL(url.get(), nullptr));
        NativeObject image(CGImageSourceCreateImageAtIndex(source.get(), 0, nullptr));
        require(CGImageGetWidth(image.get()) == 121 && CGImageGetHeight(image.get()) == 81,
                "Geometry export changed canvas dimensions.");
        if (format == luma::ExportFormat::tiff16)
            require(CGImageGetBitsPerComponent(image.get()) == 16,
                    "Transparent TIFF lost 16-bit precision.");
        std::vector<std::uint8_t> pixels(121 * 81 * 4);
        NativeObject space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
        NativeObject context(CGBitmapContextCreate(pixels.data(), 121, 81, 8, 121 * 4, space.get(),
                                                   kCGImageAlphaPremultipliedLast));
        CGContextDrawImage(context.get(), CGRectMake(0, 0, 121, 81), image.get());
        const auto center = (static_cast<std::size_t>(40) * 121 + 60) * 4;
        require(pixels[center + 3] == 255, "Encoded center lost coverage.");
        if (format == luma::ExportFormat::jpeg)
            require(pixels[3] == 255 && pixels[0] <= 2 && pixels[1] <= 2 && pixels[2] <= 2,
                    "JPEG exterior is not opaque black.");
        else
            require(pixels[3] == 0, "Encoded source exterior lost transparency.");
    }
}
} // namespace
int main() {
    try {
        mathematics();
        geometry();
        automatic();
        encodedTransparency();
        std::cout << "Projective math, Guided/Auto, EXIF/Crop, safe/transparent bounds, HDR and "
                     "cancellation passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
