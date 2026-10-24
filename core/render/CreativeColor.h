#pragma once
#include "core/edits/EditDocument.h"
#include "core/render/AdvancedColor.h"
namespace luma {
bool creativeColorActive(const EditDocument &edits);
double pointColorWeight(const std::array<float, 3> &rgb, const CreativeSettings &settings);
std::array<float, 3> applyCreativeColor(std::array<float, 3> rgb, const EditDocument &edits);
std::array<float, 3> applyOutputEffects(std::array<float, 3> rgb, const EditDocument &edits, int x,
                                        int y, int width, int height, double scale = 1);
} // namespace luma
