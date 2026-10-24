#include "platform/macos/CFHandle.h"
#include "platform/macos/FileHasher.h"
#include "platform/macos/PhotoMetadataIO.h"
#include <ImageIO/ImageIO.h>
#include <QXmlStreamReader>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string packet(const std::string &properties) {
    return "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
           "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
           "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
           "xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\">" +
           properties + "</rdf:Description></rdf:RDF></x:xmpmeta>";
}
luma::DescriptiveMetadata read(const std::filesystem::path &source, const std::string &xml) {
    return luma::macos::readDescriptiveMetadata(
        source, std::span(reinterpret_cast<const std::uint8_t *>(xml.data()), xml.size()));
}
void rejects(const std::filesystem::path &source, const std::string &xml) {
    bool rejected = false;
    try {
        (void)read(source, xml);
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Unsupported or oversized XMP list was accepted.");
}
std::map<std::string, std::string> languageValues(std::span<const std::uint8_t> bytes,
                                                  const QString &space, const QString &property) {
    QXmlStreamReader reader(QByteArray(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
    std::map<std::string, std::string> result;
    int depth = 0, propertyDepth = -1;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            ++depth;
            if (reader.namespaceUri() == space && reader.name() == property) {
                require(propertyDepth == -1, "Nested duplicate property in XML oracle.");
                propertyDepth = depth;
            } else if (propertyDepth > 0 &&
                       reader.namespaceUri() ==
                           QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#") &&
                       reader.name() == QStringLiteral("li")) {
                const auto language = reader.attributes().value(
                    QStringLiteral("http://www.w3.org/XML/1998/namespace"), QStringLiteral("lang"));
                const auto key = language.toString().toStdString();
                const auto text = reader.readElementText().toStdString();
                require(result.emplace(key, text).second, "Duplicate language in XML oracle.");
                --depth;
            }
        } else if (reader.isEndElement()) {
            if (depth == propertyDepth)
                propertyDepth = -1;
            --depth;
        }
    }
    require(!reader.hasError(), "XMP output is not well-formed XML.");
    return result;
}
void preservesUneditedProperties(const std::filesystem::path &source,
                                 const luma::DescriptiveMetadata &values) {
    const auto original = packet(
        "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">Before</rdf:li>"
        "<rdf:li xml:lang=\"de-DE\">Deutscher Titel</rdf:li></rdf:Alt></dc:title>"
        "<qa:Note xmlns:qa=\"https://example.invalid/luma-qa/\">Keep &amp; preserve</qa:Note>"
        "<crs:Exposure2012 xmlns:crs=\"http://ns.adobe.com/camera-raw-settings/1.0/\">"
        "1.25</crs:Exposure2012>"
        "<qa:Text xmlns:qa=\"https://example.invalid/luma-qa/\"><rdf:Alt>"
        "<rdf:li xml:lang=\"x-default\">Foreign default</rdf:li>"
        "<rdf:li xml:lang=\"fr-FR\">Texte conservé</rdf:li></rdf:Alt></qa:Text>");
    const auto output = luma::macos::writeDescriptiveXMP(
        values,
        std::span(reinterpret_cast<const std::uint8_t *>(original.data()), original.size()));
    require(luma::macos::readDescriptiveMetadata(source, output) == values,
            "Updating an existing XMP packet did not replace the supported metadata exactly.");
    // ImageIO 15 itself substitutes x-default while reading alternate languages.
    // Verify serialized RDF through an independent XML parser, not the faulty API.
    require(languageValues(output, QStringLiteral("http://purl.org/dc/elements/1.1/"),
                           QStringLiteral("title")) ==
                std::map<std::string, std::string>{{"x-default", values.title},
                                                   {"de-DE", "Deutscher Titel"}},
            "Writing default metadata changed another serialized language.");
    require(languageValues(output, QStringLiteral("https://example.invalid/luma-qa/"),
                           QStringLiteral("Text")) ==
                std::map<std::string, std::string>{{"x-default", "Foreign default"},
                                                   {"fr-FR", "Texte conservé"}},
            "Writing supported metadata changed a foreign language property.");
    luma::macos::CFHandle data(
        CFDataCreate(nullptr, output.data(), static_cast<CFIndex>(output.size())));
    luma::macos::CFHandle metadata(CGImageMetadataCreateFromXMPData(data.get()));
    require(static_cast<bool>(metadata), "Updated XMP cannot be parsed by ImageIO.");
    for (const auto &[path, expected] : {std::pair{CFSTR("qa:Note"), CFSTR("Keep & preserve")},
                                         std::pair{CFSTR("crs:Exposure2012"), CFSTR("1.25")}}) {
        luma::macos::CFHandle actual(
            CGImageMetadataCopyStringValueWithPath(metadata.get(), nullptr, path));
        require(actual && CFEqual(actual.get(), expected),
                "Writing supported metadata removed an external property or another language.");
    }
}
void alternatePacketForms(const std::filesystem::path &source) {
    luma::DescriptiveMetadata values;
    values.title = "New & <literal>";
    values.caption = "First\r\nSecond\tline";
    values.rating = 4;
    const auto original =
        packet("<dc:title><rdf:Alt><rdf:li xml:lang=\"de-DE\">Unverändert</rdf:li>"
               "</rdf:Alt></dc:title>");
    auto write = [&](const std::string &input) {
        return luma::macos::writeDescriptiveXMP(
            values, std::span(reinterpret_cast<const std::uint8_t *>(input.data()), input.size()));
    };
    for (const bool aliases : {false, true}) {
        auto input = original;
        if (aliases) {
            for (const auto &[from, to] :
                 {std::pair{"dc:", "other:"}, std::pair{"xmlns:dc=", "xmlns:other="},
                  std::pair{"rdf:", "r:"}, std::pair{"xmlns:rdf=", "xmlns:r="}}) {
                std::size_t position = 0;
                while ((position = input.find(from, position)) != std::string::npos) {
                    input.replace(position, std::string_view(from).size(), to);
                    position += std::string_view(to).size();
                }
            }
        }
        const auto output = write(input);
        require(languageValues(output, QStringLiteral("http://purl.org/dc/elements/1.1/"),
                               QStringLiteral("title")) ==
                    std::map<std::string, std::string>{{"x-default", values.title},
                                                       {"de-DE", "Unverändert"}},
                "Adding x-default lost another language or namespace aliases.");
        const auto actual = luma::macos::readDescriptiveMetadata(source, output);
        if (actual != values)
            std::cerr << "aliases=" << aliases << " title=" << actual.title << " caption_hex="
                      << QByteArray::fromStdString(actual.caption).toHex().constData()
                      << " rating=" << actual.rating << "\n"
                      << std::string(output.begin(), output.end()) << "\n";
        require(actual == values,
                "Metadata merge changed literal text, line endings or aliased properties.");
        const auto again = luma::macos::writeDescriptiveXMP(values, output);
        require(languageValues(again, QStringLiteral("http://purl.org/dc/elements/1.1/"),
                               QStringLiteral("title")) ==
                    languageValues(output, QStringLiteral("http://purl.org/dc/elements/1.1/"),
                                   QStringLiteral("title")),
                "Repeated metadata merge changed the language map.");
    }
    const auto wrapped = write("<?xml version='1.0'?><?xpacket begin=''?>" + original +
                               "<!-- outer metadata comment --><?xpacket end='w'?>");
    const std::string wrappedText(wrapped.begin(), wrapped.end());
    require(wrappedText.find("<?xml ") == std::string::npos &&
                wrappedText.find("<?xpacket begin=") != std::string::npos &&
                wrappedText.find("<!-- outer metadata comment -->") != std::string::npos &&
                wrappedText.find("<?xpacket end=") != std::string::npos &&
                luma::macos::readDescriptiveMetadata(source, wrapped) == values,
            "Metadata merge lost packet instructions/comments or emitted an incompatible prolog.");
    for (const auto &input :
         {packet("<dc:title>First</dc:title><dc:title>Second</dc:title>"),
          packet("<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">A</rdf:li>"
                 "<rdf:li xml:lang=\"x-default\">B</rdf:li></rdf:Alt></dc:title>"),
          std::string("<!DOCTYPE x [<!ENTITY remote SYSTEM \"file:///nonexistent-luma-qa\">]>") +
              original,
          std::string("<x:xmpmeta>unclosed")}) {
        bool rejected = false;
        try {
            (void)write(input);
        } catch (const std::exception &) {
            rejected = true;
        }
        require(rejected, "Ambiguous or unsafe XMP packet was rewritten.");
    }
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    try {
        const std::filesystem::path source(argv[1]);
        const auto hash = luma::macos::sha256File(source);
        const auto xml = packet(
            "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">Север &amp; 夜景</rdf:li></rdf:Alt>"
            "</dc:title><dc:creator><rdf:Seq><rdf:li>Անի Սարգսյան</rdf:li>"
            "<rdf:li>山田 太郎</rdf:li><rdf:li>Անի Սարգսյան</rdf:li></rdf:Seq></dc:creator>"
            "<dc:subject><rdf:Bag><rdf:li>ночь</rdf:li><rdf:li>빛</rdf:li>"
            "<rdf:li>ночь</rdf:li><rdf:li>café</rdf:li></rdf:Bag></dc:subject>");
        const auto values = read(source, xml);
        require(
            values.title == "Север & 夜景" &&
                values.creators ==
                    std::vector<std::string>{"Անի Սարգսյան", "山田 太郎", "Անի Սարգսյան"} &&
                values.keywords == std::vector<std::string>{"ночь", "빛", "café"},
            "Native ImageIO arrays lost Unicode, creator sequence/repeats or keyword uniqueness.");
        for (const std::string &prefix :
             {std::string("\n \t\r"), std::string("\xEF\xBB\xBF\n"),
              std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"),
              std::string("\xEF\xBB\xBF<?xml version='1.0'?>\n<!-- preserved comment -->"),
              std::string("<?xpacket begin='' id='W5M0MpCehiHzreSzNTczkc9d'?>")})
            require(
                read(source, prefix + xml) == values,
                "Legal XML declaration, whitespace, BOM or processing instruction was rejected.");
        const auto written = luma::macos::writeDescriptiveXMP(values, {});
        require(luma::macos::readDescriptiveMetadata(source, written) == values,
                "Written XMP lists cannot be read back exactly.");
        for (const auto *noncharacter : {"\xef\xbf\xbe", "\xef\xbf\xbf"}) {
            auto invalid = values;
            invalid.title = noncharacter;
            invalid.validate(); // Valid UTF-8, but XML 1.0 cannot encode this scalar.
            bool rejected = false;
            try {
                (void)luma::macos::writeDescriptiveXMP(invalid, {});
            } catch (const std::exception &) {
                rejected = true;
            }
            require(rejected, "New XMP accepted an XML-forbidden Unicode scalar.");
        }
        preservesUneditedProperties(source, values);
        alternatePacketForms(source);
        for (const auto &property : {"creator", "subject"}) {
            const std::string prefix = std::string("<dc:") + property + "><rdf:Seq>";
            const std::string suffix = std::string("</rdf:Seq></dc:") + property + ">";
            rejects(source,
                    packet(prefix +
                           "<rdf:li rdf:parseType=\"Resource\"><xmp:Label>nested</xmp:Label>"
                           "</rdf:li>" +
                           suffix));
            rejects(source, packet(prefix +
                                   "<rdf:li><rdf:Seq><rdf:li>nested</rdf:li>"
                                   "</rdf:Seq></rdf:li>" +
                                   suffix));
            // ImageIO normalizes a scalar spelling of these known list properties.
            const auto scalar = read(
                source, packet(std::string("<dc:") + property + ">scalar</dc:" + property + ">"));
            const auto &normalized =
                std::string_view(property) == "creator" ? scalar.creators : scalar.keywords;
            require(normalized == std::vector<std::string>{"scalar"},
                    "ImageIO-normalized single-element XMP list lost its text.");
            std::string items;
            for (int index = 0; index < 257; ++index)
                items += "<rdf:li>entry-" + std::to_string(index) + "</rdf:li>";
            rejects(source, packet(prefix + items + suffix));
        }
        require(luma::macos::sha256File(source) == hash, "Metadata reads rewrote the original.");
        std::cout << "Native XMP lists: Unicode/order/duplicates, roundtrip and strict "
                     "structure/size rejection passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
