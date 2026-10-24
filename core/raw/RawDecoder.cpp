#include "core/raw/RawDecoder.h"
#include "core/color/RGBProfiles.h"
#include "core/image/Bayer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <libraw/libraw.h>
#include <memory>
#include <sstream>

namespace luma {
namespace {
void requireSuccess(int code, const char *operation) {
    if (code != LIBRAW_SUCCESS) {
        throw std::runtime_error(std::string(operation) + ": " + libraw_strerror(code));
    }
}
template <std::size_t N> std::string text(const char (&value)[N]) {
    return {value, strnlen(value, N)};
}
std::optional<double> positive(double value) {
    return std::isfinite(value) && value > 0 ? std::optional<double>(value) : std::nullopt;
}
int cancelRaw(void *context, enum LibRaw_progress, int, int) {
    return static_cast<luma::CancellationToken *>(context)->isCancelled() ? 1 : 0;
}
bool lumaLinearDNG(const libraw_data_t &data) {
    const auto model = text(data.idata.model);
    return data.idata.dng_version != 0 && data.idata.filters == 0 && data.idata.colors == 3 &&
           text(data.idata.make) == "Luma" &&
           (model.starts_with("Rendered Linear RGB") ||
            model.starts_with("Luma Rendered Linear RGB"));
}
RawColorCalibration linearCalibration(const libraw_data_t &data) {
    const auto &profile = data.color.dng_color[0];
    const bool floating = text(data.idata.model).find("Linear RGB Float") != std::string::npos;
    if (!lumaLinearDNG(data) || profile.illuminant != 23 || data.color.black != 0 ||
        data.color.maximum != (floating ? 1U : 65535U))
        throw std::runtime_error("Unsupported rendered Linear RGB DNG calibration.");
    // LibRaw 0.21 copies TIFF matrix values without copying their parsedfields.
    // Require the actual matrices to be finite, invertible and consistent instead.
    bool calibrationPresent = false;
    for (const auto &row : profile.calibration)
        for (float value : row) {
            if (!std::isfinite(value))
                throw std::runtime_error("Invalid rendered DNG calibration.");
            calibrationPresent = calibrationPresent || value != 0;
        }
    ColorMatrix3 forward{}, color{};
    for (std::size_t row = 0; row < 3; ++row) {
        if (!std::isfinite(data.color.dng_levels.asshotneutral[row]) ||
            std::abs(data.color.dng_levels.asshotneutral[row] - 1) > 1e-6 ||
            data.color.cblack[row] != 0)
            throw std::runtime_error(
                "Rendered Linear RGB DNG requires neutral WB and zero black level.");
        const double analog = data.color.dng_levels.analogbalance[row];
        if (!std::isfinite(analog) || (analog != 0 && std::abs(analog - 1) > 1e-6))
            throw std::runtime_error("Unsupported rendered DNG analog balance.");
        for (std::size_t column = 0; column < 3; ++column) {
            forward[row * 3 + column] = profile.forwardmatrix[row][column];
            color[row * 3 + column] = profile.colormatrix[row][column];
            if (calibrationPresent &&
                (!std::isfinite(profile.calibration[row][column]) ||
                 std::abs(profile.calibration[row][column] - (row == column ? 1 : 0)) > 1e-6))
                throw std::runtime_error("Unsupported rendered DNG camera calibration.");
        }
    }
    (void)inverseColorMatrix(forward);
    if (std::abs(forward[0] + forward[1] + forward[2] - .9642) > .005 ||
        std::abs(forward[3] + forward[4] + forward[5] - 1) > .005 ||
        std::abs(forward[6] + forward[7] + forward[8] - .8249) > .005)
        throw std::runtime_error("Rendered DNG forward matrix does not preserve D50 neutral.");
    const auto identity = multiplyColorMatrices(color, forward);
    for (std::size_t i = 0; i < 9; ++i)
        if (!std::isfinite(identity[i]) || std::abs(identity[i] - (i % 4 == 0 ? 1 : 0)) > .005)
            throw std::runtime_error("Inconsistent rendered DNG color matrices.");
    for (std::size_t i = 6; i < LIBRAW_CBLACK_SIZE; ++i)
        if (data.color.cblack[i] != 0)
            throw std::runtime_error("Rendered DNG requires a zero black-level grid.");
    static const auto toSRGB = inverseColorMatrix(rgbToXYZD50(linearSRGBProfile()));
    const auto matrix = multiplyColorMatrices(toSRGB, forward);
    RawColorCalibration result;
    for (std::size_t i = 0; i < matrix.size(); ++i)
        result.cameraToSRGB[i] = static_cast<float>(matrix[i]);
    return result;
}
PhotoMetadata metadataFrom(const libraw_data_t &data) {
    PhotoMetadata metadata;
    metadata.camera = text(data.idata.make) + " " + text(data.idata.model);
    metadata.lens = text(data.lens.Lens);
    if (metadata.lens.empty()) {
        metadata.lens = text(data.lens.makernotes.Lens);
    }
    const auto minFocal = positive(data.lens.MinFocal).value_or(data.lens.makernotes.MinFocal);
    const auto maxFocal = positive(data.lens.MaxFocal).value_or(data.lens.makernotes.MaxFocal);
    const auto minAperture =
        positive(data.lens.MaxAp4MinFocal).value_or(data.lens.makernotes.MaxAp4MinFocal);
    const auto maxAperture =
        positive(data.lens.MaxAp4MaxFocal).value_or(data.lens.makernotes.MaxAp4MaxFocal);
    if (metadata.lens.empty() && positive(minFocal) && positive(maxFocal)) {
        std::ostringstream description;
        description << minFocal;
        if (maxFocal != minFocal) {
            description << "–" << maxFocal;
        }
        description << " mm";
        if (positive(minAperture)) {
            description << " f/" << minAperture;
            if (positive(maxAperture) && maxAperture != minAperture) {
                description << "–" << maxAperture;
            }
        }
        metadata.lens = description.str();
    }
    if (metadata.lens.empty() && data.lens.nikon.LensIDNumber != 0) {
        metadata.lens =
            "Nikon lens ID " + std::to_string(static_cast<unsigned>(data.lens.nikon.LensIDNumber));
    }
    metadata.iso = positive(data.other.iso_speed);
    metadata.shutterSeconds = positive(data.other.shutter);
    metadata.aperture = positive(data.other.aperture);
    metadata.focalLength = positive(data.other.focal_len);
    if (data.other.timestamp > 0) {
        metadata.captureTimestamp = data.other.timestamp;
    }
    metadata.width = data.sizes.width;
    metadata.height = data.sizes.height;
    metadata.orientation = data.sizes.flip;

    return metadata;
}
} // namespace

bool RawDecoder::recognizes(const std::filesystem::path &path, CancellationToken stop) const {
    checkCancellation(stop);
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 1'300'000'000)
        throw std::runtime_error("Image is missing or exceeds the input budget.");
    const auto decoder = std::make_unique<LibRaw>();
    decoder->set_progress_handler(cancelRaw, &stop);
    const auto result = decoder->open_file(path.c_str());
    checkCancellation(stop);
    // Recognition is independent of our supported sensor layout. Unsupported RAW
    // must fail in inspect/decode rather than fall back to an embedded preview.
    if (result == LIBRAW_FILE_UNSUPPORTED)
        return false;
    requireSuccess(result, "Read image container");
    return true;
}
PhotoMetadata RawDecoder::inspect(const std::filesystem::path &path, CancellationToken stop) const {
    checkCancellation(stop);
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 512'000'000)
        throw std::runtime_error("RAW is missing or exceeds the input budget.");
    const auto decoder = std::make_unique<LibRaw>();
    decoder->set_progress_handler(cancelRaw, &stop);
    requireSuccess(decoder->open_file(path.c_str()), "Read RAW metadata");
    const auto &data = decoder->imgdata;
    if (!lumaLinearDNG(data) && std::filesystem::file_size(path) > 512'000'000)
        throw std::runtime_error("RAW exceeds the 512 MB input budget.");
    if ((!lumaLinearDNG(data) && data.idata.filters == 0) || data.idata.filters == 9 ||
        data.idata.colors != 3 || data.sizes.width < 2 || data.sizes.height < 2)
        throw std::runtime_error("Library currently supports Bayer RAW only.");
    pixelCount(data.sizes.width, data.sizes.height);
    if (lumaLinearDNG(data))
        (void)linearCalibration(data);
    checkCancellation(stop);
    return metadataFrom(data);
}
std::vector<std::uint8_t> RawDecoder::embeddedJPEG(const std::filesystem::path &path,
                                                   CancellationToken stop) const {
    checkCancellation(stop);
    const auto decoder = std::make_unique<LibRaw>();
    decoder->set_progress_handler(cancelRaw, &stop);
    requireSuccess(decoder->open_file(path.c_str()), "Read thumbnail");
    const int code = decoder->unpack_thumb();
    checkCancellation(stop);
    if (code != LIBRAW_SUCCESS || decoder->imgdata.thumbnail.tformat != LIBRAW_THUMBNAIL_JPEG)
        return {};
    const auto &thumbnail = decoder->imgdata.thumbnail;
    if (!thumbnail.thumb || thumbnail.tlength > 32'000'000)
        return {};
    const auto *begin = reinterpret_cast<const std::uint8_t *>(thumbnail.thumb);
    return {begin, begin + thumbnail.tlength};
}
DecodedRaw RawDecoder::decode(const std::filesystem::path &path, luma::CancellationToken stop,
                              const std::function<void(double)> &progress) const {
    checkCancellation(stop);
    if (!std::filesystem::is_regular_file(path) ||
        std::filesystem::file_size(path) > 1'300'000'000) {
        throw std::runtime_error("RAW is missing or exceeds the input budget.");
    }
    // LibRaw contains large metadata tables. macOS worker stacks are only 512 KB.
    const auto allocation = std::make_unique<LibRaw>();
    auto &decoder = *allocation;
    decoder.set_progress_handler(cancelRaw, &stop);
    decoder.imgdata.rawparams.max_raw_memory_mb = 512;
    requireSuccess(decoder.open_file(path.c_str()), "Open RAW");
    if (lumaLinearDNG(decoder.imgdata)) {
        decoder.imgdata.rawparams.options &= ~LIBRAW_RAWOPTIONS_CONVERTFLOAT_TO_INT;
        decoder.imgdata.rawparams.max_raw_memory_mb = 1536;
    } else if (std::filesystem::file_size(path) > 512'000'000) {
        throw std::runtime_error("RAW exceeds the 512 MB input budget.");
    }
    if (progress) {
        progress(0.1);
    }
    requireSuccess(decoder.unpack(), "Decode sensor data");
    checkCancellation(stop);
    const auto &data = decoder.imgdata;
    const auto &sizes = data.sizes;
    if (lumaLinearDNG(data)) {
        const auto calibration = linearCalibration(data);
        const bool floating = data.rawdata.float3_image || data.rawdata.float4_image;
        const int channels = floating                    ? (data.rawdata.float3_image ? 3 : 4)
                             : data.rawdata.color3_image ? 3
                             : data.rawdata.color4_image ? 4
                                                         : 0;
        const unsigned bytes = floating ? sizeof(float) : sizeof(unsigned short);
        if (floating != (text(data.idata.model).find("Linear RGB Float") != std::string::npos))
            throw std::runtime_error("Rendered DNG sample type does not match its profile.");
        if (channels == 0 || sizes.width < 22 || sizes.height < 22 ||
            sizes.top_margin + sizes.height > sizes.raw_height ||
            sizes.left_margin + sizes.width > sizes.raw_width ||
            sizes.raw_pitch < static_cast<unsigned>(sizes.raw_width) * channels * bytes ||
            sizes.raw_pitch % (static_cast<unsigned>(channels) * bytes) != 0)
            throw std::runtime_error("Invalid rendered Linear RGB DNG sample bounds.");
        const auto stride = sizes.raw_pitch / (static_cast<unsigned>(channels) * bytes);
        FloatImage image(sizes.width, sizes.height);
        for (int y = 0; y < image.height; ++y) {
            checkCancellation(stop);
            for (int x = 0; x < image.width; ++x) {
                const auto index =
                    static_cast<std::size_t>(y + sizes.top_margin) * stride + x + sizes.left_margin;
                const auto target = (static_cast<std::size_t>(y) * image.width + x) * 3;
                for (std::size_t c = 0; c < 3; ++c) {
                    const float value =
                        floating ? (channels == 3 ? data.rawdata.float3_image[index][c]
                                                  : data.rawdata.float4_image[index][c])
                                 : (channels == 3 ? data.rawdata.color3_image[index][c]
                                                  : data.rawdata.color4_image[index][c]) /
                                       65535.F;
                    if (!std::isfinite(value))
                        throw std::runtime_error("Non-finite rendered DNG sample.");
                    image.rgb[target + c] = value;
                }
            }
            if (progress)
                progress(0.45 + .55 * (y + 1.0) / image.height);
        }
        auto metadata = metadataFrom(data);
        decoder.recycle();
        return {std::move(image), std::move(metadata), calibration};
    }
    if (!data.rawdata.raw_image || data.idata.filters == 0 || data.idata.filters == 9 ||
        data.idata.colors != 3 || sizes.width < 2 || sizes.height < 2) {
        throw std::runtime_error(
            "M0 supports integer Bayer RAW only; X-Trans, monochrome and RGB RAW are planned.");
    }
    pixelCount(sizes.width, sizes.height);
    if (sizes.top_margin + sizes.height > sizes.raw_height ||
        sizes.left_margin + sizes.width > sizes.raw_width ||
        sizes.raw_pitch < static_cast<unsigned>(sizes.raw_width) * sizeof(unsigned short)) {
        throw std::runtime_error("Invalid RAW sensor bounds.");
    }
    auto metadata = metadataFrom(data);

    std::array<int, 4> cfa{};
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const int channel = decoder.COLOR(y, x);
            cfa[y * 2 + x] = channel == 3 ? 1 : channel;
        }
    }
    BayerFrame frame(sizes.width, sizes.height, cfa);
    const auto &color = data.color;
    const auto green = color.cam_mul[1] > 0 ? color.cam_mul[1] : color.pre_mul[1];
    if (!std::isfinite(green) || green <= 0 || color.maximum == 0) {
        throw std::runtime_error("RAW has no usable white balance or white level.");
    }
    std::array<float, 4> gains{};
    for (int c = 0; c < 4; ++c) {
        const int channel = c == 3 ? 1 : c;
        const auto wb =
            color.cam_mul[channel] > 0 ? color.cam_mul[channel] : color.pre_mul[channel];
        gains[c] = wb / green;
        if (!std::isfinite(gains[c]) || gains[c] <= 0) {
            throw std::runtime_error("Invalid RAW white balance.");
        }
    }
    const auto stride = sizes.raw_pitch / sizeof(unsigned short);
    for (int y = 0; y < sizes.height; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < sizes.width; ++x) {
            const int channel = decoder.COLOR(y, x);
            double black = static_cast<double>(color.black) + color.cblack[channel];
            if (color.cblack[4] > 0 && color.cblack[5] > 0) {
                const auto index =
                    6 + (y % color.cblack[4]) * color.cblack[5] + (x % color.cblack[5]);
                if (index >= LIBRAW_CBLACK_SIZE) {
                    throw std::runtime_error("Invalid RAW black-level grid.");
                }
                black += color.cblack[index];
            }
            const double white = color.maximum;
            if (black >= white) {
                throw std::runtime_error("Invalid RAW black/white levels.");
            }
            const double sample =
                data.rawdata.raw_image[(y + sizes.top_margin) * stride + x + sizes.left_margin];
            frame.samples[static_cast<std::size_t>(y) * sizes.width + x] =
                static_cast<float>(std::max(0.0, sample - black) / (white - black));
        }
    }
    std::array<float, 9> matrix{};
    for (int row = 0; row < 3; ++row) {
        matrix[row * 3] = color.rgb_cam[row][0];
        matrix[row * 3 + 1] = color.rgb_cam[row][1] + color.rgb_cam[row][3];
        matrix[row * 3 + 2] = color.rgb_cam[row][2];
    }
    decoder.recycle();
    if (progress) {
        progress(0.45);
    }
    auto image = demosaicBilinear(frame, stop);
    frame.samples.clear();
    frame.samples.shrink_to_fit();

    if (progress) {
        progress(1);
    }
    return {std::move(image), std::move(metadata), {matrix, {gains[0], gains[1], gains[2]}}};
}
} // namespace luma
