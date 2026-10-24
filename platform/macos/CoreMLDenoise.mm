#include "platform/macos/CoreMLDenoise.h"
#include "core/image/Bayer.h"
#include "platform/macos/InferenceStack.h"
#import <CoreML/CoreML.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>

namespace luma::macos {
namespace {
std::runtime_error failure(NSError *error, const char *fallback) {
    return std::runtime_error(error ? error.localizedDescription.UTF8String : fallback);
}
std::array<int, 2> dimensions(MLFeatureDescription *feature) {
    const auto constraint = feature.multiArrayConstraint;
    const auto shape = constraint.shape;
    if (feature.type != MLFeatureTypeMultiArray ||
        constraint.dataType != MLMultiArrayDataTypeFloat32 || shape.count != 4 ||
        shape[0].integerValue != 1 || shape[1].integerValue != 3 || shape[2].integerValue < 64 ||
        shape[2].integerValue > 1024 || shape[3].integerValue < 64 || shape[3].integerValue > 1024)
        throw std::runtime_error(
            "Denoise model requires Float32 [1,3,H,W] tensors (64–1024 pixels).");
    return {static_cast<int>(shape[3].integerValue), static_cast<int>(shape[2].integerValue)};
}
std::array<std::size_t, 3> strides(NSArray<NSNumber *> *values, NSInteger bytes, int w, int h) {
    if (values.count != 4 || values[1].longLongValue < 1 || values[2].longLongValue < 1 ||
        values[3].longLongValue < 1 || bytes < 0)
        throw std::runtime_error("Invalid denoise tensor strides.");
    std::array<std::size_t, 3> result{static_cast<std::size_t>(values[1].unsignedLongLongValue),
                                      static_cast<std::size_t>(values[2].unsignedLongLongValue),
                                      static_cast<std::size_t>(values[3].unsignedLongLongValue)};
    const auto size = static_cast<std::size_t>(bytes) / sizeof(float);
    // Check bounds before arithmetic so malformed tensor layouts cannot overflow.
    if (!size || result[0] > size / 3 || result[1] > size / static_cast<std::size_t>(h) ||
        result[2] > size / static_cast<std::size_t>(w) ||
        2 * result[0] + static_cast<std::size_t>(h - 1) * result[1] +
                static_cast<std::size_t>(w - 1) * result[2] >=
            size)
        throw std::runtime_error("Denoise tensor exceeds its storage.");
    return result;
}
int reflect(int value, int count) {
    if (count == 1)
        return 0;
    const int period = 2 * (count - 1);
    value = (value % period + period) % period;
    return value < count ? value : period - value;
}
class CoreMLDenoise final : public ai::IDenoiseProvider {
  public:
    explicit CoreMLDenoise(const std::filesystem::path &path) {
        @autoreleasepool {
            if (path.extension() != ".mlmodelc" || !std::filesystem::is_directory(path))
                throw std::runtime_error("Choose a compiled local denoise model (.mlmodelc).");
            NSString *modelPath = [NSString stringWithUTF8String:path.c_str()];
            if (!modelPath)
                throw std::runtime_error("The denoise model path is not valid UTF-8.");
            url_ = [NSURL fileURLWithPath:modelPath isDirectory:YES];
            model_ = load(MLComputeUnitsAll);
            NSDictionary *metadata = model_.modelDescription.metadata[MLModelCreatorDefinedKey];
            if (![metadata[@"luma.denoise.schema"] isEqual:@"1"] ||
                ![metadata[@"luma.denoise.color"] isEqual:@"linear-srgb"] ||
                (![metadata[@"luma.denoise.halo"] isEqual:@"16"] &&
                 ![metadata[@"luma.denoise.halo"] isEqual:@"64"]))
                throw std::runtime_error("Model is missing the Luma linear RGB denoise contract.");
            halo_ = [metadata[@"luma.denoise.halo"] isEqual:@"64"] ? 64 : 16;
            NSString *modelID = metadata[@"luma.model.id"];
            if ([modelID isKindOfClass:NSString.class] && modelID.length > 0 &&
                modelID.length <= 96) {
                const char *identifier = modelID.UTF8String;
                const std::string value = identifier ? identifier : "";
                if (std::all_of(value.begin(), value.end(), [](unsigned char c) {
                        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                               (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
                    }))
                    identifier_ += ":" + value;
            }
            const auto inputs = model_.modelDescription.inputDescriptionsByName;
            const auto outputs = model_.modelDescription.outputDescriptionsByName;
            if (inputs.count != 1 || outputs.count != 1 || !inputs[@"noisy"] ||
                !outputs[@"denoised"])
                throw std::runtime_error("Denoise model must expose noisy → denoised tensors.");
            size_ = dimensions(inputs[@"noisy"]);
            if (size_[0] <= 2 * halo_ || size_[1] <= 2 * halo_)
                throw std::runtime_error("Denoise model halo leaves no usable tile pixels.");
            if (dimensions(outputs[@"denoised"]) != size_)
                throw std::runtime_error("Denoise model input and output dimensions must match.");
        }
    }
    std::string identifier() const override {
        return identifier_;
    }
    ai::ExecutionLocation location() const override {
        return ai::ExecutionLocation::local;
    }
    FloatImage denoise(const FloatImage &linear, double amount, CancellationToken stop,
                       const ai::Progress &progress) override {
        std::lock_guard lock(mutex_);
        std::optional<FloatImage> result;
        withInferenceStack(
            [&] { result.emplace(denoiseWithStack(linear, amount, stop, progress)); });
        return std::move(*result);
    }

  private:
    FloatImage denoiseWithStack(const FloatImage &linear, double amount, CancellationToken stop,
                                const ai::Progress &progress) {
        linear.validateCoverage();
        if (!std::isfinite(amount) || amount < 0 || amount > 1)
            throw std::runtime_error("Invalid AI denoise amount.");
        checkCancellation(stop);
        FloatImage result = linear;
        if (amount == 0)
            return result;
        const int width = size_[0], height = size_[1], halo = halo_, stepX = width - 2 * halo,
                  stepY = height - 2 * halo;
        const int columns = (linear.width + stepX - 1) / stepX,
                  rows = (linear.height + stepY - 1) / stepY;
        int completed = 0;
        for (int y = 0; y < linear.height; y += stepY)
            for (int x = 0; x < linear.width; x += stepX) {
                @autoreleasepool {
                    checkCancellation(stop);
                    NSError *error = nil;
                    MLMultiArray *tensor =
                        [[MLMultiArray alloc] initWithShape:@[ @1, @3, @(height), @(width) ]
                                                   dataType:MLMultiArrayDataTypeFloat32
                                                      error:&error];
                    if (!tensor)
                        throw failure(error, "Cannot allocate denoise tensor.");
                    [tensor getMutableBytesWithHandler:^(void *bytes, NSInteger length,
                                                         NSArray<NSNumber *> *layout) {
                      const auto stride = strides(layout, length, width, height);
                      auto *values = static_cast<float *>(bytes);
                      for (int ty = 0; ty < height; ++ty) {
                          checkCancellation(stop);
                          for (int tx = 0; tx < width; ++tx) {
                              const int sx = reflect(x + tx - halo, linear.width),
                                        sy = reflect(y + ty - halo, linear.height);
                              const auto i = (static_cast<std::size_t>(sy) * linear.width + sx) * 3;
                              for (std::size_t c = 0; c < 3; ++c) {
                                  const float v = linear.rgb[i + c];
                                  if (!std::isfinite(v))
                                      throw std::runtime_error("Non-finite denoise input.");
                                  values[c * stride[0] + static_cast<std::size_t>(ty) * stride[1] +
                                         static_cast<std::size_t>(tx) * stride[2]] = v;
                              }
                          }
                      }
                    }];
                    auto *features =
                        [[MLDictionaryFeatureProvider alloc] initWithDictionary:@{
                            @"noisy" : [MLFeatureValue featureValueWithMultiArray:tensor]
                        }
                                                                          error:&error];
                    if (!features)
                        throw failure(error, "Cannot prepare denoise input.");
                    id<MLFeatureProvider> prediction = [model_ predictionFromFeatures:features
                                                                                error:&error];
                    if (!prediction && !cpuOnly_) {
                        checkCancellation(stop);
                        model_ = load(MLComputeUnitsCPUOnly);
                        cpuOnly_ = true;
                        prediction = [model_ predictionFromFeatures:features error:&error];
                    }
                    if (!prediction)
                        throw failure(error, "Local denoise prediction failed.");
                    checkCancellation(stop);
                    MLMultiArray *output =
                        [prediction featureValueForName:@"denoised"].multiArrayValue;
                    if (!output || output.dataType != MLMultiArrayDataTypeFloat32 ||
                        ![output.shape isEqualToArray:tensor.shape])
                        throw std::runtime_error("Invalid denoise output tensor.");
                    FloatImage *destination =
                        &result; // Blocks otherwise capture C++ values by copy.
                    [output getBytesWithHandler:^(const void *bytes, NSInteger length) {
                      const auto stride = strides(output.strides, length, width, height);
                      const auto *values = static_cast<const float *>(bytes);
                      for (int py = 0; py < std::min(stepY, linear.height - y); ++py) {
                          checkCancellation(stop);
                          for (int px = 0; px < std::min(stepX, linear.width - x); ++px) {
                              const auto i =
                                  (static_cast<std::size_t>(y + py) * linear.width + x + px) * 3;
                              for (std::size_t c = 0; c < 3; ++c) {
                                  const float value =
                                      values[c * stride[0] +
                                             static_cast<std::size_t>(py + halo) * stride[1] +
                                             static_cast<std::size_t>(px + halo) * stride[2]];
                                  if (!std::isfinite(value))
                                      throw std::runtime_error("Non-finite denoise output.");
                                  destination->rgb[i + c] = static_cast<float>(
                                      std::lerp(static_cast<double>(linear.rgb[i + c]),
                                                static_cast<double>(value), amount));
                              }
                          }
                      }
                    }];
                    if (progress)
                        progress(static_cast<double>(++completed) / (columns * rows));
                }
            }
        return result;
    }

  private:
    MLModel *load(MLComputeUnits units) {
        auto *configuration = [[MLModelConfiguration alloc] init];
        configuration.computeUnits = units;
        NSError *error = nil;
        MLModel *model = [MLModel modelWithContentsOfURL:url_
                                           configuration:configuration
                                                   error:&error];
        if (!model && units != MLComputeUnitsCPUOnly) {
            configuration.computeUnits = MLComputeUnitsCPUOnly;
            model = [MLModel modelWithContentsOfURL:url_ configuration:configuration error:&error];
            cpuOnly_ = true;
        }
        if (!model)
            throw failure(error, "Cannot load local denoise model.");
        return model;
    }
    NSURL *__strong url_;
    MLModel *__strong model_;
    std::array<int, 2> size_{};
    int halo_ = 16;
    std::string identifier_ = "coreml.linear-denoise.v1";
    bool cpuOnly_ = false;
    std::mutex mutex_;
};
} // namespace
std::shared_ptr<ai::IDenoiseProvider> makeCoreMLDenoise(const std::filesystem::path &path) {
    return std::make_shared<CoreMLDenoise>(path);
}
} // namespace luma::macos
