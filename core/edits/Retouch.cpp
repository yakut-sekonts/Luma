#include "core/edits/Retouch.h"
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace luma {
void validateRetouch(const std::vector<RetouchStroke> &strokes) {
    if (strokes.size() > 256)
        throw std::runtime_error("A photo supports at most 256 retouch strokes.");
    std::set<std::string> ids;
    std::size_t count = 0;
    const auto range = [](double v, double lo, double hi) {
        return std::isfinite(v) && v >= lo && v <= hi;
    };
    for (const auto &stroke : strokes) {
        if (stroke.id.empty() || stroke.id.size() > 64 ||
            stroke.id.find_first_not_of("0123456789abcdefABCDEF-") != std::string::npos ||
            !ids.insert(stroke.id).second || stroke.mode < RetouchMode::clone ||
            stroke.mode > RetouchMode::remove || stroke.points.empty() ||
            stroke.points.size() > 4096 || !range(stroke.radius, .001, .25) ||
            !range(stroke.feather, 0, 1) || !range(stroke.opacity, 0, 1) ||
            !range(stroke.sourceOffsetX, -1, 1) || !range(stroke.sourceOffsetY, -1, 1))
            throw std::runtime_error("Invalid retouch stroke.");
        count += stroke.points.size();
        if (count > 32768)
            throw std::runtime_error("Retouch point budget exceeded.");
        for (const auto &point : stroke.points)
            if (!range(point[0], 0, 1) || !range(point[1], 0, 1))
                throw std::runtime_error("Retouch point is outside the photo.");
    }
}
std::string encodeRetouch(const std::vector<RetouchStroke> &strokes) {
    validateRetouch(strokes);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "LUMA_RETOUCH 1 " << strokes.size() << '\n';
    for (const auto &s : strokes) {
        out << s.id << ' ' << static_cast<int>(s.mode) << ' ' << s.enabled << ' ' << s.radius << ' '
            << s.feather << ' ' << s.opacity << ' ' << s.sourceOffsetX << ' ' << s.sourceOffsetY
            << ' ' << s.points.size() << '\n';
        for (const auto &p : s.points)
            out << p[0] << ' ' << p[1] << '\n';
    }
    return out.str();
}
std::vector<RetouchStroke> decodeRetouch(std::string_view text) {
    if (text.empty() || text.size() > 2 * 1048576)
        throw std::runtime_error("Invalid retouch document size.");
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    std::string magic;
    int version = 0;
    std::size_t count = 0, total = 0;
    if (!(in >> magic >> version >> count) || magic != "LUMA_RETOUCH" || version != 1 ||
        count > 256)
        throw std::runtime_error("Unsupported retouch document.");
    std::vector<RetouchStroke> result;
    for (std::size_t i = 0; i < count; ++i) {
        RetouchStroke stroke;
        int mode = 0, enabled = 0;
        std::size_t points = 0;
        if (!(in >> stroke.id >> mode >> enabled >> stroke.radius >> stroke.feather >>
              stroke.opacity >> stroke.sourceOffsetX >> stroke.sourceOffsetY >> points) ||
            mode < 0 || mode > 2 || enabled < 0 || enabled > 1 || points < 1 || points > 4096 ||
            total + points > 32768)
            throw std::runtime_error("Invalid retouch document.");
        total += points;
        stroke.mode = static_cast<RetouchMode>(mode);
        stroke.enabled = enabled != 0;
        stroke.points.resize(points);
        for (auto &point : stroke.points)
            if (!(in >> point[0] >> point[1]))
                throw std::runtime_error("Truncated retouch document.");
        result.push_back(std::move(stroke));
    }
    validateRetouch(result);
    if (encodeRetouch(result) != text)
        throw std::runtime_error("Noncanonical retouch document.");
    return result;
}
} // namespace luma
