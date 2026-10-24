#include "platform/macos/MetalDevelop.h"
#include "core/image/Bayer.h"
#include "platform/macos/DevelopShader.h"
#import <Metal/Metal.h>
#include <algorithm>
#include <cstring>
#include <mutex>

namespace luma::macos {
namespace {
class MetalDevelop final : public DevelopAccelerator {
  public:
    std::vector<float> develop(const FloatImage &input, const DevelopPipeline &pipeline,
                               CancellationToken stop) override {
        input.validateCoverage();
        checkCancellation(stop);
        if (!pipeline.gpuCompatible())
            return {};
        const auto parameters = pipeline.gpuParameters();
        // One instance owns one queue; bounded chunks allow cancellation between
        // command buffers and cap staging buffers independently of RAW size.
        std::lock_guard lock(mutex_);
        @autoreleasepool {
            initialize();
            if (!pipeline_)
                return {};
            constexpr std::size_t chunkPixels = 262144;
            const auto count = pixelCount(input.width, input.height);
            const auto capacity = std::min(count, chunkPixels) * 3 * sizeof(float);
            id<MTLBuffer> source = [device_ newBufferWithLength:capacity
                                                        options:MTLResourceStorageModeShared];
            id<MTLBuffer> target = [device_ newBufferWithLength:capacity
                                                        options:MTLResourceStorageModeShared];
            id<MTLBuffer> invalid = [device_ newBufferWithLength:sizeof(std::uint32_t)
                                                         options:MTLResourceStorageModeShared];
            id<MTLBuffer> constants = [device_ newBufferWithBytes:parameters.data()
                                                           length:parameters.size() * sizeof(float)
                                                          options:MTLResourceStorageModeShared];
            if (!source || !target || !invalid || !constants)
                return {};
            std::vector<float> result(input.rgb.size());
            for (std::size_t first = 0; first < count; first += chunkPixels) {
                checkCancellation(stop);
                const auto size = static_cast<std::uint32_t>(std::min(chunkPixels, count - first));
                const auto bytes = static_cast<std::size_t>(size) * 3 * sizeof(float);
                std::memcpy(source.contents, input.rgb.data() + first * 3, bytes);
                std::memset(invalid.contents, 0, sizeof(std::uint32_t));
                id<MTLCommandBuffer> command = [queue_ commandBuffer];
                id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
                if (!command || !encoder)
                    return {};
                [encoder setComputePipelineState:pipeline_];
                [encoder setBuffer:source offset:0 atIndex:0];
                [encoder setBuffer:target offset:0 atIndex:1];
                [encoder setBuffer:constants offset:0 atIndex:2];
                [encoder setBytes:&size length:sizeof(size) atIndex:3];
                [encoder setBuffer:invalid offset:0 atIndex:4];
                const auto threads =
                    std::min<NSUInteger>(256, pipeline_.maxTotalThreadsPerThreadgroup);
                [encoder dispatchThreads:MTLSizeMake(size, 1, 1)
                    threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
                [encoder endEncoding];
                [command commit];
                [command waitUntilCompleted];
                checkCancellation(stop);
                if (command.status != MTLCommandBufferStatusCompleted) {
                    // Do not repeatedly submit to a failing GPU during slider gestures.
                    pipeline_ = nil;
                    return {};
                }
                if (*static_cast<const std::uint32_t *>(invalid.contents) != 0)
                    throw std::runtime_error("Non-finite accelerated Develop pixel.");
                std::memcpy(result.data() + first * 3, target.contents, bytes);
            }
            return result;
        }
    }

  private:
    void initialize() {
        if (initialized_)
            return;
        initialized_ = true;
        device_ = MTLCreateSystemDefaultDevice();
        if (!device_)
            return;
        queue_ = [device_ newCommandQueue];
        MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
        if (@available(macOS 15.0, *)) {
            options.mathMode = MTLMathModeSafe;
            options.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;
        } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            options.fastMathEnabled = NO;
#pragma clang diagnostic pop
        }
        NSError *error = nil;
        NSString *source = [NSString stringWithUTF8String:developShader];
        if (!source)
            return;
        id<MTLLibrary> library = [device_ newLibraryWithSource:source options:options error:&error];
        if (!library || !queue_)
            return;
        id<MTLFunction> function = [library newFunctionWithName:@"develop"];
        if (function)
            pipeline_ = [device_ newComputePipelineStateWithFunction:function error:&error];
    }
    std::mutex mutex_;
    bool initialized_ = false;
    id<MTLDevice> device_;
    id<MTLCommandQueue> queue_;
    id<MTLComputePipelineState> pipeline_;
};
} // namespace
std::shared_ptr<DevelopAccelerator> makeMetalDevelopAccelerator() {
    return std::make_shared<MetalDevelop>();
}
} // namespace luma::macos
