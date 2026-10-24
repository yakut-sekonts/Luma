#include "apps/desktop/EditorController.h"
#include "core/image/Bayer.h"
#include "core/render/CpuRenderer.h"
#include <QPointer>
#include <algorithm>

QVariantMap EditorController::comparison() const {
    return {
        {"busy", compareBusy_},
        {"ready", compareReady_},
        {"error", compareError_},
        {"fullResolution", compareFull_},
        {"ids", compareIDs_},
        {"names", compareNames_},
        {"left", QStringLiteral("image://preview/compare-left?%1").arg(compareImageRevision_)},
        {"right", QStringLiteral("image://preview/compare-right?%1").arg(compareImageRevision_)}};
}
void EditorController::closeCompare() {
    compareJob_.cancel();
    ++compareRevision_;
    compareBusy_ = false;
    compareReady_ = false;
    compareError_.clear();
    compareIDs_.clear();
    compareNames_.clear();
    provider_->updateCompare({}, {});
    emit stateChanged();
}
void EditorController::prepareCompare(const QStringList &ids, bool fullResolution) {
    if (closing_ || ids.size() != 2 || ids[0] == ids[1])
        return;
    finishHistoryTools();
    compareJob_.cancel();
    const auto revision = ++compareRevision_;
    compareBusy_ = true;
    compareReady_ = false;
    compareError_.clear();
    compareFull_ = fullResolution;
    compareIDs_ = ids;
    compareNames_.clear();
    provider_->updateCompare({}, {});
    const auto active = session_ ? session_->asset.id : std::string{};
    const auto activeEdits = history_.current();
    const auto catalogPath = catalog_;
    const auto service = service_;
    const auto profile = displayProfile_;
    const QPointer<EditorController> self(this);
    try {
        compareJob_ = compareJobs_.submit(
            luma::JobPriority::interactive,
            [self, revision, ids, fullResolution, active, activeEdits, catalogPath, service,
             profile](const luma::JobContext &context) {
                std::array<QImage, 2> images;
                QStringList names;
                QString error;
                try {
                    luma::Catalog catalog(catalogPath);
                    // Freeze both edits before decoding. Never change the active Library asset.
                    std::array<luma::AssetRecord, 2> assets;
                    for (int i = 0; i < 2; ++i) {
                        auto snapshot = catalog.assetSnapshot(ids[i].toStdString());
                        if (!snapshot)
                            throw std::runtime_error(
                                "Comparison photo is no longer in the catalog.");
                        assets[i] = std::move(snapshot->asset);
                        if (assets[i].id == active)
                            assets[i].edits = activeEdits;
                        names.append(QString::fromStdString(assets[i].path.filename().string()));
                    }
                    for (int i = 0; i < 2; ++i) {
                        luma::checkCancellation(context.stopToken());
                        const auto decoded = service.fullResolution(assets[i], context.stopToken());
                        const auto preview =
                            fullResolution ? std::optional<luma::FloatImage>{}
                                           : std::optional<luma::FloatImage>{luma::standardPreview(
                                                 decoded->image, 2560, context.stopToken())};
                        const auto &input = preview ? *preview : decoded->image;
                        const double scale =
                            static_cast<double>(input.width) / decoded->image.width;
                        const auto rendered = luma::renderCPU(
                            input, assets[i].edits, assets[i].metadata.orientation, profile,
                            context.stopToken(), decoded->calibration, luma::DetailDiagnostic::none,
                            scale, assets[i].metadata);
                        images[i] = QImage(rendered.rgba.data(), rendered.width, rendered.height,
                                           rendered.width * 4, QImage::Format_RGBA8888)
                                        .copy();
                        if (images[i].isNull())
                            throw std::runtime_error("Cannot allocate the comparison preview.");
                    }
                } catch (const std::exception &failure) {
                    error = QString::fromUtf8(failure.what());
                }
                if (!self || context.stopToken().isCancelled())
                    return;
                QMetaObject::invokeMethod(
                    self,
                    [self, revision, images = std::move(images), names, error]() mutable {
                        if (!self || self->compareRevision_ != revision)
                            return;
                        self->compareBusy_ = false;
                        self->compareError_ = error;
                        self->compareReady_ = error.isEmpty();
                        if (error.isEmpty()) {
                            self->provider_->updateCompare(std::move(images[0]),
                                                           std::move(images[1]));
                            self->compareNames_ = names;
                            ++self->compareImageRevision_;
                        }
                        emit self->stateChanged();
                    },
                    Qt::QueuedConnection);
            });
    } catch (const std::exception &failure) {
        compareBusy_ = false;
        compareError_ = QString::fromUtf8(failure.what());
    }
    emit stateChanged();
}
