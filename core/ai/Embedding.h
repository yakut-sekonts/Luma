#pragma once
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace luma::ai {
inline std::vector<float> normalizedEmbedding(std::span<const float> input) {
    if (input.empty() || input.size() > 2048)
        throw std::runtime_error("Unsupported semantic embedding dimensions.");
    double norm = 0;
    for (const float v : input) {
        if (!std::isfinite(v))
            throw std::runtime_error("Invalid semantic embedding.");
        norm += static_cast<double>(v) * v;
    }
    if (norm < 1e-16)
        throw std::runtime_error("Empty semantic embedding.");
    norm = std::sqrt(norm);
    std::vector<float> result(input.begin(), input.end());
    for (auto &v : result)
        v = static_cast<float>(v / norm);
    return result;
}
struct SemanticMatch {
    std::string id;
    double similarity = 0;
};
} // namespace luma::ai
