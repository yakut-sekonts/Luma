#include "apps/desktop/PreviewProvider.h"

PreviewProvider::PreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
void PreviewProvider::update(QImage image) {
    std::lock_guard lock(mutex_);
    image_ = std::move(image);
}
void PreviewProvider::updateCompare(QImage left, QImage right) {
    std::lock_guard lock(mutex_);
    compareLeft_ = std::move(left);
    compareRight_ = std::move(right);
}
QImage PreviewProvider::requestImage(const QString &id, QSize *size, const QSize &) {
    std::lock_guard lock(mutex_);
    const auto key = id.section('?', 0, 0);
    const auto &result = key == "compare-left"    ? compareLeft_
                         : key == "compare-right" ? compareRight_
                                                  : image_;
    if (size) {
        *size = result.size();
    }
    return result;
}
