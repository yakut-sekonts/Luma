#include "tests/ux/UxMetrics.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <mach/mach.h>
#include <stdexcept>
namespace ux {
void writeJson(const QString &path, const QJsonObject &value) {
    QSaveFile file(path);
    const auto bytes = QJsonDocument(value).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Cannot save UX evidence: " + path.toStdString());
}
Metrics::Metrics(QString heartbeat) : heartbeat_(std::move(heartbeat)) {
    reset();
    timer_.setInterval(10);
    QObject::connect(&timer_, &QTimer::timeout, [&] {
        const auto now = elapsed_.elapsed();
        if (previousBeat_)
            stalls_.add(static_cast<double>(now - previousBeat_));
        previousBeat_ = now;
        if (now - previousSample_ >= 200) {
            previousSample_ = now;
            mach_task_basic_info_data_t info{};
            mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
            if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                          reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
                const double rss = static_cast<double>(info.resident_size) / 1048576;
                if (rssSamples_++ == 0)
                    rssFirst_ = rss;
                rssLast_ = rss;
                rssPeak_ = std::max(rssPeak_, rss);
                const auto minute = static_cast<std::size_t>(now / 60000);
                if (minute < 1440) {
                    if (memoryMinutes_.size() <= minute)
                        memoryMinutes_.resize(minute + 1);
                    auto &sample = memoryMinutes_[minute];
                    sample.minimum = sample.count == 0 ? rss : std::min(sample.minimum, rss);
                    sample.maximum = std::max(sample.maximum, rss);
                    sample.sum += rss;
                    ++sample.count;
                }
            }
            writeJson(heartbeat_, {{"utc_ms", QDateTime::currentMSecsSinceEpoch()},
                                   {"frames", static_cast<qint64>(frames_)}});
        }
    });
    timer_.start();
}
void Metrics::attach(QQuickWindow &window) {
    QObject::connect(&window, &QQuickWindow::afterAnimating, &timer_, [&] { ++polishes_; });
    QObject::connect(&window, &QQuickWindow::frameSwapped, &timer_, [&] {
        const auto now = elapsed_.elapsed();
        if (previousFrame_)
            intervals_.add(static_cast<double>(now - previousFrame_));
        previousFrame_ = now;
        ++frames_;
    });
}
void Metrics::reset() {
    elapsed_.restart();
    previousBeat_ = previousFrame_ = previousSample_ = 0;
    frames_ = polishes_ = 0;
    stalls_ = {};
    intervals_ = {};
    inputs_ = {};
    rssFirst_ = rssLast_ = rssPeak_ = 0;
    rssSamples_ = 0;
    memoryMinutes_.clear();
}
void Metrics::input(double milliseconds) {
    inputs_.add(milliseconds);
}
QJsonObject Metrics::json() const {
    QJsonArray memory;
    for (std::size_t i = 0; i < memoryMinutes_.size(); ++i) {
        const auto &m = memoryMinutes_[i];
        if (m.count)
            memory.append(QJsonObject{{"minute", static_cast<qint64>(i)},
                                      {"minimum_mb", m.minimum},
                                      {"maximum_mb", m.maximum},
                                      {"mean_mb", m.sum / static_cast<double>(m.count)},
                                      {"samples", static_cast<qint64>(m.count)}});
    }
    return {{"duration_ms", elapsed_.elapsed()},
            {"main_loop_stall_max_ms", stalls_.maximum()},
            {"stall_over_100_ms", static_cast<qint64>(stalls_.above(0))},
            {"stall_over_250_ms", static_cast<qint64>(stalls_.above(1))},
            {"stall_over_1000_ms", static_cast<qint64>(stalls_.above(2))},
            {"hard_hang", stalls_.maximum() > 5000},
            {"frame_p50_ms", intervals_.percentile(.5)},
            {"frame_p95_ms", intervals_.percentile(.95)},
            {"frame_p99_ms", intervals_.percentile(.99)},
            {"frame_max_ms", intervals_.maximum()},
            {"frame_intervals", static_cast<qint64>(intervals_.count())},
            {"quantile_resolution_ms", 1},
            {"input_dispatch_p95_ms", inputs_.percentile(.95)},
            {"rss_peak_mb", rssPeak_},
            {"rss_first_mb", rssFirst_},
            {"rss_last_mb", rssLast_},
            {"rss_samples", static_cast<qint64>(rssSamples_)},
            {"rss_minutes", memory},
            {"rss_minutes_limit", 1440}};
}
} // namespace ux
