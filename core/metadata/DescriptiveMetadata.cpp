#include "core/metadata/DescriptiveMetadata.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace luma {
namespace {
void text(const std::string &value, std::size_t limit, bool multiline = false) {
    if (value.size() > limit)
        throw std::runtime_error("Metadata text exceeds its byte limit.");
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<unsigned char>(value[i++]);
        if (first < 0x80) {
            if (first < 32 && !(multiline && (first == '\n' || first == '\r' || first == '\t')))
                throw std::runtime_error("Metadata contains a control character.");
            continue;
        }
        int length = first >= 0xC2 && first <= 0xDF   ? 1
                     : first >= 0xE0 && first <= 0xEF ? 2
                     : first >= 0xF0 && first <= 0xF4 ? 3
                                                      : -1;
        if (length < 0 || i + static_cast<std::size_t>(length) > value.size())
            throw std::runtime_error("Metadata must contain valid UTF-8.");
        std::uint32_t code = first & (length == 1 ? 0x1F : length == 2 ? 0x0F : 0x07);
        for (int j = 0; j < length; ++j) {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xC0) != 0x80)
                throw std::runtime_error("Metadata must contain valid UTF-8.");
            code = (code << 6) | (next & 0x3F);
        }
        if (code < (length == 1   ? 0x80U
                    : length == 2 ? 0x800U
                                  : 0x10000U) ||
            (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF)
            throw std::runtime_error("Metadata must contain valid UTF-8.");
    }
}
} // namespace
void DescriptiveMetadata::validate() const {
    text(title, 1024);
    text(caption, 16384, true);
    text(copyright, 2048);
    text(location, 2048);
    text(label, 1024);
    if (rating < 0 || rating > 5)
        throw std::runtime_error("Rating must be 0–5.");
    if (flag < -1 || flag > 1)
        throw std::runtime_error("Flag must be Reject, Unflagged or Pick.");
    if (creators.size() > 256)
        throw std::runtime_error("At most 256 creators are supported per photo.");
    // dc:creator is an ordered sequence. Repeated names retain their positions.
    for (const auto &creator : creators) {
        text(creator, 1024);
        if (creator.empty())
            throw std::runtime_error("Creator names must not be empty.");
    }
    if (keywords.size() > 256)
        throw std::runtime_error("At most 256 keywords are supported per photo.");
    std::set<std::string> unique;
    for (const auto &keyword : keywords) {
        text(keyword, 512);
        if (keyword.find_first_not_of(' ') == std::string::npos || !unique.insert(keyword).second)
            throw std::runtime_error("Keywords must be nonempty and unique.");
    }
}
std::string DescriptiveMetadata::colorLabel() const {
    auto token = label;
    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char value) {
        return static_cast<char>(value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value);
    });
    if (token == "red" || token == "yellow" || token == "green" || token == "blue" ||
        token == "purple")
        return token;
    return {};
}
} // namespace luma
