#include "core/edits/CreativeSettings.h"
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace luma {
bool isCreativeParameter(std::string_view name) {
    return name.starts_with("presence.") || name.starts_with("point.") ||
           name.starts_with("grading.") || name.starts_with("vignette.") ||
           name.starts_with("grain.");
}
const CreativeParameter &creativeParameter(std::string_view name) {
    for (const auto &parameter : creativeParameters)
        if (parameter.name == name)
            return parameter;
    throw std::runtime_error("Unknown creative adjustment parameter.");
}
CreativeSettings::CreativeSettings() {
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = creativeParameters[i].initial;
}
double CreativeSettings::parameter(std::string_view name) const {
    return values[static_cast<std::size_t>(&creativeParameter(name) - creativeParameters.data())];
}
void CreativeSettings::setParameter(std::string_view name, double value) {
    const auto &p = creativeParameter(name);
    if (!std::isfinite(value) || value < p.minimum || value > p.maximum)
        throw std::runtime_error("Creative adjustment is outside its supported range.");
    values[static_cast<std::size_t>(&p - creativeParameters.data())] = value;
}
void CreativeSettings::validate() const {
    if (enabled & ~creativeCorrectionMask)
        throw std::runtime_error("Invalid creative adjustment flags.");
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto &p = creativeParameters[i];
        if (!std::isfinite(values[i]) || values[i] < p.minimum || values[i] > p.maximum)
            throw std::runtime_error("Creative adjustment is outside its supported range.");
    }
}
std::string encodeCreative(const CreativeSettings &settings) {
    settings.validate();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "LUMA_CREATIVE 1 " << settings.enabled
        << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto value : settings.values)
        out << ' ' << value;
    out << '\n';
    return out.str();
}
CreativeSettings decodeCreative(std::string_view text) {
    if (text.empty() || text.size() > 8192)
        throw std::runtime_error("Invalid creative adjustment document.");
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    std::string magic;
    int version = 0;
    CreativeSettings result;
    in >> magic >> version >> result.enabled;
    for (auto &value : result.values)
        in >> value;
    if (!in || magic != "LUMA_CREATIVE" || version != 1)
        throw std::runtime_error("Invalid creative adjustment document.");
    result.validate();
    if (encodeCreative(result) != text)
        throw std::runtime_error("Invalid creative adjustment document.");
    return result;
}
} // namespace luma
