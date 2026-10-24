#pragma once
#include "core/ai/Providers.h"
#include "core/math/Homography.h"
namespace luma::macos {
enum class AlignmentMode { bracket, panorama };
// Input is upright sRGB, equal dimensions, maximum 1024 pixels per edge.
// Returns floating -> reference in normalized top-left coordinates.
// Panorama validates overlap and considers large translations. Bracket frames
// can contain differently clipped regions, so do not use that photometric gate.
Homography alignImages(ai::RgbaImage reference, ai::RgbaImage floating, CancellationToken stop = {},
                       AlignmentMode mode = AlignmentMode::bracket);
} // namespace luma::macos
