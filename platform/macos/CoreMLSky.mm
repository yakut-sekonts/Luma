#include "platform/macos/CoreMLSky.h"
#include "core/image/Bayer.h"
#import <CoreML/CoreML.h>
#include <algorithm>
#include <cmath>

namespace luma::macos {
namespace {
std::array<int, 2> dimensions(MLFeatureDescription *feature, int channels) {
    const auto constraint = feature.multiArrayConstraint;
    const auto shape = constraint.shape;
    if (feature.type != MLFeatureTypeMultiArray ||
        constraint.dataType != MLMultiArrayDataTypeFloat32 || shape.count != 4 ||
        shape[0].integerValue != 1 || shape[1].integerValue != channels ||
        shape[2].integerValue < 64 || shape[2].integerValue > 1024 || shape[3].integerValue < 64 ||
        shape[3].integerValue > 1024)
        throw std::runtime_error("Sky model requires Float32 [1,3,H,W] image and [1,1,H,W] sky "
                                 "tensors (64–1024 pixels).");
    return {static_cast<int>(shape[3].integerValue), static_cast<int>(shape[2].integerValue)};
}
std::array<std::size_t, 3> strides(NSArray<NSNumber *> *layout, NSInteger bytes, int channels,
                                   int width, int height) {
    if (layout.count != 4 || bytes < 0 || layout[1].longLongValue < 1 ||
        layout[2].longLongValue < 1 || layout[3].longLongValue < 1)
        throw std::runtime_error("Invalid sky tensor strides.");
    const std::array<std::size_t, 3> stride{
        static_cast<std::size_t>(layout[1].unsignedLongLongValue),
        static_cast<std::size_t>(layout[2].unsignedLongLongValue),
        static_cast<std::size_t>(layout[3].unsignedLongLongValue)};
    const auto count = static_cast<std::size_t>(bytes) / sizeof(float);
    if (!count || stride[0] > count / channels || stride[1] > count / height ||
        stride[2] > count / width ||
        (channels - 1) * stride[0] + (height - 1) * stride[1] + (width - 1) * stride[2] >= count)
        throw std::runtime_error("Sky tensor exceeds its storage.");
    return stride;
}
class CoreMLSky final : public ai::ISegmentationProvider {
  public:
    explicit CoreMLSky(const std::filesystem::path &path) {
        @autoreleasepool {
            if (path.extension() != ".mlmodelc" || !std::filesystem::is_directory(path))
                throw std::runtime_error("Choose a compiled local Sky model (.mlmodelc).");
            NSString *modelPath = [NSString stringWithUTF8String:path.c_str()];
            if (!modelPath)
                throw std::runtime_error("The Sky model path is not valid UTF-8.");
            url_ = [NSURL fileURLWithPath:modelPath isDirectory:YES];
            model_ = load(false);
            NSDictionary *metadata = model_.modelDescription.metadata[MLModelCreatorDefinedKey];
            if (![metadata[@"luma.sky.schema"] isEqual:@"1"] ||
                ![metadata[@"luma.sky.color"] isEqual:@"srgb"] ||
                ![metadata[@"luma.sky.resize"] isEqual:@"stretch"])
                throw std::runtime_error("Model is missing the Luma Sky segmentation contract.");
            const auto inputs = model_.modelDescription.inputDescriptionsByName,
                       outputs = model_.modelDescription.outputDescriptionsByName;
            if (inputs.count != 1 || outputs.count != 1 || !inputs[@"image"] || !outputs[@"sky"])
                throw std::runtime_error("Sky model must expose image → sky tensors.");
            size_ = dimensions(inputs[@"image"], 3);
            if (dimensions(outputs[@"sky"], 1) != size_)
                throw std::runtime_error("Sky model input and output dimensions must match.");
        }
    }
    std::string identifier() const override {
        return "coreml.sky.v1";
    }
    ai::ExecutionLocation location() const override {
        return ai::ExecutionLocation::local;
    }
    bool supports(ai::Selection selection) const override {
        return selection == ai::Selection::sky;
    }
    BitmapMask segment(ai::RgbaImage image, ai::Selection selection, std::array<double, 2>,
                       CancellationToken stop, const ai::Progress &progress) override {
        @autoreleasepool {
            image.validate();
            checkCancellation(stop);
            if (!supports(selection))
                throw std::runtime_error("Sky model cannot select this category.");
            const int width = size_[0], height = size_[1];
            NSError *error = nil;
            MLMultiArray *input =
                [[MLMultiArray alloc] initWithShape:@[ @1, @3, @(height), @(width) ]
                                           dataType:MLMultiArrayDataTypeFloat32
                                              error:&error];
            if (!input)
                throw std::runtime_error("Cannot allocate Sky input tensor.");
            [input getMutableBytesWithHandler:^(void *bytes, NSInteger length,
                                                NSArray<NSNumber *> *layout) {
              const auto stride = strides(layout, length, 3, width, height);
              auto *values = static_cast<float *>(bytes);
              for (int y = 0; y < height; ++y) {
                  checkCancellation(stop);
                  const double sy =
                      std::clamp((y + .5) * image.height / height - .5, 0.0, image.height - 1.0);
                  const int top = static_cast<int>(sy);
                  for (int x = 0; x < width; ++x) {
                      const double sx =
                          std::clamp((x + .5) * image.width / width - .5, 0.0, image.width - 1.0);
                      const int left = static_cast<int>(sx);
                      for (std::size_t c = 0; c < 3; ++c) {
                          double value = 0;
                          for (int dy = 0; dy < 2; ++dy)
                              for (int dx = 0; dx < 2; ++dx) {
                                  const auto i = (static_cast<std::size_t>(
                                                      std::min(top + dy, image.height - 1)) *
                                                      image.width +
                                                  std::min(left + dx, image.width - 1)) *
                                                 4;
                                  const double weight = (dx ? sx - left : 1 - sx + left) *
                                                        (dy ? sy - top : 1 - sy + top);
                                  value += weight * image.pixels[i + c] / 255.0;
                              }
                          values[c * stride[0] + static_cast<std::size_t>(y) * stride[1] +
                                 static_cast<std::size_t>(x) * stride[2]] =
                              static_cast<float>(value);
                      }
                  }
              }
            }];
            if (progress)
                progress(.15);
            auto *features = [[MLDictionaryFeatureProvider alloc]
                initWithDictionary:@{@"image" : [MLFeatureValue featureValueWithMultiArray:input]}
                             error:&error];
            if (!features)
                throw std::runtime_error("Cannot prepare Sky features.");
            id<MLFeatureProvider> prediction = [model_ predictionFromFeatures:features
                                                                        error:&error];
            if (!prediction && !cpuOnly_) {
                checkCancellation(stop);
                model_ = load(true);
                prediction = [model_ predictionFromFeatures:features error:&error];
            }
            if (!prediction)
                throw std::runtime_error(error ? error.localizedDescription.UTF8String
                                               : "Sky prediction failed.");
            checkCancellation(stop);
            MLMultiArray *output = [prediction featureValueForName:@"sky"].multiArrayValue;
            if (!output || output.dataType != MLMultiArrayDataTypeFloat32 ||
                ![output.shape isEqualToArray:@[ @1, @1, @(height), @(width) ]])
                throw std::runtime_error("Invalid Sky probability tensor.");
            std::vector<std::uint8_t> coverage(pixelCount(width, height));
            auto *destination = &coverage;
            [output getBytesWithHandler:^(const void *bytes, NSInteger length) {
              const auto stride = strides(output.strides, length, 1, width, height);
              const auto *values = static_cast<const float *>(bytes);
              for (int y = 0; y < height; ++y) {
                  checkCancellation(stop);
                  for (int x = 0; x < width; ++x) {
                      const auto value = values[static_cast<std::size_t>(y) * stride[1] +
                                                static_cast<std::size_t>(x) * stride[2]];
                      if (!std::isfinite(value) || value < 0 || value > 1)
                          throw std::runtime_error(
                              "Sky model output must be a probability in [0,1].");
                      (*destination)[static_cast<std::size_t>(y) * width + x] =
                          static_cast<std::uint8_t>(std::lround(value * 255));
                  }
              }
            }];
            if (progress)
                progress(1);
            return {width, height, std::move(coverage)};
        }
    }

  private:
    MLModel *load(bool cpu) {
        auto *configuration = [[MLModelConfiguration alloc] init];
        configuration.computeUnits = cpu ? MLComputeUnitsCPUOnly : MLComputeUnitsAll;
        NSError *error = nil;
        MLModel *model = [MLModel modelWithContentsOfURL:url_
                                           configuration:configuration
                                                   error:&error];
        if (!model && !cpu)
            return load(true);
        if (!model)
            throw std::runtime_error(error ? error.localizedDescription.UTF8String
                                           : "Cannot load local Sky model.");
        cpuOnly_ = cpu;
        return model;
    }
    NSURL *__strong url_;
    MLModel *__strong model_;
    std::array<int, 2> size_{};
    bool cpuOnly_ = false;
};
} // namespace
std::shared_ptr<ai::ISegmentationProvider> makeCoreMLSky(const std::filesystem::path &path) {
    return std::make_shared<CoreMLSky>(path);
}
} // namespace luma::macos
