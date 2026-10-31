#pragma once
#include <cstdint>
#include <string>

namespace luma {
enum class DecodeSource { unknown, raster, renderedLinearDNG, integerBayerRaw };
enum class DecodeReconstruction { none, edgeAware };
enum class DecodeColorPath {
    unknown,
    embeddedProfileToLinearSRGB,
    dngProfileToLinearSRGB,
    cameraToLinearSRGB
};
enum class DecodeHighlightPath { unknown, sourceRange, adaptiveHDR, normalizedSensorHeadroom };

struct DecodeDiagnostics {
    std::string decoder;
    std::string decoderVersion;
    DecodeSource source = DecodeSource::unknown;
    DecodeReconstruction reconstruction = DecodeReconstruction::none;
    DecodeColorPath color = DecodeColorPath::unknown;
    DecodeHighlightPath highlights = DecodeHighlightPath::unknown;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int activeWidth = 0;
    int activeHeight = 0;
    int leftMargin = 0;
    int topMargin = 0;
    std::string cfa;
    double blackMinimum = 0;
    double blackMaximum = 0;
    double whiteLevel = 0;
    std::uint64_t sensorSamples = 0;
    std::uint64_t samplesAtOrAboveWhite = 0;
    bool operator==(const DecodeDiagnostics &) const = default;
};
} // namespace luma
