#include "platform/macos/ExportMetadata.h"
#include "platform/macos/PhotoMetadataIO.h"
#import <Foundation/Foundation.h>
#include <set>
#include <stdexcept>

namespace luma::macos {
namespace {
std::string lower(CFStringRef value) {
    if (!value)
        return {};
    NSString *string = (__bridge NSString *)value;
    const char *bytes = [[string lowercaseString] UTF8String];
    return bytes ? std::string(bytes) : std::string();
}
std::string tagName(CGImageMetadataTagRef tag) {
    CFHandle name(CGImageMetadataTagCopyName(tag));
    return lower(name.get());
}
std::string tagNamespace(CGImageMetadataTagRef tag) {
    CFHandle name(CGImageMetadataTagCopyNamespace(tag));
    return lower(name.get());
}
bool attributionPath(CFStringRef path, CFArrayRef roots) {
    const auto value = lower(path);
    for (CFIndex i = 0; i < CFArrayGetCount(roots); ++i) {
        const auto root = lower(static_cast<CFStringRef>(CFArrayGetValueAtIndex(roots, i)));
        if (value == root || value.starts_with(root + "[") || value.starts_with(root + "/") ||
            value.starts_with(root + "?"))
            return true;
    }
    return false;
}
bool locationTag(CGImageMetadataTagRef tag, CFStringRef path) {
    const auto name = tagName(tag), location = lower(path);
    static const std::set<std::string> names{"city",          "country",         "countrycode",
                                             "countryname",   "province",        "state",
                                             "provincestate", "sublocation",     "worldregion",
                                             "location",      "locationcreated", "locationshown"};
    return name.starts_with("gps") || name.starts_with("latitude") ||
           name.starts_with("longitude") || name.starts_with("altitude") || names.contains(name) ||
           location.find(":locationcreated") != std::string::npos ||
           location.find(":locationshown") != std::string::npos;
}
bool attributionTag(CGImageMetadataTagRef tag) {
    const auto name = tagName(tag), ns = tagNamespace(tag);
    if (ns == "http://purl.org/dc/elements/1.1/")
        return name == "creator" || name == "rights";
    if (ns == "http://ns.adobe.com/xap/1.0/rights/")
        return true;
    if (ns == "http://ns.adobe.com/photoshop/1.0/")
        return name == "credit" || name == "source" || name == "authorsposition";
    return name == "copyright" || name == "artist" || name == "copyrightnotice";
}
void set(CGMutableImageMetadataRef metadata, CFStringRef dictionary, CFStringRef property,
         int value) {
    CFHandle number(CFNumberCreate(nullptr, kCFNumberIntType, &value));
    if (!number ||
        !CGImageMetadataSetValueMatchingImageProperty(metadata, dictionary, property, number.get()))
        throw std::runtime_error("Cannot normalize exported image metadata.");
}
} // namespace
CFHandle<CGMutableImageMetadataRef> exportMetadata(const std::filesystem::path &source,
                                                   const ExportOptions &options, int width,
                                                   int height,
                                                   const std::optional<MetadataOverlay> &overlay) {
    CFHandle<CGMutableImageMetadataRef> result(CGImageMetadataCreateMutable());
    if (!result)
        throw std::runtime_error("Cannot allocate export metadata.");
    if (options.metadata == MetadataPolicy::none)
        return result;
    if (source.empty())
        throw std::runtime_error("Metadata export requires the verified original path.");
    CFHandle url(CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8 *>(source.c_str()),
        static_cast<CFIndex>(source.string().size()), false));
    CFHandle input(url ? CGImageSourceCreateWithURL(url.get(), nullptr) : nullptr);
    if (!input || CGImageSourceGetCount(input.get()) == 0)
        throw std::runtime_error("ImageIO cannot read source metadata.");
    CFHandle metadata(CGImageSourceCopyMetadataAtIndex(
        input.get(), CGImageSourceGetPrimaryImageIndex(input.get()), nullptr));
    if (overlay) {
        auto overridden = applyMetadataOverlay(metadata.get(), *overlay);
        metadata = CFHandle<CGImageMetadataRef>(
            overridden ? static_cast<CGImageMetadataRef>(CFRetain(overridden.get())) : nullptr);
    }
    if (metadata) {
        result =
            CFHandle<CGMutableImageMetadataRef>(CGImageMetadataCreateMutableCopy(metadata.get()));
        if (!result)
            throw std::runtime_error("Cannot copy source metadata.");
        CFHandle<CFMutableArrayRef> removals(
            CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks));
        if (!removals)
            throw std::runtime_error("Cannot allocate metadata filter.");
        CFHandle<CFMutableArrayRef> attribution(
            CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks));
        if (!attribution)
            throw std::runtime_error("Cannot allocate copyright filter.");
        auto roots = attribution.get();
        CGImageMetadataEnumerateTagsUsingBlock(metadata.get(), nullptr, nullptr,
                                               ^bool(CFStringRef path, CGImageMetadataTagRef tag) {
                                                 if (attributionTag(tag))
                                                     CFArrayAppendValue(roots, path);
                                                 return true;
                                               });
        auto paths = removals.get();
        const auto policy = options.metadata;
        NSDictionary *recursive =
            @{(__bridge NSString *)kCGImageMetadataEnumerateRecursively : @YES};
        CGImageMetadataEnumerateTagsUsingBlock(
            metadata.get(), nullptr, (__bridge CFDictionaryRef)recursive,
            ^bool(CFStringRef path, CGImageMetadataTagRef tag) {
              const auto name = tagName(tag);
              static const std::set<std::string> encodingTags{"imagewidth",
                                                              "imagelength",
                                                              "bitspersample",
                                                              "compression",
                                                              "photometricinterpretation",
                                                              "samplesperpixel",
                                                              "planarconfiguration",
                                                              "stripoffsets",
                                                              "stripbytecounts",
                                                              "rowsperstrip",
                                                              "tilewidth",
                                                              "tilelength",
                                                              "tileoffsets",
                                                              "tilebytecounts",
                                                              "ycbcrsubsampling",
                                                              "ycbcrpositioning",
                                                              "cfapattern",
                                                              "blacklevel",
                                                              "whitelevel"};
              const bool rawOnly =
                  encodingTags.contains(name) || name == "makernote" || name == "makernotes" ||
                  name == "thumbnails" || name == "thumbnail" || name == "previewimage" ||
                  name == "thumbnailimage" || name == "jpeginterchangeformat" ||
                  name == "jpeginterchangeformatlength" ||
                  tagNamespace(tag) == "http://ns.adobe.com/camera-raw-settings/1.0/" ||
                  lower(path) == "dc:format";
              const bool remove =
                  rawOnly ||
                  (policy == MetadataPolicy::copyrightOnly && !attributionPath(path, roots)) ||
                  (policy == MetadataPolicy::removeLocation && locationTag(tag, path));
              if (remove)
                  CFArrayAppendValue(paths, path);
              return true;
            });
        std::vector<std::string> pathNames;
        std::set<std::string> filteredPaths;
        for (CFIndex i = 0; i < CFArrayGetCount(paths); ++i) {
            auto path = static_cast<CFStringRef>(CFArrayGetValueAtIndex(paths, i));
            const char *name = [(__bridge NSString *)path UTF8String];
            if (!name)
                throw std::runtime_error("Cannot read export metadata path.");
            pathNames.emplace_back(name);
            filteredPaths.emplace(name);
        }
        // Remove an entire filtered branch once. Removing its array members or
        // qualifiers first can invalidate ImageIO's parent storage (notably
        // localized dc:title/dc:description values). Keep reverse order for
        // independent sibling array members, whose indices can otherwise shift.
        for (CFIndex i = CFArrayGetCount(paths); i > 0; --i) {
            const auto &name = pathNames[static_cast<std::size_t>(i - 1)];
            bool parentFiltered = false;
            for (auto boundary = name.find_first_of("[/?"); boundary != std::string::npos;
                 boundary = name.find_first_of("[/?", boundary + 1)) {
                if (filteredPaths.contains(name.substr(0, boundary))) {
                    parentFiltered = true;
                    break;
                }
            }
            if (!parentFiltered &&
                !CGImageMetadataRemoveTagWithPath(
                    result.get(), nullptr,
                    static_cast<CFStringRef>(CFArrayGetValueAtIndex(paths, i - 1))))
                throw std::runtime_error("Cannot remove private export metadata.");
        }
    }
    if (options.metadata != MetadataPolicy::copyrightOnly) {
        set(result.get(), kCGImagePropertyTIFFDictionary, kCGImagePropertyTIFFOrientation, 1);
        set(result.get(), kCGImagePropertyExifDictionary, kCGImagePropertyExifPixelXDimension,
            width);
        set(result.get(), kCGImagePropertyExifDictionary, kCGImagePropertyExifPixelYDimension,
            height);
        set(result.get(), kCGImagePropertyExifDictionary, kCGImagePropertyExifColorSpace,
            options.format != ExportFormat::dng && options.colorSpace == ExportColorSpace::srgb
                ? 1
                : 65535);
    }
    return result;
}
std::vector<std::uint8_t> metadataXMP(CGImageMetadataRef metadata, MetadataPolicy policy) {
    if (policy == MetadataPolicy::none)
        return {};
    CFHandle bytes(CGImageMetadataCreateXMPData(metadata, nullptr));
    if (!bytes)
        return {};
    if (CFDataGetLength(bytes.get()) > 16 * 1024 * 1024)
        throw std::runtime_error("Export metadata exceeds the 16 MB limit.");
    return {CFDataGetBytePtr(bytes.get()),
            CFDataGetBytePtr(bytes.get()) + CFDataGetLength(bytes.get())};
}
} // namespace luma::macos
