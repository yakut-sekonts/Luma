// Bounded qualification bridge using the actual application provider and tiler.
#include "platform/macos/CoreMLDenoise.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int dimension(const char *text) {
    std::size_t end = 0;
    const std::string value(text);
    const int result = std::stoi(value, &end);
    require(end == value.size() && result > 0 && result <= 2048,
            "Invalid bounded runtime fixture dimension");
    return result;
}
template <class Function> void rejected(Function function, const char *expected) {
    bool refused = false;
    try {
        function();
    } catch (const std::runtime_error &error) {
        require(error.what() == std::string(expected), "Request failed for an unexpected reason");
        refused = true;
    }
    require(refused, "Invalid or cancelled runtime request was accepted");
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 7 || (argc == 8 && std::string(argv[7]) == "contracts"),
                "Expected model, input, output, width, height, amount and optional contracts");
        luma::FloatImage input(dimension(argv[4]), dimension(argv[5]));
        std::ifstream source(argv[2], std::ios::binary | std::ios::ate);
        const auto bytes = static_cast<std::streamsize>(input.rgb.size() * sizeof(float));
        require(source && source.tellg() == bytes, "Invalid runtime fixture byte count");
        source.seekg(0);
        source.read(reinterpret_cast<char *>(input.rgb.data()), bytes);
        require(bool(source), "Cannot read complete runtime fixture");
        const std::string text(argv[6]);
        std::size_t end = 0;
        const double amount = std::stod(text, &end);
        require(end == text.size(), "Invalid runtime amount argument");
        input.alpha.resize(input.rgb.size() / 3);
        for (std::size_t i = 0; i < input.alpha.size(); ++i)
            input.alpha[i] = static_cast<float>(i % 5) / 4;
        const auto original = input;
        auto provider = luma::macos::makeCoreMLDenoise(argv[1]);
        QJsonArray progress;
        const auto result = provider->denoise(input, amount, {}, [&](double value) {
            require(std::isfinite(value) && value > 0 && value <= 1 &&
                        (progress.isEmpty() || value > progress.last().toDouble()),
                    "Runtime progress is invalid or not monotonic");
            progress.append(value);
        });
        require(result.width == input.width && result.height == input.height &&
                    result.rgb.size() == input.rgb.size() && result.alpha == original.alpha &&
                    input.rgb == original.rgb && input.alpha == original.alpha,
                "Denoise mutated its input, dimensions or alpha coverage");
        require(std::all_of(result.rgb.begin(), result.rgb.end(),
                            [](float value) { return std::isfinite(value); }),
                "Non-finite runtime output");
        require(amount == 0 || (!progress.isEmpty() && progress.last().toDouble() == 1),
                "Runtime progress did not finish");
        QJsonObject report{{"identifier", QString::fromStdString(provider->identifier())},
                           {"progress", progress},
                           {"alpha_preserved", true},
                           {"input_unchanged", true},
                           {"requested_compute_units", "ALL"}};
        if (argc == 8) {
            require(amount == 1 && progress.size() > 1, "Contracts require full multi-tile output");
            const auto zero = provider->denoise(input, 0, {}, [](double) {
                throw std::runtime_error("Amount zero performed prediction work");
            });
            require(zero.rgb == input.rgb && zero.alpha == input.alpha,
                    "Amount zero was not exact identity");
            const auto half = provider->denoise(input, .5, {}, {});
            require(half.width == input.width && half.height == input.height &&
                        half.rgb.size() == input.rgb.size(),
                    "Amount blend changed image dimensions or storage");
            double blendError = 0;
            for (std::size_t i = 0; i < result.rgb.size(); ++i)
                blendError =
                    std::max(blendError, std::abs(static_cast<double>(half.rgb[i]) -
                                                  (input.rgb[i] + double(result.rgb[i])) / 2));
            require(blendError <= 5e-6 && half.alpha == input.alpha, "Amount blend mismatch");
            luma::CancellationSource cancelled;
            cancelled.cancel();
            rejected([&] { provider->denoise(input, 1, cancelled.token(), {}); },
                     "Operation cancelled.");
            luma::CancellationSource during;
            int completed = 0;
            rejected(
                [&] {
                    provider->denoise(input, 1, during.token(), [&](double) {
                        ++completed;
                        during.cancel();
                    });
                },
                "Operation cancelled.");
            require(completed == 1 && input.rgb == original.rgb,
                    "Cancellation did not stop after the first completed tile");
            for (double invalid : {-1.0, 1.1, std::numeric_limits<double>::infinity(),
                                   std::numeric_limits<double>::quiet_NaN()})
                rejected([&] { provider->denoise(input, invalid, {}, {}); },
                         "Invalid AI denoise amount.");
            report["contracts"] = QJsonObject{{"amount_zero_identity", true},
                                              {"half_amount_max_error", blendError},
                                              {"pre_cancelled_rejected", true},
                                              {"cancelled_after_first_tile", true},
                                              {"invalid_amounts_rejected", true}};
        }
        std::ofstream target(argv[3], std::ios::binary | std::ios::trunc);
        target.write(reinterpret_cast<const char *>(result.rgb.data()), bytes);
        target.close();
        require(bool(target), "Cannot write complete runtime output");
        std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
