#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace luma {
inline constexpr std::size_t maxQuickLookParameterDeltas = 64;
struct QuickLookParameterDelta {
    std::string parameter;
    double value = 0;
    bool operator==(const QuickLookParameterDelta &) const = default;
};
struct QuickLookLayer {
    std::string id;
    std::string recipe;
    double amount = 100;
    bool enabled = true;
    // Empty for a global look. Adaptive looks own a bitmap mask with this ID.
    std::string mask;
    // Match Look layers are self-contained so rendering and export do not depend
    // on a mutable user preset collection. Built-in layers leave both fields empty.
    std::string name;
    std::vector<QuickLookParameterDelta> deltas;
    // Custom generated layers retain the exact provider contract that created
    // their editable deltas. An empty model identifies a declared fallback,
    // never an implicit or unknown model.
    std::string provider;
    std::string providerVersion;
    std::string model;
    std::string modelDigest;
    bool deterministicFallback = false;
    QuickLookLayer() = default;
    QuickLookLayer(std::string layerID, std::string recipeID, double layerAmount, bool layerEnabled,
                   std::string maskID, std::string layerName,
                   std::vector<QuickLookParameterDelta> parameterDeltas,
                   std::string providerID = {}, std::string version = {}, std::string modelID = {},
                   std::string digest = {}, bool fallback = false)
        : id(std::move(layerID)), recipe(std::move(recipeID)), amount(layerAmount),
          enabled(layerEnabled), mask(std::move(maskID)), name(std::move(layerName)),
          deltas(std::move(parameterDeltas)), provider(std::move(providerID)),
          providerVersion(std::move(version)), model(std::move(modelID)),
          modelDigest(std::move(digest)), deterministicFallback(fallback) {}
    bool operator==(const QuickLookLayer &) const = default;
    void validate() const;
};
} // namespace luma
