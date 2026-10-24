#pragma once
#include "core/render/ImageHistogram.h"
#include <QQuickImageProvider>
#include <mutex>
#include <optional>

// One cache entry owns both the pixels and the analysis of that exact render.
struct RenderedPreview {
    QImage image;
    std::optional<luma::ImageHistogram> histogram;
};

class PreviewProvider final : public QQuickImageProvider {
  public:
    PreviewProvider();
    void update(QImage image);
    void updateCompare(QImage left, QImage right);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

  private:
    std::mutex mutex_;
    QImage image_, compareLeft_, compareRight_;
};
