#include "apps/shared/PresetStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace luma {
namespace {
constexpr qint64 maxBytes = 1'048'576;
constexpr std::size_t maxPresets = 512;
const QStringList &groupNames() {
    static const QStringList names{"Light",    "Color",       "Curve",         "HSL",
                                   "Detail",   "Crop",        "Optics",        "Geometry",
                                   "Presence", "Point Color", "Color Grading", "Effects"};
    return names;
}
QStringList parameters(int group) {
    if (group >= 8 && group < 12) {
        QStringList names;
        for (const auto &p : creativeParameters)
            if (p.group == (1U << group))
                names.append(QString::fromUtf8(p.name.data(), p.name.size()));
        return names;
    }
    switch (group) {
    case 0:
        return {"exposure", "contrast", "highlights", "shadows", "whites", "blacks"};
    case 1:
        return {"temperature", "tint", "vibrance", "saturation"};
    case 2:
        return {"curve.shadows", "curve.darks", "curve.lights", "curve.highlights"};
    case 3: {
        QStringList names;
        for (const auto band : hslBands)
            for (const auto *parameter : {"hue", "saturation", "luminance"})
                names.append(QString::fromStdString("hsl." + std::string(band) + "." + parameter));
        return names;
    }
    case 4:
        return {"sharpen.amount",          "sharpen.radius",  "sharpen.detail",
                "sharpen.masking",         "noise.luminance", "noise.luminanceDetail",
                "noise.luminanceContrast", "noise.color",     "noise.colorDetail",
                "noise.colorSmoothness"};
    case 5:
        return {"crop.x",           "crop.y",      "crop.width",        "crop.height",
                "crop.angle",       "crop.aspect", "crop.quarterTurns", "crop.flipHorizontal",
                "crop.flipVertical"};
    case 6:
        return {"lens.distortion", "lens.vignetting", "lens.chromaticAberration"};
    case 7: {
        QStringList names;
        for (auto name : transformParameters)
            names.append(QString::fromUtf8(name));
        return names;
    }
    default:
        throw std::runtime_error("Unsupported preset settings group.");
    }
}
void require(bool valid, const char *message) {
    if (!valid)
        throw std::runtime_error(message);
}
void keys(const QJsonObject &object, QStringList expected) {
    expected.sort();
    require(object.keys() == expected, "Preset has missing or unsupported fields.");
}
QJsonObject object(const QJsonValue &value) {
    require(value.isObject(), "Expected a JSON object in preset.");
    return value.toObject();
}
double number(const QJsonValue &value) {
    require(value.isDouble() && std::isfinite(value.toDouble()),
            "Expected a finite preset number.");
    return value.toDouble();
}
int header(const QJsonObject &root, const QString &format) {
    require(root.value("format").isString() && root.value("format").toString() == format,
            "Unsupported preset format.");
    const auto version = number(root.value("schemaVersion"));
    require(version == 1 || version == 2 || version == 3 || version == 4 || version == 5 ||
                version == 6 || version == 7 || version == 8 || version == 9,
            "Unsupported preset schema version.");
    return static_cast<int>(version);
}
QJsonObject toJson(const DevelopPreset &preset) {
    preset.validate();
    QJsonObject settings;
    for (int group = 0; group < 12; ++group) {
        const auto bit = std::uint32_t{1} << group;
        if (!(preset.groups & bit))
            continue;
        QJsonObject values;
        for (const auto &name : parameters(group))
            values.insert(name, preset.settings.parameter(name.toStdString()));
        values.insert("enabledCorrections", static_cast<int>((preset.settings.enabledCorrections |
                                                              preset.settings.creative.enabled) &
                                                             settingsCorrections(bit)));
        if (group == 6)
            values.insert("profile", QString::fromStdString(preset.settings.lens.profile));
        if (group == 1)
            values.insert("customWhiteBalance", preset.settings.customWhiteBalance);
        if (group == 2) {
            QJsonArray channels;
            for (const auto &curve : preset.settings.curves) {
                QJsonArray points;
                for (const auto &point : curve.points)
                    points.append(QJsonArray{point.x, point.y});
                channels.append(points);
            }
            values.insert("points", channels);
        }
        settings.insert(groupNames()[group], values);
    }
    QJsonObject result{{"format", "luma.develop-preset"},
                       {"schemaVersion", preset.parameters.empty() ? 8 : 9},
                       {"name", preset.name},
                       {"folder", preset.folder},
                       {"settings", settings}};
    if (!preset.parameters.empty()) {
        QJsonArray selection;
        for (const auto &name : preset.parameters)
            selection.append(QString::fromStdString(name));
        result.insert("parameters", selection);
    }
    return result;
}
DevelopPreset fromJson(const QJsonObject &root) {
    const auto version = header(root, "luma.develop-preset");
    QStringList fields{"format", "schemaVersion", "name", "folder", "settings"};
    if (version == 7 || version == 9)
        fields.append("parameters");
    keys(root, fields);
    require(root.value("name").isString() && root.value("folder").isString(),
            "Preset name and folder must be strings.");
    DevelopPreset preset{root.value("name").toString(), root.value("folder").toString(), {}, 0};
    const auto settings = object(root.value("settings"));
    for (auto entry = settings.begin(); entry != settings.end(); ++entry) {
        const auto index = groupNames().indexOf(entry.key());
        require(index >= 0 && (version >= 2 || index < 4) && (version >= 4 || index < 5) &&
                    (version >= 5 || index < 6) && (version >= 6 || index < 7) &&
                    (version >= 8 || index < 8),
                "Unsupported preset settings group.");
        const auto bit = std::uint32_t{1} << index;
        preset.groups |= bit;
        const auto values = object(entry.value());
        auto names = parameters(static_cast<int>(index));
        if (version < 3 && index == 4)
            names = names.mid(0, 4);
        auto expected = names;
        expected.append("enabledCorrections");
        if (index == 6)
            expected.append("profile");
        if (index == 1)
            expected.append("customWhiteBalance");
        if (index == 2)
            expected.append("points");
        keys(values, expected);
        std::vector<std::pair<std::string, double>> parametersToApply;
        for (const auto &name : names)
            parametersToApply.emplace_back(name.toStdString(), number(values.value(name)));
        if (index == 6) {
            require(values.value("profile").isString(), "Lens profile must be a string.");
            preset.settings.lens.profile = values.value("profile").toString().toStdString();
        }
        preset.settings.setParameters(parametersToApply);
        const auto flags = number(values.value("enabledCorrections"));
        const auto mask = version < 3 && index == 4 ? 2048U : settingsCorrections(bit);
        require(flags >= 0 && flags <= 4194303 && std::floor(flags) == flags &&
                    !(static_cast<std::uint32_t>(flags) & ~mask),
                "Invalid preset correction flags.");
        const auto selectedFlags = static_cast<std::uint32_t>(flags);
        preset.settings.enabledCorrections = (preset.settings.enabledCorrections & ~mask) |
                                             (selectedFlags & ~creativeCorrectionMask);
        preset.settings.creative.enabled =
            (preset.settings.creative.enabled & ~mask) | (selectedFlags & creativeCorrectionMask);
        if (index == 1) {
            require(values.value("customWhiteBalance").isBool(), "Invalid white balance mode.");
            preset.settings.customWhiteBalance = values.value("customWhiteBalance").toBool();
        }
        if (index == 2) {
            require(values.value("points").isArray(), "Curve points must be arrays.");
            const auto channels = values.value("points").toArray();
            require(channels.size() == 4, "Expected four curve channels.");
            for (int c = 0; c < 4; ++c) {
                require(channels[c].isArray(), "Curve points must be arrays.");
                const auto points = channels[c].toArray();
                require(points.size() >= 2 && points.size() <= 32, "Invalid curve point count.");
                auto &curve = preset.settings.curves[static_cast<std::size_t>(c)];
                curve.points.clear();
                for (const auto &value : points) {
                    require(value.isArray() && value.toArray().size() == 2,
                            "Expected a curve coordinate pair.");
                    curve.points.push_back(
                        {number(value.toArray()[0]), number(value.toArray()[1])});
                }
            }
        }
    }
    if (version == 7 || version == 9) {
        require(root.value("parameters").isArray(), "Sparse preset parameters must be an array.");
        const auto selection = root.value("parameters").toArray();
        require(!selection.isEmpty() && selection.size() <= 128,
                "Invalid sparse preset parameter count.");
        for (const auto &name : selection) {
            require(name.isString(), "Sparse preset parameter must be a string.");
            preset.parameters.push_back(name.toString().toStdString());
        }
    }
    preset.validate();
    return preset;
}
QJsonObject parse(const QByteArray &bytes) {
    require(bytes.size() <= maxBytes, "Preset file exceeds the 1 MiB limit.");
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && doc.isObject(), "Invalid preset JSON.");
    return doc.object();
}
QByteArray read(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot open preset file.");
    require(file.size() <= maxBytes, "Preset file exceeds the 1 MiB limit.");
    const auto bytes = file.read(maxBytes + 1);
    require(file.error() == QFileDevice::NoError, "Cannot read preset file.");
    return bytes;
}
void write(const QString &path, const QByteArray &bytes) {
    require(bytes.size() <= maxBytes, "Preset collection exceeds the 1 MiB limit.");
    QSaveFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
            "Cannot atomically save preset file; the previous file is preserved.");
}
void prepare(const QString &path) {
    require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create preset directory.");
}
} // namespace
void DevelopPreset::validate() const {
    validateSettingsGroups(groups);
    settings.validate();
    if (!parameters.empty())
        require(parameterSelectionGroups(parameters) == groups,
                "Sparse preset parameters do not match its settings groups.");
    const auto validText = [](const QString &text, bool empty) {
        return (empty || !text.isEmpty()) && text == text.trimmed() && text.size() <= 80 &&
               std::none_of(text.begin(), text.end(),
                            [](QChar c) { return c.category() == QChar::Other_Control; });
    };
    require(validText(name, false) && validText(folder, true),
            "Preset name and folder must be trimmed text of up to 80 characters.");
}
QByteArray encodePreset(const DevelopPreset &preset) {
    return QJsonDocument(toJson(preset)).toJson();
}
DevelopPreset decodePreset(const QByteArray &json) {
    return fromJson(parse(json));
}
std::vector<StoredPreset> PresetStore::load() const {
    if (!QFileInfo::exists(path_))
        return {};
    const auto root = parse(read(path_));
    keys(root, {"format", "schemaVersion", "presets"});
    header(root, "luma.preset-library");
    require(root.value("presets").isArray(), "Invalid preset collection.");
    const auto array = root.value("presets").toArray();
    require(array.size() <= static_cast<qsizetype>(maxPresets), "Preset collection limit reached.");
    std::vector<StoredPreset> result;
    QSet<QString> ids;
    for (const auto &value : array) {
        const auto entry = object(value);
        keys(entry, {"id", "preset"});
        require(entry.value("id").isString(), "Invalid preset identifier.");
        const auto id = entry.value("id").toString();
        require(!QUuid(id).isNull() && QUuid(id).toString(QUuid::WithoutBraces) == id &&
                    !ids.contains(id),
                "Invalid or duplicate preset identifier.");
        ids.insert(id);
        result.push_back({id, fromJson(object(entry.value("preset")))});
    }
    return result;
}
void PresetStore::save(const std::vector<StoredPreset> &presets) const {
    require(presets.size() <= maxPresets, "Preset collection limit reached (512).");
    QJsonArray entries;
    for (const auto &entry : presets)
        entries.append(QJsonObject{{"id", entry.id}, {"preset", toJson(entry.preset)}});
    const bool sparse = std::any_of(presets.begin(), presets.end(), [](const auto &entry) {
        return !entry.preset.parameters.empty();
    });
    write(path_, QJsonDocument(QJsonObject{{"format", "luma.preset-library"},
                                           {"schemaVersion", sparse ? 9 : 8},
                                           {"presets", entries}})
                     .toJson());
}
QString PresetStore::put(const DevelopPreset &preset, const QString &id) {
    preset.validate();
    prepare(path_);
    QLockFile lock(path_ + ".lock");
    require(lock.tryLock(0), "Preset collection is being modified by another app instance.");
    auto presets = load();
    const auto found =
        std::find_if(presets.begin(), presets.end(), [&](const auto &p) { return p.id == id; });
    QString result = id;
    if (id.isEmpty()) {
        result = QUuid::createUuid().toString(QUuid::WithoutBraces);
        presets.push_back({result, preset});
    } else {
        require(found != presets.end(), "Preset no longer exists. Refresh the collection.");
        found->preset = preset;
    }
    save(presets);
    return result;
}
void PresetStore::remove(const QString &id) {
    prepare(path_);
    QLockFile lock(path_ + ".lock");
    require(lock.tryLock(0), "Preset collection is being modified by another app instance.");
    auto presets = load();
    const auto count = std::erase_if(presets, [&](const auto &p) { return p.id == id; });
    require(count == 1, "Preset no longer exists. Refresh the collection.");
    save(presets);
}
DevelopPreset PresetStore::importFile(const QString &path) {
    return inspectImport(path).preset;
}
PresetImport PresetStore::inspectImport(const QString &path) {
    const auto bytes = read(path);
    if (QFileInfo(path).suffix().compare(QStringLiteral("xmp"), Qt::CaseInsensitive) == 0)
        return decodeXmpPreset(bytes, QFileInfo(path).completeBaseName());
    return {decodePreset(bytes), {}, false};
}
void PresetStore::exportFile(const QString &path, const DevelopPreset &preset) {
    write(path, encodePreset(preset));
}
} // namespace luma
