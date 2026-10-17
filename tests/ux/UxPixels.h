#pragma once
#include "core/edits/EditDocument.h"
#include "core/render/Sharpening.h"
#include <QImage>
#include <QString>
namespace ux {
class Context;
// Independent document-to-pixels oracle for the small generated public fixtures.
// This checks UI/state coherence; renderer numerical correctness has separate tests.
QImage fixtureReference(Context &context,
                        luma::DetailDiagnostic diagnostic = luma::DetailDiagnostic::none);
QImage fixtureReference(Context &context, const luma::EditDocument &document,
                        const QString &sourcePath,
                        luma::DetailDiagnostic diagnostic = luma::DetailDiagnostic::none);
} // namespace ux
