// Bounded bridge through the application's real Sky preprocessing and provider.
#include "platform/macos/CoreMLSky.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
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
            "Invalid bounded Sky fixture dimension");
    return result;
}
template <class Function> void rejected(Function function, const char *expected) {
    bool refused = false;
    try {
        function();
    } catch (const std::runtime_error &error) {
        require(error.what() == std::string(expected),
                "Sky request failed for an unexpected reason");
        refused = true;
    }
    require(refused, "Invalid or cancelled Sky request was accepted");
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 6 || (argc == 7 && std::string(argv[6]) == "contracts"),
                "Expected model, RGBA input, mask output, width, height and optional contracts");
        const int width = dimension(argv[4]), height = dimension(argv[5]);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
        std::ifstream source(argv[2], std::ios::binary | std::ios::ate);
        const auto bytes = static_cast<std::streamsize>(pixels.size());
        require(source && source.tellg() == bytes, "Invalid Sky fixture byte count");
        source.seekg(0);
        source.read(reinterpret_cast<char *>(pixels.data()), bytes);
        require(bool(source), "Cannot read complete Sky fixture");
        const auto original = pixels;
        const luma::ai::RgbaImage input{width, height, pixels};
        auto provider = luma::macos::makeCoreMLSky(argv[1]);
        require(provider->location() == luma::ai::ExecutionLocation::local &&
                    provider->supports(luma::ai::Selection::sky),
                "Sky provider contract changed");
        QJsonArray progress;
        const auto result =
            provider->segment(input, luma::ai::Selection::sky, {}, {}, [&](double value) {
                require(std::isfinite(value) && value > 0 && value <= 1 &&
                            (progress.isEmpty() || value > progress.last().toDouble()),
                        "Sky progress is not finite and monotonic");
                progress.append(value);
            });
        require(!result.empty() &&
                    result.pixels().size() ==
                        static_cast<std::size_t>(result.width()) * result.height() &&
                    !progress.isEmpty() && progress.last().toDouble() == 1 && pixels == original,
                "Sky changed its input or failed its mask/progress contract");
        QJsonObject report{{"identifier", QString::fromStdString(provider->identifier())},
                           {"width", result.width()},
                           {"height", result.height()},
                           {"progress", progress},
                           {"input_unchanged", true},
                           {"requested_compute_units", "ALL"}};
        if (argc == 7) {
            for (const auto category :
                 {luma::ai::Selection::subject, luma::ai::Selection::background,
                  luma::ai::Selection::object}) {
                require(!provider->supports(category), "Sky advertises an unsupported category");
                rejected([&] { provider->segment(input, category, {}, {}, {}); },
                         "Sky model cannot select this category.");
            }
            rejected(
                [&] {
                    provider->segment({width, height, std::span(pixels).subspan(1)},
                                      luma::ai::Selection::sky, {}, {}, {});
                },
                "Invalid AI input image.");
            luma::CancellationSource cancelled;
            cancelled.cancel();
            rejected(
                [&] {
                    provider->segment(input, luma::ai::Selection::sky, {}, cancelled.token(), {});
                },
                "Operation cancelled.");
            luma::CancellationSource during;
            QJsonArray cancelledProgress;
            rejected(
                [&] {
                    provider->segment(input, luma::ai::Selection::sky, {}, during.token(),
                                      [&](double value) {
                                          cancelledProgress.append(value);
                                          during.cancel();
                                      });
                },
                "Operation cancelled.");
            require(cancelledProgress.size() == 1 && cancelledProgress.first().toDouble() < 1,
                    "Cancelled Sky reported completion");
            const auto again = provider->segment(input, luma::ai::Selection::sky, {}, {}, {});
            require(again == result && pixels == original,
                    "Sky failed to recover after cancellation");
            report["contracts"] = QJsonObject{{"unsupported_categories_rejected", true},
                                              {"invalid_buffer_rejected", true},
                                              {"pre_cancel", true},
                                              {"after_resize_cancel", true},
                                              {"repeat_after_cancel_identical", true},
                                              {"cancelled_progress", cancelledProgress}};
        }
        std::ofstream output(argv[3], std::ios::binary);
        output.write(reinterpret_cast<const char *>(result.pixels().data()),
                     static_cast<std::streamsize>(result.pixels().size()));
        require(bool(output), "Cannot write Sky mask");
        std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).toStdString() << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
