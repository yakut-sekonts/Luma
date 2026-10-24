#include "platform/macos/PhotoMetadataIO.h"
#include "platform/macos/CFHandle.h"
#import <Foundation/Foundation.h>
#include <ImageIO/ImageIO.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace luma::macos {
namespace {
NSString *xmlText(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > 8 * 1024 * 1024)
        throw std::runtime_error("XMP must fit the 8 MiB limit.");
    NSString *xml = [[NSString alloc] initWithBytes:bytes.data()
                                             length:bytes.size()
                                           encoding:NSUTF8StringEncoding];
    if (!xml ||
        [xml rangeOfString:@"<!DOCTYPE" options:NSCaseInsensitiveSearch].location != NSNotFound ||
        [xml rangeOfString:@"<!ENTITY" options:NSCaseInsensitiveSearch].location != NSNotFound)
        throw std::runtime_error("XMP must be UTF-8 without DTD or entity declarations.");
    return xml;
}
std::string string(CFStringRef value) {
    const char *utf8 = value ? [(__bridge NSString *)value UTF8String] : nullptr;
    return utf8 ? utf8 : "";
}
std::string scalar(CGImageMetadataRef data, CFStringRef path) {
    CFHandle value(CGImageMetadataCopyStringValueWithPath(data, nullptr, path));
    return string(value.get());
}
bool exists(CGImageMetadataRef data, CFStringRef path) {
    CFHandle tag(CGImageMetadataCopyTagWithPath(data, nullptr, path));
    return static_cast<bool>(tag);
}
std::vector<std::string> array(CGImageMetadataRef data, CFStringRef path, bool unique = true) {
    CFHandle tag(CGImageMetadataCopyTagWithPath(data, nullptr, path));
    if (!tag)
        return {};
    CFHandle value(CGImageMetadataTagCopyValue(tag.get()));
    if (!value || CFGetTypeID(value.get()) != CFArrayGetTypeID())
        throw std::runtime_error("XMP list property must be an array.");
    auto list = static_cast<CFArrayRef>(value.get());
    if (CFArrayGetCount(list) > 256)
        throw std::runtime_error("XMP list exceeds its item limit.");
    std::vector<std::string> result;
    for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
        auto item = CFArrayGetValueAtIndex(list, i);
        CFHandle<CFTypeRef> unwrapped;
        // ImageIO can wrap textual array members in metadata tags.
        if (item && CFGetTypeID(item) == CGImageMetadataTagGetTypeID()) {
            const auto member = static_cast<CGImageMetadataTagRef>(const_cast<void *>(item));
            if (CGImageMetadataTagGetType(member) != kCGImageMetadataTypeString)
                throw std::runtime_error("XMP list must contain text.");
            unwrapped = CFHandle(CGImageMetadataTagCopyValue(member));
            item = unwrapped.get();
        }
        if (!item || CFGetTypeID(item) != CFStringGetTypeID())
            throw std::runtime_error("XMP list must contain text.");
        auto text = string(static_cast<CFStringRef>(item));
        if (!text.empty() &&
            (!unique || std::find(result.begin(), result.end(), text) == result.end()))
            result.push_back(std::move(text));
    }
    return result;
}
void overlay(CGImageMetadataRef data, DescriptiveMetadata &values) {
    if (!data)
        return;
    for (const auto &[path, destination] : {std::pair{CFSTR("dc:title"), &values.title},
                                            {CFSTR("dc:description"), &values.caption},
                                            {CFSTR("dc:rights"), &values.copyright},
                                            {CFSTR("Iptc4xmpCore:Location"), &values.location}})
        if (exists(data, path))
            *destination = scalar(data, path);
    if (exists(data, CFSTR("dc:creator")))
        values.creators = array(data, CFSTR("dc:creator"), false);
    if (exists(data, CFSTR("dc:subject")))
        values.keywords = array(data, CFSTR("dc:subject"));
    if (exists(data, CFSTR("xmp:Rating"))) {
        std::istringstream number(scalar(data, CFSTR("xmp:Rating")));
        number.imbue(std::locale::classic());
        double rating = 0;
        if (!(number >> rating) || !(number >> std::ws).eof() || !std::isfinite(rating) ||
            rating < -1 || rating > 5)
            throw std::runtime_error("Invalid XMP rating.");
        // Accept integral real spellings such as 3.0 without rounding fractions.
        if (std::trunc(rating) != rating)
            throw std::runtime_error(
                "Fractional XMP ratings cannot be represented by whole-star Library ratings.");
        values.flag = rating == -1 ? -1 : 0;
        values.rating = rating == -1 ? 0 : static_cast<int>(rating);
    }
    if (exists(data, CFSTR("xmp:Label")))
        values.label = scalar(data, CFSTR("xmp:Label"));
}
std::string escaped(std::string_view value) {
    std::string out;
    for (char c : value) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '"')
            out += "&quot;";
        else if (c == '\r')
            out += "&#13;";
        else
            out += c;
    }
    return out;
}
NSXMLDocument *xmlDocument(std::span<const std::uint8_t> bytes) {
    NSError *error = nil;
    auto *document = [[NSXMLDocument alloc]
        initWithXMLString:xmlText(bytes)
                  options:NSXMLNodeLoadExternalEntitiesNever | NSXMLNodePreservePrefixes |
                          NSXMLNodePreserveWhitespace
                    error:&error];
    if (!document || error || document.DTD)
        throw std::runtime_error("Invalid or unsupported XMP packet.");
    return document;
}
NSString *serializedText(NSXMLNode *node) {
    // XML normalizes literal CR/CRLF. Preserve character references and newly
    // edited CR characters when serializing parsed text.
    return [node.XMLString stringByReplacingOccurrencesOfString:@"\r" withString:@"&#13;"];
}
CFHandle<CGImageMetadataRef> parse(std::span<const std::uint8_t> bytes) {
    auto *document = xmlDocument(bytes);
    // ImageIO 15 rejects a legal XML declaration and leading whitespace/BOM.
    // Parse securely first and hand it only the validated document element;
    // declarations, comments and processing instructions are not RDF properties.
    NSData *data = [serializedText(document.rootElement) dataUsingEncoding:NSUTF8StringEncoding];
    CFHandle result(data ? CGImageMetadataCreateFromXMPData((__bridge CFDataRef)data) : nullptr);
    if (!result)
        throw std::runtime_error("Invalid or unsupported XMP packet.");
    return result;
}
NSArray<NSXMLElement *> *descriptions(NSXMLDocument *document) {
    constexpr auto rdf = @"http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    auto *root = document.rootElement;
    if (!([root.localName isEqualToString:@"RDF"] && [root.URI isEqualToString:rdf]) &&
        !([root.localName isEqualToString:@"xmpmeta"] &&
          [root.URI isEqualToString:@"adobe:ns:meta/"]))
        throw std::runtime_error("Unsupported XMP document root.");
    NSArray<NSXMLElement *> *roots =
        [root.localName isEqualToString:@"RDF"] && [root.URI isEqualToString:rdf]
            ? @[ root ]
            : [root elementsForLocalName:@"RDF" URI:rdf];
    if (roots.count != 1)
        throw std::runtime_error("XMP must contain one RDF metadata root.");
    auto *result = [roots.firstObject elementsForLocalName:@"Description" URI:rdf];
    if (result.count == 0)
        throw std::runtime_error("XMP has no RDF description.");
    NSString *subject = [result.firstObject attributeForLocalName:@"about" URI:rdf].stringValue;
    if (!subject)
        subject = @"";
    for (NSXMLElement *description in result) {
        if ([description attributeForLocalName:@"ID" URI:rdf] ||
            [description attributeForLocalName:@"nodeID" URI:rdf])
            throw std::runtime_error("Named RDF nodes cannot be edited safely.");
        NSString *other = [description attributeForLocalName:@"about" URI:rdf].stringValue;
        if (!other)
            other = @"";
        if (![subject isEqualToString:other])
            throw std::runtime_error("XMP with multiple RDF subjects cannot be edited safely.");
    }
    return result;
}
void replaceProperty(NSArray<NSXMLElement *> *descriptions, NSXMLElement *update) {
    NSString *name = update.localName;
    NSString *space = update.URI;
    if (!name || !space)
        throw std::runtime_error("XMP update is missing its expanded property name.");
    NSXMLNode *previous = nil;
    NSXMLElement *owner = descriptions.firstObject;
    for (NSXMLElement *description in descriptions) {
        NSMutableArray<NSXMLNode *> *matches =
            [[description elementsForLocalName:name URI:space] mutableCopy];
        if (auto *attribute = [description attributeForLocalName:name URI:space])
            [matches addObject:attribute];
        for (NSXMLNode *match in matches) {
            if (previous)
                throw std::runtime_error("Duplicate XMP properties cannot be edited safely.");
            previous = match;
            owner = description;
        }
    }
    NSXMLElement *replacement = [update copy];
    // Give new properties their own namespace bindings: an existing packet may
    // use other prefixes, or bind our usual prefixes to unrelated namespaces.
    for (NSXMLNode *binding in [(NSXMLElement *)update.parent namespaces])
        [replacement addNamespace:[binding copy]];
    constexpr auto rdf = @"http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    [replacement addNamespace:[NSXMLNode namespaceWithName:@"rdf" stringValue:rdf]];
    auto *newAlternatives = [replacement elementsForLocalName:@"Alt" URI:rdf].firstObject;
    if (newAlternatives && previous.kind == NSXMLElementKind) {
        auto *alternatives = [(NSXMLElement *)previous elementsForLocalName:@"Alt" URI:rdf];
        if (alternatives.count > 1)
            throw std::runtime_error("Ambiguous localized XMP property.");
        if (alternatives.count == 1) {
            NSXMLElement *preserved = [previous copy];
            auto *oldAlternatives = [preserved elementsForLocalName:@"Alt" URI:rdf].firstObject;
            NSXMLElement *defaultText = nil;
            for (NSXMLElement *item in [oldAlternatives elementsForLocalName:@"li" URI:rdf]) {
                auto *language =
                    [item attributeForLocalName:@"lang"
                                            URI:@"http://www.w3.org/XML/1998/namespace"];
                if ([language.stringValue isEqualToString:@"x-default"]) {
                    if (defaultText)
                        throw std::runtime_error("Duplicate default XMP language.");
                    defaultText = item;
                }
            }
            auto *updatedDefault = [newAlternatives elementsForLocalName:@"li" URI:rdf].firstObject;
            if (defaultText)
                defaultText.stringValue = updatedDefault.stringValue;
            else {
                NSXMLElement *added = [updatedDefault copy];
                [added addNamespace:[NSXMLNode namespaceWithName:@"rdf" stringValue:rdf]];
                [oldAlternatives insertChild:added atIndex:0];
            }
            replacement = preserved;
        }
    }
    if (previous.kind == NSXMLAttributeKind) {
        NSString *attributeName = previous.name;
        if (!attributeName)
            throw std::runtime_error("XMP attribute has no qualified name.");
        [owner removeAttributeForName:attributeName];
        [owner addChild:replacement];
    } else if (previous) {
        const auto index = [owner.children indexOfObjectIdenticalTo:previous];
        [owner replaceChildAtIndex:index withNode:replacement];
    } else
        [owner addChild:replacement];
}
std::vector<std::uint8_t> mergeXMP(const std::string &updates,
                                   std::span<const std::uint8_t> existing) {
    // Valid UTF-8 can still contain scalars forbidden by XML 1.0. Validate the
    // generated packet before returning bytes for a new sidecar as well.
    auto *source = xmlDocument(
        std::span(reinterpret_cast<const std::uint8_t *>(updates.data()), updates.size()));
    if (existing.empty())
        return {updates.begin(), updates.end()};
    auto *destination = xmlDocument(existing);
    auto *targets = descriptions(destination);
    // Edit only supported expanded names. Preserve every other XML subtree,
    // including foreign language variants that ImageIO 15 changes during parsing.
    for (NSXMLNode *property in descriptions(source).firstObject.children)
        if (property.kind == NSXMLElementKind)
            replaceProperty(targets, (NSXMLElement *)property);
    // Serializing the document would synthesize an XML declaration that
    // ImageIO 15 rejects. Its children retain the root, comments and original
    // processing instructions without adding that incompatible prolog.
    NSMutableString *serialized = [NSMutableString string];
    for (NSXMLNode *child in destination.children)
        [serialized appendString:serializedText(child)];
    NSData *output = [serialized dataUsingEncoding:NSUTF8StringEncoding];
    if (!output || output.length == 0 || output.length > 8 * 1024 * 1024)
        throw std::runtime_error("Cannot serialize XMP within its size limit.");
    auto *begin = static_cast<const std::uint8_t *>(output.bytes);
    return {begin, begin + output.length};
}
} // namespace
DescriptiveMetadata readDescriptiveMetadata(const std::filesystem::path &source,
                                            std::span<const std::uint8_t> sidecar) {
    @autoreleasepool {
        CFHandle url(CFURLCreateFromFileSystemRepresentation(
            nullptr, reinterpret_cast<const UInt8 *>(source.c_str()),
            static_cast<CFIndex>(source.string().size()), false));
        CFHandle image(CGImageSourceCreateWithURL(url.get(), nullptr));
        if (!image)
            throw std::runtime_error("ImageIO cannot read original metadata.");
        CFHandle embedded(CGImageSourceCopyMetadataAtIndex(
            image.get(), CGImageSourceGetPrimaryImageIndex(image.get()), nullptr));
        DescriptiveMetadata result;
        overlay(embedded.get(), result);
        if (!sidecar.empty()) {
            auto data = parse(sidecar);
            overlay(data.get(), result);
        }
        result.validate();
        return result;
    }
}
std::vector<std::uint8_t> writeDescriptiveXMP(const DescriptiveMetadata &values,
                                              std::span<const std::uint8_t> existing) {
    @autoreleasepool {
        values.validate();
        std::ostringstream xml;
        xml.imbue(std::locale::classic());
        xml << "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
               "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
               "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
               "xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" "
               "xmlns:Iptc4xmpCore=\"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/\">"
               "<xmp:Rating>"
            << values.xmpRating() << "</xmp:Rating><xmp:Label>" << escaped(values.label)
            << "</xmp:Label><Iptc4xmpCore:Location>" << escaped(values.location)
            << "</Iptc4xmpCore:Location>";
        auto localized = [&](const char *name, const std::string &text) {
            xml << "<dc:" << name << "><rdf:Alt><rdf:li xml:lang=\"x-default\">" << escaped(text)
                << "</rdf:li></rdf:Alt></dc:" << name << ">";
        };
        localized("title", values.title);
        localized("description", values.caption);
        localized("rights", values.copyright);
        xml << "<dc:creator><rdf:Seq>";
        for (const auto &creator : values.creators)
            xml << "<rdf:li>" << escaped(creator) << "</rdf:li>";
        xml << "</rdf:Seq></dc:creator><dc:subject><rdf:Bag>";
        for (const auto &keyword : values.keywords)
            xml << "<rdf:li>" << escaped(keyword) << "</rdf:li>";
        xml << "</rdf:Bag></dc:subject></rdf:Description></rdf:RDF></x:xmpmeta>";
        return mergeXMP(xml.str(), existing);
    }
}
CFHandle<CGMutableImageMetadataRef> applyMetadataOverlay(CGImageMetadataRef source,
                                                         const MetadataOverlay &overlay) {
    @autoreleasepool {
        overlay.values.validate();
        if (overlay.includeText) {
            CFHandle<CFDataRef> packet(source ? CGImageMetadataCreateXMPData(source, nullptr)
                                              : nullptr);
            const auto bytes =
                packet ? std::span(CFDataGetBytePtr(packet.get()),
                                   static_cast<std::size_t>(CFDataGetLength(packet.get())))
                       : std::span<const std::uint8_t>{};
            const auto rewritten = writeDescriptiveXMP(overlay.values, bytes);
            auto parsed = parse(rewritten);
            return CFHandle<CGMutableImageMetadataRef>(
                CGImageMetadataCreateMutableCopy(parsed.get()));
        }
        CFHandle result(source ? CGImageMetadataCreateMutableCopy(source)
                               : CGImageMetadataCreateMutable());
        NSString *rating = [NSString stringWithFormat:@"%d", overlay.values.xmpRating()];
        NSString *label = [NSString stringWithUTF8String:overlay.values.label.c_str()];
        if (!result ||
            !CGImageMetadataRegisterNamespaceForPrefix(result.get(),
                                                       kCGImageMetadataNamespaceXMPBasic,
                                                       kCGImageMetadataPrefixXMPBasic, nullptr) ||
            !CGImageMetadataSetValueWithPath(result.get(), nullptr, CFSTR("xmp:Rating"),
                                             (__bridge CFStringRef)rating) ||
            !CGImageMetadataSetValueWithPath(result.get(), nullptr, CFSTR("xmp:Label"),
                                             (__bridge CFStringRef)label))
            throw std::runtime_error("Cannot apply catalog metadata to export.");
        return result;
    }
}
} // namespace luma::macos
