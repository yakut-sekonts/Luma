// Standalone test bridge. Core ML owns its tensor storage; Python never lends
// NumPy memory to libcoremlpython's asynchronous input deallocator.
#import <CoreML/CoreML.h>
#import <Foundation/Foundation.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    @autoreleasepool {
        try {
            if (argc != 8 && argc != 9)
                throw std::runtime_error(
                    "Expected model, input, output, input name, output name, size, channels and "
                    "optional compute units.");
            const int size = std::stoi(argv[6]), channels = std::stoi(argv[7]);
            if ((size != 320 && size != 512) || (channels != 1 && channels != 3))
                throw std::runtime_error("Unexpected test tensor dimensions.");
            NSString *modelPath = [NSString stringWithUTF8String:argv[1]];
            NSString *inputPath = [NSString stringWithUTF8String:argv[2]];
            NSString *outputPath = [NSString stringWithUTF8String:argv[3]];
            NSString *inputName = [NSString stringWithUTF8String:argv[4]];
            NSString *outputName = [NSString stringWithUTF8String:argv[5]];
            if (!modelPath || !inputPath || !outputPath || !inputName || !outputName)
                throw std::runtime_error("Invalid UTF-8 test argument.");
            NSError *error = nil;
            MLModelConfiguration *configuration = [[MLModelConfiguration alloc] init];
            configuration.computeUnits = MLComputeUnitsCPUOnly;
            if (argc == 9) {
                const std::string units = argv[8];
                if (units == "all")
                    configuration.computeUnits = MLComputeUnitsAll;
                else if (units == "cpu-and-gpu")
                    configuration.computeUnits = MLComputeUnitsCPUAndGPU;
                else if (units != "cpu")
                    throw std::runtime_error("Unsupported compute units.");
            }
            MLModel *model = [MLModel modelWithContentsOfURL:[NSURL fileURLWithPath:modelPath]
                                               configuration:configuration
                                                       error:&error];
            if (!model)
                throw std::runtime_error("Cannot load compiled test model.");
            NSData *bytes = [NSData dataWithContentsOfFile:inputPath];
            MLMultiArray *input = [[MLMultiArray alloc] initWithShape:@[ @1, @3, @(size), @(size) ]
                                                             dataType:MLMultiArrayDataTypeFloat32
                                                                error:&error];
            if (!input || !bytes ||
                bytes.length != static_cast<NSUInteger>(3 * size * size) * sizeof(float) ||
                ![input.strides
                    isEqualToArray:@[ @(3 * size * size), @(size * size), @(size), @1 ]])
                throw std::runtime_error("Invalid contiguous input tensor.");
            void *inputStorage = input.dataPointer;
            const void *sourceStorage = bytes.bytes;
            if (!inputStorage || !sourceStorage)
                throw std::runtime_error("Test input storage is unavailable.");
            std::memcpy(inputStorage, sourceStorage, bytes.length);
            MLDictionaryFeatureProvider *features = [[MLDictionaryFeatureProvider alloc]
                initWithDictionary:@{inputName : [MLFeatureValue featureValueWithMultiArray:input]}
                             error:&error];
            if (!features)
                throw std::runtime_error("Cannot create model input.");
            id<MLFeatureProvider> prediction = [model predictionFromFeatures:features error:&error];
            MLMultiArray *output = [prediction featureValueForName:outputName].multiArrayValue;
            if (!output || output.dataType != MLMultiArrayDataTypeFloat32 ||
                ![output.shape isEqualToArray:@[ @1, @(channels), @(size), @(size) ]])
                throw std::runtime_error("Invalid model output contract.");
            NSMutableData *result = [NSMutableData
                dataWithLength:static_cast<NSUInteger>(output.count) * sizeof(float)];
            if (!result)
                throw std::runtime_error("Cannot allocate test output.");
            auto *values = static_cast<float *>(result.mutableBytes);
            if (!values)
                throw std::runtime_error("Test output storage is unavailable.");
            // Indexed access honors Core ML's actual strides, including padding.
            for (NSInteger i = 0; i < output.count; ++i)
                values[i] = output[i].floatValue;
            if (![result writeToFile:outputPath options:NSDataWritingAtomic error:&error])
                throw std::runtime_error("Cannot write test prediction.");
            return 0;
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
}
