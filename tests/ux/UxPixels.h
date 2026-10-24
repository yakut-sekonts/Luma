#pragma once
#include "core/edits/EditDocument.h"
#include "core/render/Sharpening.h"
#include <QImage>
#include <QSharedPointer>
#include <QString>
class QQuickItem;
class QQuickItemGrabResult;
namespace luma {
struct AssetSnapshot;
}
namespace ux {
class Context;
// Wait for GUI ready before reading pixels; keep a failed pending result until
// Context's engine/window has been destroyed, rather than destroy it mid-render.
QSharedPointer<QQuickItemGrabResult> readyItemGrab(Context &context, QQuickItem &item,
                                                   const QString &reason, int timeout = 5000);
// Independent document-to-pixels oracle for the small generated public fixtures.
// This checks UI/state coherence; renderer numerical correctness has separate tests.
QImage fixtureReference(Context &context,
                        luma::DetailDiagnostic diagnostic = luma::DetailDiagnostic::none);
QImage fixtureReference(Context &context, const luma::EditDocument &document,
                        const QString &sourcePath,
                        luma::DetailDiagnostic diagnostic = luma::DetailDiagnostic::none);
// Independent CPU reference: RGB <= 1 only for Metal-eligible documents;
// alpha, geometry, format, color space and DPR remain exact. CPU paths are exact.
bool previewPixelsAgree(const QImage &actual, const QImage &expected, bool metalEligible);
// Small acceptance/rejection controls, invoked by each affected scenario family.
void checkPreviewPixelOracle();
// Failure evidence only; never changes pixel acceptance or retries the scenario.
void recordPreviewDifference(Context &context, const QImage &expected, const QString &reason);
// Exact saved-document JPEG cache plus actual Image-node scene readback.
// The URL generation is an invalidation counter, not the asset edit revision.
void thumbnailCardPixels(Context &context, QQuickItem *card, const luma::AssetSnapshot &saved);
} // namespace ux
