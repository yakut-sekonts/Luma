#pragma once
#include "tests/ux/UxDistribution.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QQuickWindow>
#include <QTimer>
#include <vector>
namespace ux {
class Metrics {
  public:
    explicit Metrics(QString heartbeat);
    void attach(QQuickWindow &window);
    void reset();
    void input(double milliseconds);
    QJsonObject json() const;
    std::size_t polishes() const {
        return polishes_;
    }
    std::size_t frames() const {
        return frames_;
    }

  private:
    QTimer timer_;
    QElapsedTimer elapsed_;
    QString heartbeat_;
    qint64 previousBeat_ = 0, previousFrame_ = 0, previousSample_ = 0;
    std::size_t frames_ = 0, polishes_ = 0;
    Distribution stalls_, intervals_, inputs_;
    double rssFirst_ = 0, rssLast_ = 0, rssPeak_ = 0;
    std::uint64_t rssSamples_ = 0;
    struct MemoryMinute {
        double minimum = 0, maximum = 0, sum = 0;
        std::uint64_t count = 0;
    };
    std::vector<MemoryMinute> memoryMinutes_;
};
void writeJson(const QString &path, const QJsonObject &value);
} // namespace ux
