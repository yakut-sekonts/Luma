#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

namespace luma::desktop {
enum class BundledModel { denoise, sky };
inline QString bundledModelPath(BundledModel model) {
    const auto filename = model == BundledModel::denoise ? QStringLiteral("LumaDenoise.mlmodelc")
                                                         : QStringLiteral("LumaSky.mlmodelc");
    const auto path = QDir::cleanPath(QCoreApplication::applicationDirPath() +
                                      QStringLiteral("/../Resources/Models/") + filename);
    const QFileInfo file(path);
    return file.isDir() && !file.isSymLink() ? path : QString();
}
} // namespace luma::desktop
