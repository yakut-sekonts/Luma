#include "tests/ux/UxPixels.h"
#include "apps/shared/PhotoDecoder.h"
#include "core/render/CpuRenderer.h"
#include "platform/macos/DisplayProfile.h"
#include "tests/ux/UxContext.h"
#include <QGuiApplication>
namespace ux {
QImage fixtureReference(Context &context, luma::DetailDiagnostic diagnostic) {
    return fixtureReference(context, context.editor.currentSettings(), context.editor.sourcePath(),
                            diagnostic);
}
QImage fixtureReference(Context &context, const luma::EditDocument &document,
                        const QString &sourcePath, luma::DetailDiagnostic diagnostic) {
    const auto decoded = luma::PhotoDecoder().decode(sourcePath.toStdString());
    require(decoded.image.width <= 512 && decoded.image.height <= 512,
            "Pixel oracle requires a small full-resolution public fixture");
    const auto profile = QGuiApplication::platformName() == "cocoa"
                             ? luma::macos::displayProfile(context.window->winId())
                             : std::vector<std::uint8_t>{};
    const auto pixels =
        luma::renderCPU(decoded.image, document, decoded.metadata.orientation, profile, {},
                        decoded.calibration, diagnostic, 1, decoded.metadata);
    return QImage(pixels.rgba.data(), pixels.width, pixels.height, pixels.width * 4,
                  QImage::Format_RGBA8888)
        .copy();
}
} // namespace ux
