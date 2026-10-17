#include "apps/shared/ExportSettings.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>

namespace luma {
namespace {
int integer(const QJsonObject &object, const char *key) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
        value.toDouble() != std::floor(value.toDouble()) || std::abs(value.toDouble()) > 100000)
        throw std::runtime_error("Invalid integer export setting.");
    return value.toInt();
}
double number(const QJsonObject &object, const char *key) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        throw std::runtime_error("Invalid numeric export setting.");
    return value.toDouble();
}
QString text(const QJsonObject &object, const char *key) {
    const auto value = object.value(QLatin1String(key));
    if (!value.isString() || value.toString().contains(QChar(0)))
        throw std::runtime_error("Invalid text export setting.");
    return value.toString();
}
} // namespace
void ExportSettings::validate() const {
    options.validate();
    if (destination.isEmpty() || !QFileInfo(destination).isAbsolute() ||
        destination.contains(QChar(0)))
        throw std::runtime_error("Choose an absolute export folder.");
    if (conflict < ExportConflict::skip || conflict > ExportConflict::replace)
        throw std::runtime_error("Invalid export conflict policy.");
    if (naming.isEmpty() || naming.size() > 120 ||
        naming.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f/:\\\\]"))))
        throw std::runtime_error("Invalid export filename template.");
    auto remainder = naming;
    for (const auto &token :
         {QStringLiteral("{filename}"), QStringLiteral("{sequence}"), QStringLiteral("{date}")})
        remainder.replace(token, QString());
    if (remainder.contains('{') || remainder.contains('}'))
        throw std::runtime_error("Unknown export filename token.");
}
QString ExportSettings::filename(const QString &source, int sequence, const QString &date) const {
    // Parse the template once: replacement values are literal filename data.
    static const QRegularExpression tokens(QStringLiteral("\\{(filename|sequence|date)\\}"));
    auto matches = tokens.globalMatch(naming);
    QString result;
    qsizetype cursor = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += naming.sliced(cursor, match.capturedStart() - cursor);
        const auto token = match.captured(1);
        if (token == QStringLiteral("filename"))
            result += QFileInfo(source).completeBaseName();
        else if (token == QStringLiteral("sequence"))
            result += QStringLiteral("%1").arg(sequence, 4, 10, QChar('0'));
        else
            result += date;
        cursor = match.capturedEnd();
    }
    result += naming.sliced(cursor);
    if (result.isEmpty() || result == QStringLiteral(".") || result == QStringLiteral("..") ||
        result.toUtf8().size() > 230 ||
        result.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f/:\\\\]"))))
        throw std::runtime_error("Generated export filename is invalid or too long.");
    return result + QString::fromStdString(options.extension());
}
QJsonObject ExportSettings::json() const {
    return {{"format", int(options.format)},
            {"quality", options.quality},
            {"colorSpace", int(options.colorSpace)},
            {"compression", int(options.compression)},
            {"resize", int(options.resize)},
            {"resizeValue", options.resizeValue},
            {"outputMedium", int(options.outputMedium)},
            {"outputAmount", int(options.outputAmount)},
            {"metadata", int(options.metadata)},
            {"watermarkKind", int(options.watermark.kind)},
            {"watermarkText", QString::fromStdString(options.watermark.text)},
            {"watermarkImage", QString::fromStdString(options.watermark.image.string())},
            {"watermarkOpacity", options.watermark.opacity},
            {"watermarkSize", options.watermark.size},
            {"watermarkMargin", options.watermark.margin},
            {"watermarkAnchor", options.watermark.anchor},
            {"destination", destination},
            {"naming", naming},
            {"conflict", int(conflict)}};
}
ExportSettings ExportSettings::fromJson(const QJsonObject &object) {
    ExportSettings result;
    const auto expected = result.json();
    if (object.size() != expected.size())
        throw std::runtime_error("Incomplete export settings.");
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!expected.contains(it.key()))
            throw std::runtime_error("Unknown export setting.");
    auto &o = result.options;
    o.format = static_cast<ExportFormat>(integer(object, "format"));
    o.quality = integer(object, "quality");
    o.colorSpace = static_cast<ExportColorSpace>(integer(object, "colorSpace"));
    o.compression = static_cast<TiffCompression>(integer(object, "compression"));
    o.resize = static_cast<ResizeMode>(integer(object, "resize"));
    o.resizeValue = number(object, "resizeValue");
    o.outputMedium = static_cast<OutputMedium>(integer(object, "outputMedium"));
    o.outputAmount = static_cast<SharpenAmount>(integer(object, "outputAmount"));
    o.metadata = static_cast<MetadataPolicy>(integer(object, "metadata"));
    o.watermark.kind = static_cast<WatermarkKind>(integer(object, "watermarkKind"));
    o.watermark.text = text(object, "watermarkText").toStdString();
    o.watermark.image = text(object, "watermarkImage").toStdString();
    o.watermark.opacity = number(object, "watermarkOpacity");
    o.watermark.size = number(object, "watermarkSize");
    o.watermark.margin = number(object, "watermarkMargin");
    o.watermark.anchor = integer(object, "watermarkAnchor");
    result.destination = text(object, "destination");
    result.naming = text(object, "naming");
    result.conflict = static_cast<ExportConflict>(integer(object, "conflict"));
    result.validate();
    return result;
}
} // namespace luma
