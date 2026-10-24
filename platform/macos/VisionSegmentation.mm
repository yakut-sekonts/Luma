#include "platform/macos/VisionSegmentation.h"
#include "core/image/Bayer.h"
#include "platform/macos/CFHandle.h"
#import <Vision/Vision.h>
#include <algorithm>
#include <cmath>
#include <dispatch/dispatch.h>

namespace luma::macos {
namespace {
class PixelLock {
  public:
    explicit PixelLock(CVPixelBufferRef buffer) : buffer_(buffer) {
        if (!buffer_ ||
            CVPixelBufferLockBaseAddress(buffer_, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
            throw std::runtime_error("Cannot read Vision pixel buffer.");
    }
    ~PixelLock() {
        CVPixelBufferUnlockBaseAddress(buffer_, kCVPixelBufferLock_ReadOnly);
    }

  private:
    CVPixelBufferRef buffer_;
};
class RequestCancellation {
  public:
    RequestCancellation(VNRequest *request, CancellationToken stop) {
        timer_ = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                        dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
        if (!timer_)
            throw std::runtime_error("Cannot schedule AI cancellation.");
        dispatch_source_set_timer(timer_, DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC, 5 * NSEC_PER_MSEC);
        dispatch_source_set_event_handler(timer_, ^{
          if (stop.isCancelled())
              [request cancel];
        });
        dispatch_resume(timer_);
    }
    ~RequestCancellation() {
        dispatch_source_cancel(timer_);
    }

  private:
    dispatch_source_t timer_;
};
class VisionSegmentation final : public ai::ISegmentationProvider {
  public:
    std::string identifier() const override {
        return "apple.vision.foreground.v1";
    }
    ai::ExecutionLocation location() const override {
        return ai::ExecutionLocation::local;
    }
    bool supports(ai::Selection selection) const override {
        return selection == ai::Selection::subject || selection == ai::Selection::background ||
               selection == ai::Selection::object;
    }
    BitmapMask segment(ai::RgbaImage image, ai::Selection selection, std::array<double, 2> point,
                       CancellationToken stop, const ai::Progress &progress) override {
        image.validate();
        if (!supports(selection))
            throw std::runtime_error(
                "This local provider does not support the requested selection.");
        for (const auto coordinate : point)
            if (!std::isfinite(coordinate) || coordinate < 0 || coordinate > 1)
                throw std::runtime_error("Object selection point is outside the photo.");
        checkCancellation(stop);
        @autoreleasepool {
            CFHandle color(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
            CFHandle data(CGDataProviderCreateWithData(nullptr, image.pixels.data(),
                                                       image.pixels.size(), nullptr));
            CFHandle cgImage(CGImageCreate(
                image.width, image.height, 8, 32, image.width * 4, color.get(),
                static_cast<CGBitmapInfo>(kCGImageAlphaLast) | kCGBitmapByteOrder32Big, data.get(),
                nullptr, false, kCGRenderingIntentDefault));
            if (!cgImage)
                throw std::runtime_error("Cannot prepare photo for local segmentation.");
            VNImageRequestHandler *handler =
                [[VNImageRequestHandler alloc] initWithCGImage:cgImage.get() options:@{}];
            VNGenerateForegroundInstanceMaskRequest *request =
                [[VNGenerateForegroundInstanceMaskRequest alloc] init];
            request.revision = VNGenerateForegroundInstanceMaskRequestRevision1;
            RequestCancellation cancellation(request, stop);
            if (progress)
                progress(.1);
            NSError *error = nil;
            const BOOL success = [handler performRequests:@[ request ] error:&error];
            checkCancellation(stop);
            if (!success || request.results.count != 1)
                throw std::runtime_error("Local subject detection failed.");
            VNInstanceMaskObservation *observation = request.results.firstObject;
            if (!observation.allInstances.count)
                throw std::runtime_error("No foreground subject was detected.");
            NSIndexSet *instances = observation.allInstances;
            if (selection == ai::Selection::object) {
                CVPixelBufferRef labels = observation.instanceMask;
                if (CVPixelBufferGetPixelFormatType(labels) != kCVPixelFormatType_OneComponent8)
                    throw std::runtime_error("Unsupported Vision instance label format.");
                PixelLock lock(labels);
                const auto width = CVPixelBufferGetWidth(labels),
                           height = CVPixelBufferGetHeight(labels);
                if (!width || !height)
                    throw std::runtime_error("Empty Vision instance mask.");
                const auto x = std::min(static_cast<std::size_t>(point[0] * width), width - 1);
                const auto y = std::min(static_cast<std::size_t>(point[1] * height), height - 1);
                const auto *bytes =
                    static_cast<const std::uint8_t *>(CVPixelBufferGetBaseAddress(labels));
                if (!bytes)
                    throw std::runtime_error("Vision instance labels have no readable storage.");
                const auto label = bytes[y * CVPixelBufferGetBytesPerRow(labels) + x];
                if (!label || ![instances containsIndex:label])
                    throw std::runtime_error("No foreground object was detected at this point.");
                instances = [NSIndexSet indexSetWithIndex:label];
            }
            if (progress)
                progress(.8);
            CFHandle mask([observation generateScaledMaskForImageForInstances:instances
                                                           fromRequestHandler:handler
                                                                        error:&error]);
            checkCancellation(stop);
            if (!mask || CVPixelBufferGetPixelFormatType(mask.get()) !=
                             kCVPixelFormatType_OneComponent32Float)
                throw std::runtime_error("Vision did not return a supported coverage mask.");
            PixelLock lock(mask.get());
            const auto width = CVPixelBufferGetWidth(mask.get()),
                       height = CVPixelBufferGetHeight(mask.get());
            if (width < 1 || height < 1 || width > 1024 || height > 1024)
                throw std::runtime_error("Vision mask exceeds the supported dimensions.");
            std::vector<std::uint8_t> values(width * height);
            const auto *bytes =
                static_cast<const std::uint8_t *>(CVPixelBufferGetBaseAddress(mask.get()));
            if (!bytes)
                throw std::runtime_error("Vision coverage mask has no readable storage.");
            for (std::size_t y = 0; y < height; ++y) {
                checkCancellation(stop);
                const auto *row = reinterpret_cast<const float *>(
                    bytes + y * CVPixelBufferGetBytesPerRow(mask.get()));
                for (std::size_t x = 0; x < width; ++x) {
                    if (!std::isfinite(row[x]))
                        throw std::runtime_error("Vision returned non-finite coverage.");
                    const double value =
                        selection == ai::Selection::background ? 1.0 - row[x] : row[x];
                    values[y * width + x] =
                        static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
                }
            }
            if (progress)
                progress(1);
            return BitmapMask(static_cast<int>(width), static_cast<int>(height), std::move(values));
        }
    }
};
} // namespace
std::shared_ptr<ai::ISegmentationProvider> makeVisionSegmentation() {
    return std::make_shared<VisionSegmentation>();
}
} // namespace luma::macos
