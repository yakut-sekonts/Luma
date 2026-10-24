#include "core/edits/LocalMasks.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>

namespace luma {
namespace {
void require(bool valid, const char *message) {
    if (!valid)
        throw std::runtime_error(message);
}
bool validName(std::string_view name) {
    if (name.empty() || name.size() > 320 || name.front() == ' ' || name.back() == ' ')
        return false;
    std::size_t count = 0;
    for (std::size_t i = 0; i < name.size();) {
        const auto first = static_cast<unsigned char>(name[i++]);
        unsigned value = first;
        int remaining = 0;
        unsigned minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            value = first & 31U;
            remaining = 1;
            minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            value = first & 15U;
            remaining = 2;
            minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            value = first & 7U;
            remaining = 3;
            minimum = 0x10000;
        } else if (first >= 0x80)
            return false;
        while (remaining-- > 0) {
            if (i == name.size())
                return false;
            const auto next = static_cast<unsigned char>(name[i++]);
            if ((next & 0xc0U) != 0x80U)
                return false;
            value = (value << 6U) | (next & 63U);
        }
        if (value < minimum || value < 0x20 || (value >= 0x7f && value <= 0x9f) ||
            (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff || ++count > 80)
            return false;
    }
    return true;
}
void range(double value, double lower, double upper) {
    require(std::isfinite(value) && value >= lower && value <= upper,
            "Local mask parameter is out of range.");
}
std::size_t parameterIndex(std::string_view name) {
    const auto it = std::find(localParameters.begin(), localParameters.end(), name);
    require(it != localParameters.end(), "Unknown local adjustment parameter.");
    return static_cast<std::size_t>(it - localParameters.begin());
}
void validID(std::string_view id) {
    require(id.size() == 36, "Invalid local mask ID.");
    for (std::size_t i = 0; i < id.size(); ++i) {
        const bool separator = i == 8 || i == 13 || i == 18 || i == 23;
        const char c = id[i];
        require(separator ? c == '-' : ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')),
                "Invalid local mask ID.");
    }
}
} // namespace
void MaskGeometry::validate() const {
    require(static_cast<unsigned>(shape) <= static_cast<unsigned>(MaskShape::bitmap),
            "Unsupported local mask shape.");
    require(shape == MaskShape::bitmap ? !bitmap.empty() : bitmap.empty(),
            "Bitmap data requires a bitmap selection.");
    for (double value : {x1, y1, x2, y2})
        range(value, -2, 3);
    if (shape == MaskShape::linear)
        require(std::hypot(x2 - x1, y2 - y1) >= 0.001, "Gradient endpoints are too close.");
    if (shape == MaskShape::radial) {
        range(x2, 0.001, 3);
        range(y2, 0.001, 3);
    }
    range(angle, -180, 180);
    range(feather, 0, 100);
    range(rangeLow, 0, 1);
    range(rangeHigh, rangeLow, 1);
    range(colorTolerance, 0, 1);
    for (double value : referenceColor)
        range(value, 0, 1);
    require(strokes.size() <= 256, "A brush supports at most 256 strokes.");
    require(shape == MaskShape::brush || strokes.empty(), "Strokes require a brush primitive.");
    std::size_t points = 0;
    for (const auto &stroke : strokes) {
        range(stroke.radius, 0.0001, 1);
        range(stroke.opacity, 0, 100);
        require(!stroke.points.empty(), "A brush stroke needs a point.");
        points += stroke.points.size();
        require(points <= 16384, "Brush point limit exceeded.");
        for (const auto &point : stroke.points) {
            range(point.x, -2, 3);
            range(point.y, -2, 3);
        }
    }
}
void LocalMask::validate() const {
    validID(id);
    require(validName(name), "Mask name must contain 1–80 printable characters.");
    MaskGeometry::validate();
    range(opacity, 0, 100);
    for (std::size_t i = 0; i < adjustments.size(); ++i)
        range(adjustments[i], i == 0 ? -5 : -100, i == 0 ? 5 : 100);
    require(components.size() <= 64, "A mask supports at most 64 components.");
    std::set<std::string> ids{id};
    for (const auto &component : components) {
        validID(component.id);
        require(ids.insert(component.id).second, "Duplicate mask component ID.");
        require(static_cast<unsigned>(component.operation) <=
                    static_cast<unsigned>(MaskOperation::intersect),
                "Invalid mask operation.");
        component.geometry.validate();
    }
}
bool LocalMask::neutral() const {
    return !enabled || opacity == 0 ||
           std::all_of(adjustments.begin(), adjustments.end(), [](double v) { return v == 0; });
}
double MaskGeometry::parameter(std::string_view name) const {
    if (name == "feather")
        return feather;
    if (name == "angle")
        return angle;
    if (name == "rangeLow")
        return rangeLow * 100;
    if (name == "rangeHigh")
        return rangeHigh * 100;
    if (name == "colorTolerance")
        return colorTolerance * 100;
    throw std::runtime_error("Unknown mask geometry parameter.");
}
void MaskGeometry::setParameter(std::string_view name, double value) {
    auto next = *this;
    if (name == "feather")
        next.feather = value;
    else if (name == "angle")
        next.angle = value;
    else if (name == "rangeLow")
        next.rangeLow = value / 100;
    else if (name == "rangeHigh")
        next.rangeHigh = value / 100;
    else if (name == "colorTolerance")
        next.colorTolerance = value / 100;
    else
        throw std::runtime_error("Unknown mask geometry parameter.");
    next.validate();
    *this = std::move(next);
}
double LocalMask::parameter(std::string_view name) const {
    if (name == "opacity")
        return opacity;
    if (std::find(localParameters.begin(), localParameters.end(), name) != localParameters.end())
        return adjustments[parameterIndex(name)];
    return MaskGeometry::parameter(name);
}
void LocalMask::setParameter(std::string_view name, double value) {
    auto next = *this;
    if (name == "opacity")
        next.opacity = value;
    else if (std::find(localParameters.begin(), localParameters.end(), name) !=
             localParameters.end())
        next.adjustments[parameterIndex(name)] = value;
    else
        next.MaskGeometry::setParameter(name, value);
    next.validate();
    *this = std::move(next);
}
void validateLocalMasks(const std::vector<LocalMask> &masks) {
    require(masks.size() <= 128, "A photo supports at most 128 local masks.");
    std::set<std::string> ids;
    std::size_t primitives = 0, strokes = 0, points = 0, bitmapPixels = 0;
    for (const auto &mask : masks) {
        mask.validate();
        require(ids.insert(mask.id).second, "Duplicate local mask ID.");
        const auto count = [&](const MaskGeometry &geometry) {
            ++primitives;
            bitmapPixels += geometry.bitmap.pixels().size();
            strokes += geometry.strokes.size();
            for (const auto &stroke : geometry.strokes)
                points += stroke.points.size();
        };
        count(mask);
        for (const auto &component : mask.components) {
            require(ids.insert(component.id).second, "Duplicate local component ID.");
            count(component.geometry);
        }
    }
    require(primitives <= 512 && strokes <= 512 && points <= 16384 &&
                bitmapPixels <= 4 * 1024 * 1024,
            "Photo mask resource limit exceeded.");
}
namespace {
void geometryOut(std::ostream &out, const MaskGeometry &g, int version) {
    out << "G " << static_cast<unsigned>(g.shape);
    for (double v :
         {g.x1, g.y1, g.x2, g.y2, g.angle, g.feather, g.rangeLow, g.rangeHigh, g.colorTolerance,
          g.referenceColor[0], g.referenceColor[1], g.referenceColor[2]})
        out << ' ' << v;
    out << ' ' << g.strokes.size() << '\n';
    for (const auto &stroke : g.strokes) {
        out << "S " << stroke.erase << ' ' << stroke.radius << ' ' << stroke.opacity << ' '
            << stroke.points.size();
        for (const auto &point : stroke.points)
            out << ' ' << point.x << ' ' << point.y;
        out << '\n';
    }
    if (g.shape == MaskShape::bitmap) {
        require(version >= 3, "Bitmap selections require mask document version 3.");
        constexpr char digits[] = "0123456789abcdef";
        out << "B " << g.bitmap.width() << ' ' << g.bitmap.height() << ' ';
        for (const auto value : g.bitmap.pixels())
            out << digits[value >> 4] << digits[value & 15];
        out << '\n';
    }
}
std::string encode(const std::vector<LocalMask> &masks, int version) {
    validateLocalMasks(masks);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "LUMA_LOCAL_MASKS " << version << ' ' << masks.size() << '\n';
    for (const auto &mask : masks) {
        out << std::quoted(mask.id) << ' ' << std::quoted(mask.name);
        if (version == 1) {
            require(mask.shape == MaskShape::linear || mask.shape == MaskShape::radial,
                    "Unsupported legacy mask shape.");
            out << ' ' << static_cast<unsigned>(mask.shape) << ' ' << mask.enabled << ' '
                << mask.inverted;
            for (double v :
                 {mask.x1, mask.y1, mask.x2, mask.y2, mask.angle, mask.feather, mask.opacity})
                out << ' ' << v;
            for (double v : mask.adjustments)
                out << ' ' << v;
            out << '\n';
        } else {
            out << ' ' << mask.enabled << ' ' << mask.inverted << ' ' << mask.opacity;
            for (double v : mask.adjustments)
                out << ' ' << v;
            out << ' ' << mask.components.size() << '\n';
            geometryOut(out, mask, version);
            for (const auto &component : mask.components) {
                out << "C " << std::quoted(component.id) << ' '
                    << static_cast<unsigned>(component.operation) << ' ' << component.enabled << ' '
                    << component.inverted << '\n';
                geometryOut(out, component.geometry, version);
            }
        }
    }
    auto text = out.str();
    require(text.size() <= 16 * 1048576, "Local mask document exceeds 16 MiB.");
    return text;
}
bool flag(std::istream &in) {
    int v = -1;
    in >> v;
    require(in && (v == 0 || v == 1), "Invalid mask flag.");
    return v == 1;
}
std::size_t count(std::istream &in, std::size_t maximum) {
    std::int64_t v = -1;
    in >> v;
    require(in && v >= 0 && static_cast<std::uint64_t>(v) <= maximum,
            "Invalid mask element count.");
    return static_cast<std::size_t>(v);
}
MaskGeometry geometryIn(std::istream &in, std::size_t &totalPoints, std::size_t &totalStrokes,
                        std::size_t &totalBitmapPixels, int version) {
    MaskGeometry g;
    std::string marker;
    int shape = -1;
    in >> marker >> shape;
    require(in && marker == "G" && shape >= 0 && shape <= (version >= 3 ? 5 : 4),
            "Invalid mask primitive.");
    g.shape = static_cast<MaskShape>(shape);
    in >> g.x1 >> g.y1 >> g.x2 >> g.y2 >> g.angle >> g.feather >> g.rangeLow >> g.rangeHigh >>
        g.colorTolerance;
    for (auto &v : g.referenceColor)
        in >> v;
    const auto strokes = count(in, 256);
    totalStrokes += strokes;
    require(totalStrokes <= 512, "Photo brush stroke limit exceeded.");
    for (std::size_t i = 0; i < strokes; ++i) {
        BrushStroke stroke;
        in >> marker;
        require(marker == "S", "Missing brush stroke.");
        stroke.erase = flag(in);
        in >> stroke.radius >> stroke.opacity;
        const auto points = count(in, 16384);
        require(points > 0, "A brush stroke needs a point.");
        totalPoints += points;
        require(totalPoints <= 16384, "Brush point limit exceeded.");
        for (std::size_t p = 0; p < points; ++p) {
            BrushPoint point;
            in >> point.x >> point.y;
            require(static_cast<bool>(in), "Incomplete brush point.");
            stroke.points.push_back(point);
        }
        g.strokes.push_back(std::move(stroke));
    }
    if (g.shape == MaskShape::bitmap) {
        int width = 0, height = 0;
        in >> marker >> width >> height;
        require(in && marker == "B" && width > 0 && height > 0 && width <= 1024 && height <= 1024,
                "Invalid bitmap mask dimensions.");
        const auto pixels = static_cast<std::size_t>(width) * height;
        totalBitmapPixels += pixels;
        require(totalBitmapPixels <= 4 * 1024 * 1024, "Photo bitmap mask limit exceeded.");
        std::string hex;
        in >> hex;
        require(in && hex.size() == pixels * 2, "Incomplete bitmap selection.");
        const auto digit = [](char c) -> unsigned {
            if (c >= '0' && c <= '9')
                return static_cast<unsigned>(c - '0');
            if (c >= 'a' && c <= 'f')
                return static_cast<unsigned>(c - 'a' + 10);
            throw std::runtime_error("Invalid bitmap mask encoding.");
        };
        std::vector<std::uint8_t> values(pixels);
        for (std::size_t i = 0; i < pixels; ++i)
            values[i] = static_cast<std::uint8_t>(digit(hex[i * 2]) * 16 + digit(hex[i * 2 + 1]));
        g.bitmap = BitmapMask(width, height, std::move(values));
    }
    return g;
}
} // namespace
std::string encodeLocalMasks(const std::vector<LocalMask> &masks) {
    return encode(masks, 3);
}
std::vector<LocalMask> decodeLocalMasks(std::string_view text) {
    require(!text.empty() && text.size() <= 16 * 1048576, "Invalid local mask document size.");
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    std::string marker;
    int version = 0;
    in >> marker >> version;
    require(in && marker == "LUMA_LOCAL_MASKS" && version >= 1 && version <= 3,
            "Unsupported local mask document.");
    const auto size = count(in, 128);
    std::vector<LocalMask> masks;
    std::size_t totalPoints = 0, totalComponents = size, totalStrokes = 0;
    std::size_t totalBitmapPixels = 0;
    for (std::size_t i = 0; i < size; ++i) {
        LocalMask mask;
        in >> std::quoted(mask.id) >> std::quoted(mask.name);
        if (version == 1) {
            int shape = -1;
            in >> shape;
            require(shape == 0 || shape == 1, "Invalid legacy mask shape.");
            mask.shape = static_cast<MaskShape>(shape);
            mask.enabled = flag(in);
            mask.inverted = flag(in);
            in >> mask.x1 >> mask.y1 >> mask.x2 >> mask.y2 >> mask.angle >> mask.feather >>
                mask.opacity;
            for (auto &v : mask.adjustments)
                in >> v;
        } else {
            mask.enabled = flag(in);
            mask.inverted = flag(in);
            in >> mask.opacity;
            for (auto &v : mask.adjustments)
                in >> v;
            const auto components = count(in, 64);
            totalComponents += components;
            require(totalComponents <= 512, "Mask component limit exceeded.");
            static_cast<MaskGeometry &>(mask) =
                geometryIn(in, totalPoints, totalStrokes, totalBitmapPixels, version);
            for (std::size_t c = 0; c < components; ++c) {
                MaskComponent component;
                int operation = -1;
                in >> marker >> std::quoted(component.id) >> operation;
                require(in && marker == "C" && operation >= 0 && operation <= 2,
                        "Invalid mask component.");
                component.operation = static_cast<MaskOperation>(operation);
                component.enabled = flag(in);
                component.inverted = flag(in);
                component.geometry =
                    geometryIn(in, totalPoints, totalStrokes, totalBitmapPixels, version);
                mask.components.push_back(std::move(component));
            }
        }
        require(static_cast<bool>(in), "Incomplete local mask document.");
        masks.push_back(std::move(mask));
    }
    require(encode(masks, version) == text, "Noncanonical or trailing local mask data.");
    return masks;
}
} // namespace luma
