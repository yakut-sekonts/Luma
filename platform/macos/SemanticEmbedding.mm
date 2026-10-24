#include "platform/macos/SemanticEmbedding.h"
#include "core/ai/Embedding.h"
#include "core/image/Bayer.h"
#include "platform/macos/CFHandle.h"
#import <NaturalLanguage/NaturalLanguage.h>
#import <Vision/Vision.h>
#include <algorithm>

namespace luma::macos {
namespace {
class SemanticEmbedding final : public ai::IEmbeddingProvider {
  public:
    SemanticEmbedding() {
        @autoreleasepool {
            embedding_ = [NLEmbedding sentenceEmbeddingForLanguage:NLLanguageEnglish];
            if (!embedding_)
                throw std::runtime_error(
                    "The local English sentence model is unavailable on this Mac.");
            if (!embedding_.dimension || embedding_.dimension > 2048)
                throw std::runtime_error("Unsupported local sentence model.");
            identifier_ = "apple.vision-labels-v1.english-sentence-" +
                          std::to_string(embedding_.revision) + ".mean-v1";
        }
    }
    std::string identifier() const override {
        return identifier_;
    }
    ai::ExecutionLocation location() const override {
        return ai::ExecutionLocation::local;
    }
    std::vector<float> textEmbedding(const std::string &text, CancellationToken stop) override {
        checkCancellation(stop);
        if (text.empty() || text.size() > 2048 || text.find('\0') != std::string::npos)
            throw std::runtime_error("Enter a short English description.");
        @autoreleasepool {
            NSString *sentence = [[NSString alloc] initWithBytes:text.data()
                                                          length:text.size()
                                                        encoding:NSUTF8StringEncoding];
            if (!sentence)
                throw std::runtime_error("Invalid semantic query text.");
            const auto result = vector(sentence);
            checkCancellation(stop);
            return result;
        }
    }
    std::vector<float> imageEmbedding(ai::RgbaImage input, CancellationToken stop) override {
        input.validate();
        checkCancellation(stop);
        @autoreleasepool {
            CFHandle color(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
            CFHandle data(CGDataProviderCreateWithData(nullptr, input.pixels.data(),
                                                       input.pixels.size(), nullptr));
            CFHandle image(CGImageCreate(
                input.width, input.height, 8, 32, input.width * 4, color.get(),
                static_cast<CGBitmapInfo>(kCGImageAlphaLast) | kCGBitmapByteOrder32Big, data.get(),
                nullptr, false, kCGRenderingIntentDefault));
            if (!image)
                throw std::runtime_error("Cannot prepare semantic search image.");
            VNClassifyImageRequest *request = [[VNClassifyImageRequest alloc] init];
            request.revision = VNClassifyImageRequestRevision1;
            VNImageRequestHandler *handler =
                [[VNImageRequestHandler alloc] initWithCGImage:image.get() options:@{}];
            NSError *error = nil;
            if (![handler performRequests:@[ request ] error:&error])
                throw std::runtime_error("Local image classification failed.");
            checkCancellation(stop);
            std::vector<float> result(embedding_.dimension);
            std::size_t used = 0;
            for (VNClassificationObservation *observation in request.results) {
                if (used >= 16)
                    break;
                if (!std::isfinite(observation.confidence) || observation.confidence < .02F)
                    continue;
                NSString *label =
                    [observation.identifier stringByReplacingOccurrencesOfString:@"_"
                                                                      withString:@" "];
                const auto values = vector(label);
                for (std::size_t i = 0; i < values.size(); ++i)
                    result[i] += values[i] * observation.confidence;
                ++used;
            }
            checkCancellation(stop);
            return ai::normalizedEmbedding(result);
        }
    }

  private:
    std::vector<float> vector(NSString *text) {
        NSArray<NSNumber *> *values = [embedding_ vectorForString:text];
        if (!values || values.count != embedding_.dimension)
            throw std::runtime_error("The local language model could not encode this description.");
        std::vector<float> result;
        result.reserve(values.count);
        for (NSNumber *value in values)
            result.push_back(value.floatValue);
        return ai::normalizedEmbedding(result);
    }
    NLEmbedding *__strong embedding_;
    std::string identifier_;
};
} // namespace
std::shared_ptr<ai::IEmbeddingProvider> makeSemanticEmbedding() {
    return std::make_shared<SemanticEmbedding>();
}
} // namespace luma::macos
