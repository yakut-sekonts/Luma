#include "apps/desktop/AppearanceController.h"
#include "apps/desktop/LocalizationController.h"
#include "apps/desktop/LocalizedText.h"
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <typename F> void wait(F ready) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    require(ready(), "Language QML timed out.");
}
QString read(const QString &path) {
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), "Cannot read preferences fixture.");
    return QString::fromUtf8(f.readAll());
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    require(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(),
            "Cannot write preferences fixture.");
}
} // namespace
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    try {
        require(argc == 2, "Expected QML directory.");
        QTemporaryDir temporary;
        require(temporary.isValid(), "Cannot create language fixture.");
        const auto path = temporary.path() + QStringLiteral("/language.json");
        QLocale::setDefault(QLocale(QStringLiteral("ru")));
        {
            std::unique_ptr<AppearanceController> appearance;
            QQmlEngine engine;
            LocalizationController controller(path, engine);
            const auto appearancePath = temporary.path() + QStringLiteral("/appearance.json");
            appearance = std::make_unique<AppearanceController>(appearancePath);
            require(controller.language() == QStringLiteral("en") && !QFile::exists(path),
                    "Default must be English without startup writes.");
            engine.rootContext()->setContextProperty("localization", &controller);
            engine.rootContext()->setContextProperty("appearance", appearance.get());
            // This test exercises language selection, not catalog/model actions.
            // Supply the read-only state consumed by the expanded preferences UI.
            QQmlPropertyMap editor, library, diagnostics;
            for (const auto &name :
                 {"saving", "metalPreview", "hdrToneMapping", "denoiseModelConfigured",
                  "denoiseModelBundled", "skyModelConfigured", "skyModelBundled"})
                editor.insert(name, false);
            editor.insert("catalogPath", QString{});
            editor.insert("previewCacheMiB", 64);
            editor.insert("denoiseModelName", QString{});
            editor.insert("skyModelName", QString{});
            library.insert("working", false);
            library.insert("backupSchedule", 0);
            library.insert("backupKeep", 5);
            library.insert("autoWriteXMP", false);
            library.insert("backupFolder", QUrl{});
            library.insert("status", QString{});
            library.insert("error", QString{});
            diagnostics.insert("snapshot", QVariantMap{});
            diagnostics.insert("busy", false);
            diagnostics.insert("localFolder", QUrl{});
            diagnostics.insert("error", QString{});
            diagnostics.insert("savedPath", QString{});
            engine.rootContext()->setContextProperty("editor", &editor);
            engine.rootContext()->setContextProperty("library", &library);
            engine.rootContext()->setContextProperty("diagnostics", &diagnostics);
            QStringList warnings;
            QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError> &errors) {
                for (const auto &error : errors)
                    warnings.append(error.toString());
            });
            QQmlComponent component(&engine);
            component.setData(
                ("import QtQuick\nimport QtQuick.Controls\nimport \"" +
                 QUrl::fromLocalFile(QString::fromUtf8(argv[1])).toString() +
                 "\"\nApplicationWindow { width: 700; height: 550; visible: true; Text { "
                 "objectName: \"translatedCaption\"; text: qsTranslate(\"Apertone\", \"Export\") } "
                 "PreferencesPanel { id: settings; Component.onCompleted: open() } }")
                    .toUtf8(),
                QUrl());
            wait([&] { return component.status() != QQmlComponent::Loading; });
            if (component.isError())
                throw std::runtime_error(component.errorString().toStdString());
            const std::unique_ptr<QObject> window(component.create());
            require(bool(window), "Preferences QML did not create.");
            auto *caption = window->findChild<QObject *>("translatedCaption");
            auto *choice = window->findChild<QObject *>("languageChoice");
            auto *appearanceChoice = window->findChild<QObject *>("appearanceChoice");
            require(caption && choice && appearanceChoice, "Preference controls are missing.");
            require(appearance->mode() == QStringLiteral("system") &&
                        !QFile::exists(appearancePath),
                    "Appearance must follow the system without a startup write.");
            require(appearanceChoice->setProperty("currentIndex", 1) &&
                        QMetaObject::invokeMethod(appearanceChoice, "activated", Q_ARG(int, 1)) &&
                        appearance->mode() == QStringLiteral("day") &&
                        read(appearancePath).contains(QStringLiteral("day")),
                    "Day appearance was not applied and persisted.");
            require(appearanceChoice->setProperty("currentIndex", 2) &&
                        QMetaObject::invokeMethod(appearanceChoice, "activated", Q_ARG(int, 2)) &&
                        appearance->mode() == QStringLiteral("night") && appearance->dark() &&
                        read(appearancePath).contains(QStringLiteral("night")),
                    "Night appearance was not applied and persisted.");
            const auto languages = controller.languages();
            require(languages.size() == 8, "Expected eight language packs.");
            const QStringList expected{QStringLiteral("Export"),      QStringLiteral("Экспорт"),
                                       QStringLiteral("导出"),        QStringLiteral("Արտահանել"),
                                       QStringLiteral("Exportieren"), QStringLiteral("Exportar"),
                                       QStringLiteral("Esporta"),     QStringLiteral("Exporter")};
            const auto message = LocalizedText(QStringLiteral("Importing %1"))
                                     .arg(QStringLiteral("Export %1 %2_日本語.NEF"));
            for (qsizetype i = 0; i < languages.size(); ++i) {
                const auto code = languages[i].toMap().value("code").toString();
                require(choice->setProperty("currentIndex", i), "Cannot choose a language.");
                require(
                    QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, static_cast<int>(i))),
                    "Cannot activate actual language QML.");
                QCoreApplication::processEvents();
                require(controller.language() == code && controller.error().isEmpty(),
                        "Language selection failed.");
                require(caption->property("text").toString() == expected[i],
                        "Existing QML binding did not retranslate.");
                require(message.text().contains(QStringLiteral("Export %1 %2_日本語.NEF")),
                        "Translation changed a filename or reinterpreted placeholders.");
                require(read(path).contains(code), "Explicit language was not persisted.");
            }
            require(!controller.selectLanguage(QStringLiteral("unknown")) &&
                        controller.language() == QStringLiteral("fr"),
                    "Invalid locale changed the active pack.");
            QLockFile lock(path + QStringLiteral(".lock"));
            require(lock.tryLock(0), "Cannot lock language preferences.");
            const auto before = read(path);
            require(!controller.selectLanguage(QStringLiteral("de")) &&
                        controller.language() == QStringLiteral("fr") && read(path) == before,
                    "Write failure changed language or preferences.");
            if (!warnings.isEmpty())
                throw std::runtime_error(warnings.join('\n').toStdString());
        }
        {
            QQmlEngine engine;
            LocalizationController restored(path, engine);
            require(restored.language() == QStringLiteral("fr") &&
                        QCoreApplication::translate("Apertone", "Export") ==
                            QStringLiteral("Exporter"),
                    "Language did not survive restart.");
        }
        {
            const auto appearancePath = temporary.path() + QStringLiteral("/appearance.json");
            AppearanceController restored(appearancePath);
            require(restored.mode() == QStringLiteral("night") && restored.dark(),
                    "Appearance did not survive restart.");
            const auto corrupt = QByteArray("{\"schema\":2,\"mode\":\"day\"}");
            write(appearancePath, corrupt);
            AppearanceController invalid(appearancePath);
            require(invalid.mode() == QStringLiteral("system") && !invalid.error().isEmpty() &&
                        read(appearancePath).toUtf8() == corrupt,
                    "Corrupt appearance preferences were not preserved.");
        }
        for (const auto &bytes :
             {QByteArray("invalid"), QByteArray("{\"schema\":1,\"language\":\"invalid\"}"),
              QByteArray("{\"schema\":2,\"language\":\"ru\"}")}) {
            write(path, bytes);
            QQmlEngine engine;
            LocalizationController invalid(path, engine);
            require(invalid.language() == QStringLiteral("en") && !invalid.error().isEmpty() &&
                        read(path).toUtf8() == bytes,
                    "Corrupt preferences did not preserve data and fall back to English.");
        }
        std::cout << "Eight language packs and system/day/night appearance: runtime selection, "
                     "persistence and failure handling passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
