#include "apps/desktop/BundledModels.h"
#include "apps/desktop/EditorController.h"
#include "apps/shared/DerivedImage.h"
#include "core/image/Bayer.h"
#include "core/raw/RawDecoder.h"
#include "platform/macos/CoreMLDenoise.h"
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QXmlStreamWriter>
#include <atomic>
#include <cmath>

namespace {
struct DenoiseProgress {
    std::atomic<double> value{0};
    std::atomic<bool> queued{false};
};
} // namespace

QString EditorController::denoiseModelName() const {
    return QFileInfo(denoiseModelPath_).fileName();
}
bool EditorController::denoiseModelBundled() const {
    return !denoiseModelPath_.isEmpty() &&
           denoiseModelPath_ ==
               luma::desktop::bundledModelPath(luma::desktop::BundledModel::denoise);
}
void EditorController::setDenoiseModel(const QUrl &url) {
    if (!url.isEmpty() && !url.isLocalFile())
        return;
    cancelAI();
    const auto overridePath = url.isEmpty() ? QString() : url.toLocalFile();
    denoiseModelPath_ = overridePath.isEmpty()
                            ? luma::desktop::bundledModelPath(luma::desktop::BundledModel::denoise)
                            : overridePath;
    QSettings settings(QString::fromStdString(catalog_.parent_path().string()) + "/preview.ini",
                       QSettings::IniFormat);
    // Persist only the user's override. Bundled paths are resolved again after
    // updates or moving the application to a different directory.
    settings.setValue("denoiseModel", overridePath);
    emit stateChanged();
}
void EditorController::denoisePhoto(double amount) {
    if (!editable() || aiBusy_ || !denoiseModelConfigured() || !std::isfinite(amount) ||
        amount <= 0 || amount > 100)
        return;
    cancelAI();
    setRetouchMode(QStringLiteral("off"));
    endEdit();
    const auto revision = ++aiRevision_;
    const auto asset = session_->asset;
    const auto service = service_;
    const auto catalog = catalog_;
    const auto path = std::filesystem::path(denoiseModelPath_.toStdString());
    const QPointer<EditorController> self(this);
    aiBusy_ = true;
    aiOperation_ = QStringLiteral("AI Denoise");
    aiProgress_ = 0;
    error_.clear();
    emit stateChanged();
    try {
        aiJob_ = aiJobs_.submit(
            luma::JobPriority::background, [self, revision, asset, service, catalog, path,
                                            amount](const luma::JobContext &context) {
                try {
                    const auto stop = context.stopToken();
                    const auto progressState = std::make_shared<DenoiseProgress>();
                    const auto progress = [self, revision, progressState](double value) {
                        progressState->value.store(value);
                        // DNG writing reports each row. Retain the latest value
                        // in one pending GUI callback so progress cannot starve
                        // input/timers through repeated QML binding updates.
                        if (self && !progressState->queued.exchange(true))
                            QMetaObject::invokeMethod(
                                self,
                                [self, revision, progressState] {
                                    progressState->queued.store(false);
                                    if (self && self->aiRevision_ == revision) {
                                        self->aiProgress_ = progressState->value.load();
                                        emit self->stateChanged();
                                    }
                                },
                                Qt::QueuedConnection);
                    };
                    // This action is for RAW/Linear DNG, not an embedded JPEG preview.
                    (void)luma::RawDecoder().inspect(asset.path, stop);
                    const auto provider = luma::macos::makeCoreMLDenoise(path);
                    luma::checkCancellation(stop);
                    const auto decoded = service.fullResolution(asset, stop);
                    luma::EditDocument neutral;
                    neutral.enabledCorrections = 0; // Camera/as-shot conversion is retained.
                    auto base = luma::renderLinearOutput(*decoded, neutral, stop);
                    luma::FloatImage input(base.width, base.height);
                    for (std::size_t i = 0; i < input.rgb.size() / 3; ++i) {
                        if (base.rgba[i * 4 + 3] != 1)
                            throw std::runtime_error("AI Denoise requires an opaque RAW image.");
                        for (std::size_t c = 0; c < 3; ++c)
                            input.rgb[i * 3 + c] = base.rgba[i * 4 + c];
                    }
                    progress(.15);
                    auto result = provider->denoise(input, amount / 100, stop, [&](double value) {
                        progress(.15 + .7 * value);
                    });
                    for (std::size_t i = 0; i < result.rgb.size() / 3; ++i) {
                        if ((i & 65535) == 0)
                            luma::checkCancellation(stop);
                        for (std::size_t c = 0; c < 3; ++c)
                            base.rgba[i * 4 + c] = result.rgb[i * 3 + c];
                    }
                    QByteArray metadata;
                    QXmlStreamWriter writer(&metadata);
                    writer.writeStartElement("x:xmpmeta");
                    writer.writeNamespace("adobe:ns:meta/", "x");
                    writer.writeStartElement("rdf:RDF");
                    writer.writeNamespace("http://www.w3.org/1999/02/22-rdf-syntax-ns#", "rdf");
                    writer.writeStartElement("rdf:Description");
                    writer.writeNamespace("https://luma.photo/ns/1.0/", "luma");
                    writer.writeAttribute("rdf:about", "");
                    writer.writeAttribute("luma:Operation", "AI Denoise");
                    writer.writeAttribute("luma:SourceID", QString::fromStdString(asset.id));
                    writer.writeAttribute("luma:SourceSHA256",
                                          QString::fromStdString(asset.checksum));
                    writer.writeAttribute("luma:Amount", QString::number(amount));
                    writer.writeAttribute("luma:Provider",
                                          QString::fromStdString(provider->identifier()));
                    writer.writeEndElement();
                    writer.writeEndElement();
                    writer.writeEndElement();
                    const auto created = luma::createDerivedImage(
                        catalog, base, metadata.toStdString(), stop,
                        [&](double value) { progress(.85 + .15 * value); });
                    if (self)
                        QMetaObject::invokeMethod(
                            self,
                            [self, revision, created] {
                                if (!self || self->aiRevision_ != revision)
                                    return;
                                self->aiBusy_ = false;
                                self->aiProgress_ = 1;
                                emit self->stateChanged();
                                self->openFile(
                                    QUrl::fromLocalFile(QString::fromStdString(created.string())));
                            },
                            Qt::QueuedConnection);
                } catch (const std::exception &failure) {
                    const auto message = QString::fromUtf8(failure.what());
                    const bool cancelled = context.stopToken().isCancelled();
                    if (self)
                        QMetaObject::invokeMethod(
                            self,
                            [self, revision, message, cancelled] {
                                if (!self || self->aiRevision_ != revision)
                                    return;
                                self->aiBusy_ = false;
                                if (!cancelled)
                                    self->error_ = message;
                                emit self->stateChanged();
                            },
                            Qt::QueuedConnection);
                }
            });
    } catch (const std::exception &failure) {
        cancelAI();
        advancedFailure(failure);
    }
}
