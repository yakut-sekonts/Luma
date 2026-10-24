#pragma once
#include "core/color/RGBProfiles.h"
#include "core/export/LinearOutput.h"
#include <string_view>

namespace luma {
enum class DngSamples { integer16, floating32 };
using ExportRowConversion = std::function<void(std::span<const float>, std::span<float>)>;
// Writes to an owned temporary path; the platform writer installs it atomically.
// This product includes DNG technology under license by Adobe.
void writeLinearDNG(const LinearOutput &image, const ColorMatrix3 &rgbToXYZ,
                    const ExportRowConversion &convert, const std::filesystem::path &temporary,
                    std::string_view profileName, std::span<const std::uint8_t> xmp = {},
                    CancellationToken stop = {}, const std::function<void(double)> &progress = {},
                    DngSamples samples = DngSamples::integer16);
} // namespace luma
