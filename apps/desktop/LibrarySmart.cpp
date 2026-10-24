#include "apps/desktop/LibraryModel.h"
#include <QPointer>
#include <cmath>
namespace {
QVariantMap toMap(const luma::SmartRule &rule) {
    QVariantMap result{{"kind", QString::fromStdString(rule.kind)},
                       {"field", QString::fromStdString(rule.field)},
                       {"operation", QString::fromStdString(rule.operation)}};
    if (const auto number = std::get_if<double>(&rule.value))
        result["value"] = *number;
    else
        result["value"] = QString::fromStdString(std::get<std::string>(rule.value));
    QVariantList children;
    for (const auto &child : rule.children)
        children.append(toMap(child));
    result["children"] = children;
    return result;
}
luma::SmartRule fromMap(const QVariantMap &map, int depth, int &count) {
    if (++count > 64 || depth > 8)
        throw std::runtime_error("Smart collection rules are too complex.");
    luma::SmartRule result;
    result.kind = map.value("kind").toString().toStdString();
    if (result.kind == "condition") {
        result.field = map.value("field").toString().toStdString();
        result.operation = map.value("operation").toString().toStdString();
        const bool numeric = result.field == "rating" || result.field == "flag" ||
                             result.field == "iso" || result.field == "width" ||
                             result.field == "height";
        if (numeric) {
            bool ok = false;
            const double value = map.value("value").toDouble(&ok);
            if (!ok || !std::isfinite(value))
                throw std::runtime_error("Enter a valid numeric rule value.");
            result.value = value;
        } else
            result.value = map.value("value").toString().toStdString();
    } else {
        const auto children = map.value("children").toList();
        if (children.size() > 16)
            throw std::runtime_error("Smart collection rules are too complex.");
        for (const auto &child : children)
            result.children.push_back(fromMap(child.toMap(), depth + 1, count));
    }
    return result;
}
} // namespace
void LibraryModel::editSmartCollection(const QString &id, const QString &name) {
    const QPointer<LibraryModel> self(this);
    mutate([self, id, name](auto &db) {
        const auto record = db.smartCollection(id.toStdString());
        if (!record)
            throw std::runtime_error("Smart collection no longer exists.");
        const auto rule = toMap(record->rule);
        const auto revision = QString::number(record->revision);
        if (self)
            QMetaObject::invokeMethod(
                self,
                [self, id, name, revision, rule] {
                    if (self)
                        emit self->smartCollectionReady(id, name, revision, rule);
                },
                Qt::QueuedConnection);
    });
}
void LibraryModel::saveSmartCollection(const QString &id, const QString &name,
                                       const QString &revision, const QVariantMap &rule) {
    try {
        bool ok = false;
        const auto version = revision.toLongLong(&ok);
        if (!ok || version < 0)
            throw std::runtime_error("Invalid smart collection revision.");
        int count = 0;
        const auto parsed = fromMap(rule, 0, count);
        parsed.validate();
        mutate([id, name, version, parsed](auto &db) {
            db.saveSmartCollection(id.toStdString(), name.trimmed().toStdString(), version, parsed);
        });
    } catch (const std::exception &failure) {
        error_ = QString::fromUtf8(failure.what());
        emit stateChanged();
    }
}
