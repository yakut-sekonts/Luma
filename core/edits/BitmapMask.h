#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace luma {
// Immutable data is shared by history snapshots and render jobs.
class BitmapMask {
  public:
    BitmapMask() = default;
    BitmapMask(int width, int height, std::vector<std::uint8_t> values)
        : width_(width), height_(height) {
        if (width < 1 || height < 1 || width > 1024 || height > 1024 ||
            values.size() != static_cast<std::size_t>(width) * height)
            throw std::runtime_error("Invalid bitmap mask dimensions.");
        values_ = std::make_shared<const std::vector<std::uint8_t>>(std::move(values));
    }
    bool empty() const {
        return !values_;
    }
    int width() const {
        return width_;
    }
    int height() const {
        return height_;
    }
    std::span<const std::uint8_t> pixels() const {
        return values_ ? std::span<const std::uint8_t>(*values_) : std::span<const std::uint8_t>{};
    }
    double coverage(double x, double y) const {
        if (!values_ || !std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x > 1 || y > 1)
            return 0;
        const double px = std::clamp(x * width_ - .5, 0.0, width_ - 1.0);
        const double py = std::clamp(y * height_ - .5, 0.0, height_ - 1.0);
        const int x0 = static_cast<int>(px), y0 = static_cast<int>(py);
        const int x1 = std::min(x0 + 1, width_ - 1), y1 = std::min(y0 + 1, height_ - 1);
        const auto at = [&](int sx, int sy) {
            return (*values_)[static_cast<std::size_t>(sy) * width_ + sx] / 255.0;
        };
        return std::lerp(std::lerp(at(x0, y0), at(x1, y0), px - x0),
                         std::lerp(at(x0, y1), at(x1, y1), px - x0), py - y0);
    }
    bool operator==(const BitmapMask &other) const {
        return width_ == other.width_ && height_ == other.height_ &&
               (values_ == other.values_ ||
                (values_ && other.values_ && *values_ == *other.values_));
    }

  private:
    int width_ = 0, height_ = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> values_;
};
} // namespace luma
