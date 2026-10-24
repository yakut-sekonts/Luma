#pragma once
#include "apps/desktop/BatchController.h"
#include "apps/desktop/EditorController.h"
#include "apps/desktop/ExportController.h"
#include "apps/desktop/LibraryModel.h"
#include "apps/desktop/LocalizationController.h"
#include "apps/desktop/PresetController.h"
#include "apps/desktop/ThumbnailProvider.h"
#include "apps/desktop/UpdateController.h"
#include "tests/ux/SqliteFault.h"
#include "tests/ux/UxDriver.h"
#include <QQmlApplicationEngine>
#include <QSharedPointer>
#include <memory>
class QQuickItemGrabResult;
class DiagnosticsController;
namespace luma::desktop {
class DiagnosticSession;
}
namespace ux {
class Context {
  public:
    Context(QString root, QString fixture, const QString &qml, QString state = {},
            bool updaterProbe = false);
    ~Context();
    QString root, fixture;
    QString stateRoot, catalogFile;
    SqliteFault storageFault;
    PreviewProvider *provider;
    EditorController editor;
    PresetController presets;
    LibraryModel library;
    BatchController batch;
    ExportController exporter;
    UpdateController updates;
    std::unique_ptr<luma::desktop::DiagnosticSession> diagnosticSession;
    std::unique_ptr<DiagnosticsController> diagnostics;
    QStringList warnings;
    std::unique_ptr<LocalizationController> localization;
    // A failed readback stays alive past engine/window/render-thread teardown.
    // No second readback may start after this first unfinished failure.
    QSharedPointer<QQuickItemGrabResult> unfinishedItemGrab;
    QQmlApplicationEngine engine;
    QQuickWindow *window = nullptr;
    Metrics metrics;
    std::unique_ptr<Driver> driver;
    void loaded();
    void saved();
    void import(int count, bool copy = false);
    void selectPresetGroups(std::uint32_t groups);
    void openFirst();
    void addMask(int kind);
    void addComponent(int kind, int operation);
    QString exportPNG(const QString &name);
    void reveal(const QString &id);
    QString card(int row, bool filmstrip = false);
    void checkData();
    void finish(const QString &id, const QString &status, const QString &message = {});
};
bool lifecycleScenario(Context &c, const QString &id);
bool importScenario(Context &c, const QString &id);
bool recoveryScenario(Context &c, const QString &id);
bool exportScenario(Context &c, const QString &id);
void restartScenario(Context &c, const QString &id, const QString &stage);
bool batchScenario(Context &c, const QString &id);
bool transformScenario(Context &c, const QString &id);
void editTransformControls(Context &c);
bool geometryScenario(Context &c, const QString &id);
bool detailScenario(Context &c, const QString &id);
void editDetailControls(Context &c, bool includeBasic = false);
bool lensScenario(Context &c, const QString &id);
void openKnownLens(Context &c);
void selectKnownLensProfile(Context &c);
bool advancedScenario(Context &c, const QString &id);
bool presetScenario(Context &c, const QString &id);
bool localizationScenario(Context &c, const QString &id);
bool asyncScenario(Context &c, const QString &id);
bool maskScenario(Context &c, const QString &id);
bool libraryScenario(Context &c, const QString &id);
void basicScenario(Context &c, const QString &id);
} // namespace ux
