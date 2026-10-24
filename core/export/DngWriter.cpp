#include "core/export/DngWriter.h"
#include "core/image/Bayer.h"
#include "core/render/AdvancedColor.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>

// This product includes DNG technology under license by Adobe.
// Original implementation of Adobe DNG 1.4 LinearRaw using a single D50 profile.
namespace luma {
namespace {
using Bytes = std::vector<std::uint8_t>;
void u16(Bytes &bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 255));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
void u32(Bytes &bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 255));
}
struct Field {
    std::uint16_t tag, type;
    std::uint32_t count;
    Bytes bytes;
};
Field shorts(std::uint16_t tag, std::initializer_list<std::uint16_t> values) {
    Field result{tag, 3, static_cast<std::uint32_t>(values.size()), {}};
    for (auto value : values)
        u16(result.bytes, value);
    return result;
}
Field longs(std::uint16_t tag, std::initializer_list<std::uint32_t> values) {
    Field result{tag, 4, static_cast<std::uint32_t>(values.size()), {}};
    for (auto value : values)
        u32(result.bytes, value);
    return result;
}
Field ascii(std::uint16_t tag, std::string_view value) {
    if (value.size() > 256 || value.find('\0') != std::string_view::npos)
        throw std::runtime_error("Invalid DNG profile name.");
    Field result{
        tag, 2, static_cast<std::uint32_t>(value.size() + 1), {value.begin(), value.end()}};
    result.bytes.push_back(0);
    return result;
}
Field rational(std::uint16_t tag, std::initializer_list<std::uint32_t> values) {
    Field result{tag, 5, static_cast<std::uint32_t>(values.size()), {}};
    for (auto value : values) {
        u32(result.bytes, value);
        u32(result.bytes, 1);
    }
    return result;
}
Field matrix(std::uint16_t tag, const ColorMatrix3 &values) {
    Field result{tag, 10, 9, {}};
    for (double value : values) {
        const double scaled = std::round(value * 1e7);
        if (!std::isfinite(scaled) || scaled < std::numeric_limits<std::int32_t>::min() ||
            scaled > std::numeric_limits<std::int32_t>::max())
            throw std::runtime_error("DNG color matrix is out of range.");
        u32(result.bytes, static_cast<std::uint32_t>(static_cast<std::int32_t>(scaled)));
        u32(result.bytes, 10'000'000);
    }
    return result;
}
void write(std::ofstream &file, const Bytes &bytes) {
    file.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file)
        throw std::runtime_error("Cannot write Linear RGB DNG.");
}
} // namespace
void writeLinearDNG(const LinearOutput &image, const ColorMatrix3 &rgbToXYZ,
                    const ExportRowConversion &convert, const std::filesystem::path &temporary,
                    std::string_view profileName, std::span<const std::uint8_t> xmp,
                    CancellationToken stop, const std::function<void(double)> &progress,
                    DngSamples samples) {
    image.validate();
    checkCancellation(stop);
    ExportOptions limits;
    limits.format = ExportFormat::dng;
    (void)limits.dimensions(image.width, image.height);
    if (!convert || profileName.empty() || xmp.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid DNG conversion/metadata.");
    const auto inverse = inverseColorMatrix(rgbToXYZ);
    // A standard DNG forward matrix must map the unit vector to XYZ D50.
    if (std::abs(rgbToXYZ[0] + rgbToXYZ[1] + rgbToXYZ[2] - .9642) > .005 ||
        std::abs(rgbToXYZ[3] + rgbToXYZ[4] + rgbToXYZ[5] - 1) > .005 ||
        std::abs(rgbToXYZ[6] + rgbToXYZ[7] + rgbToXYZ[8] - .8249) > .005)
        throw std::runtime_error("DNG forward matrix does not map neutral RGB to D50.");
    const auto width = static_cast<std::uint32_t>(image.width),
               height = static_cast<std::uint32_t>(image.height);
    if (samples != DngSamples::integer16 && samples != DngSamples::floating32)
        throw std::runtime_error("Unsupported DNG sample format.");
    const bool floating = samples == DngSamples::floating32;
    const std::uint16_t bits = floating ? 32 : 16;
    std::vector<Field> fields{
        longs(254, {0}),
        longs(256, {width}),
        longs(257, {height}),
        shorts(258, {bits, bits, bits}),
        shorts(259, {1}),
        shorts(262, {34892}),
        ascii(271, "Luma"),
        ascii(272, floating ? "Rendered Linear RGB Float" : "Rendered Linear RGB"),
        longs(273, {0}),
        shorts(274, {1}),
        shorts(277, {3}),
        longs(278, {height}),
        longs(279, {width * height * (floating ? 12U : 6U)}),
        shorts(284, {1}),
        ascii(305, "Luma"),
        shorts(339, {static_cast<std::uint16_t>(floating ? 3 : 1)}),
        {50706, 1, 4, {1, 4, 0, 0}},
        {50707, 1, 4, {1, 4, 0, 0}},
        ascii(50708,
              floating ? "Luma Rendered Linear RGB Float v1" : "Luma Rendered Linear RGB v1"),
        {50710, 1, 3, {0, 1, 2}},
        rational(50714, {0}),
        longs(50717, {floating ? 1U : 65535U}),
        rational(50718, {1, 1}),
        longs(50719, {0, 0}),
        longs(50720, {width, height}),
        matrix(50721, inverse),
        rational(50728, {1, 1, 1}),
        {50730, 10, 1, {0, 0, 0, 0, 1, 0, 0, 0}},
        shorts(50778, {23}),
        longs(50829, {0, 0, height, width}),
        ascii(50936, profileName),
        matrix(50964, rgbToXYZ),
        longs(51110, {1})};
    Field curve{50940, 11, 4, {}};
    for (float value : {0.F, 0.F, 1.F, 1.F})
        u32(curve.bytes, std::bit_cast<std::uint32_t>(value));
    fields.push_back(std::move(curve));
    if (!xmp.empty())
        fields.push_back(
            {700, 1, static_cast<std::uint32_t>(xmp.size()), {xmp.begin(), xmp.end()}});
    // A conventional RGB preview in IFD0 keeps Finder/metadata readers useful;
    // full-resolution LinearRaw is referenced by SubIFDs and has independent tags.
    const double previewScale = std::min(1.0, 256.0 / std::max(image.width, image.height));
    const auto previewWidth =
        static_cast<std::uint32_t>(std::max(1.0, std::round(image.width * previewScale)));
    const auto previewHeight =
        static_cast<std::uint32_t>(std::max(1.0, std::round(image.height * previewScale)));
    Bytes preview;
    preview.reserve(static_cast<std::size_t>(previewWidth) * previewHeight * 3);
    for (std::uint32_t y = 0; y < previewHeight; ++y) {
        checkCancellation(stop);
        for (std::uint32_t x = 0; x < previewWidth; ++x) {
            std::array<double, 3> sums{};
            const int firstX = static_cast<int>(x * width / previewWidth),
                      lastX = static_cast<int>((x + 1) * width / previewWidth);
            const int firstY = static_cast<int>(y * height / previewHeight),
                      lastY = static_cast<int>((y + 1) * height / previewHeight);
            for (int sy = firstY; sy < lastY; ++sy)
                for (int sx = firstX; sx < lastX; ++sx) {
                    const auto source = (static_cast<std::size_t>(sy) * image.width + sx) * 4;
                    for (std::size_t c = 0; c < 3; ++c)
                        sums[c] += image.rgba[source + c] * image.rgba[source + 3];
                }
            const double samples = static_cast<double>((lastX - firstX) * (lastY - firstY));
            for (double value : sums)
                preview.push_back(static_cast<std::uint8_t>(
                    std::lround(encodeSRGB(std::clamp(value / samples, 0.0, 1.0)) * 255)));
        }
    }
    std::vector<Field> root{longs(254, {1}),
                            longs(256, {previewWidth}),
                            longs(257, {previewHeight}),
                            shorts(258, {8, 8, 8}),
                            shorts(259, {1}),
                            shorts(262, {2}),
                            longs(273, {0}),
                            shorts(274, {1}),
                            shorts(277, {3}),
                            longs(278, {previewHeight}),
                            longs(279, {static_cast<std::uint32_t>(preview.size())}),
                            shorts(284, {1}),
                            longs(330, {0}),
                            ascii(50966, "Luma"),
                            longs(50970, {2})};
    std::vector<Field> raw;
    for (auto &field : fields) {
        const bool rawTag = field.tag == 50710 || field.tag == 50714 || field.tag == 50717 ||
                            field.tag == 50718 || field.tag == 50719 || field.tag == 50720 ||
                            field.tag == 50829;
        if (field.tag == 271 || field.tag == 272 || field.tag == 305 || field.tag == 700 ||
            (field.tag >= 50000 && !rawTag))
            root.push_back(std::move(field));
        else
            raw.push_back(std::move(field));
    }
    const auto order = [](const auto &left, const auto &right) { return left.tag < right.tag; };
    std::sort(root.begin(), root.end(), order);
    std::sort(raw.begin(), raw.end(), order);
    const auto rawDirectoryOffset = static_cast<std::uint32_t>(8 + 2 + root.size() * 12 + 4);
    const auto extraStart =
        static_cast<std::uint32_t>(rawDirectoryOffset + 2 + raw.size() * 12 + 4);
    std::uint32_t previewStart = extraStart;
    for (const auto *directory : {&root, &raw})
        for (const auto &field : *directory)
            if (field.bytes.size() > 4)
                previewStart += static_cast<std::uint32_t>((field.bytes.size() + 1) / 2 * 2);
    const auto pixelStart = previewStart + static_cast<std::uint32_t>((preview.size() + 1) / 2 * 2);
    Bytes extra;
    const auto directoryBytes = [&](std::vector<Field> &directory, std::uint32_t stripOffset) {
        Bytes result;
        u16(result, static_cast<std::uint16_t>(directory.size()));
        for (auto &field : directory) {
            if (field.tag == 273 || field.tag == 330) {
                field.bytes.clear();
                u32(field.bytes, field.tag == 273 ? stripOffset : rawDirectoryOffset);
            }
            u16(result, field.tag);
            u16(result, field.type);
            u32(result, field.count);
            if (field.bytes.size() <= 4) {
                result.insert(result.end(), field.bytes.begin(), field.bytes.end());
                result.insert(result.end(), 4 - field.bytes.size(), 0);
            } else {
                u32(result, extraStart + static_cast<std::uint32_t>(extra.size()));
                extra.insert(extra.end(), field.bytes.begin(), field.bytes.end());
                if (extra.size() % 2)
                    extra.push_back(0);
            }
        }
        u32(result, 0);
        return result;
    };
    const auto rootBytes = directoryBytes(root, previewStart),
               rawBytes = directoryBytes(raw, pixelStart);
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file)
        throw std::runtime_error("Cannot open DNG temporary output.");
    Bytes header{'I', 'I'};
    u16(header, 42);
    u32(header, 8);
    write(file, header);
    write(file, rootBytes);
    write(file, rawBytes);
    write(file, extra);
    write(file, preview);
    if (preview.size() % 2)
        write(file, Bytes{0});
    std::vector<float> row(static_cast<std::size_t>(image.width) * 3), converted(row.size());
    for (int y = 0; y < image.height; ++y) {
        checkCancellation(stop);
        for (int x = 0; x < image.width; ++x) {
            const auto source = (static_cast<std::size_t>(y) * image.width + x) * 4;
            for (std::size_t c = 0; c < 3; ++c)
                row[static_cast<std::size_t>(x) * 3 + c] =
                    image.rgba[source + c] * image.rgba[source + 3];
        }
        convert(row, converted);
        Bytes samples;
        samples.reserve(row.size() * (floating ? 4 : 2));
        for (float value : converted) {
            if (!std::isfinite(value))
                throw std::runtime_error("Non-finite DNG output sample.");
            if (floating)
                u32(samples, std::bit_cast<std::uint32_t>(value));
            else
                u16(samples, static_cast<std::uint16_t>(std::lround(
                                 std::clamp(static_cast<double>(value), 0.0, 1.0) * 65535)));
        }
        write(file, samples);
        if (progress)
            progress(static_cast<double>(y + 1) / image.height);
    }
    file.close();
    if (!file)
        throw std::runtime_error("Cannot finalize DNG output.");
    checkCancellation(stop);
}
} // namespace luma
