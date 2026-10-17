#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace ux {
// Fixed memory over arbitrarily long runs; quantiles use conservative 1-unit bins.
class Distribution {
  public:
    void add(double value) {
        if (!std::isfinite(value) || value < 0)
            throw std::runtime_error("Invalid UX metric sample");
        const auto bin =
            static_cast<std::size_t>(std::min(value, static_cast<double>(bins_.size() - 1)));
        ++bins_[bin];
        ++count_;
        maximum_ = std::max(maximum_, value);
        for (std::size_t i = 0; i < thresholds_.size(); ++i)
            if (value > thresholds_[i])
                ++above_[i];
    }
    double percentile(double fraction) const {
        if (!std::isfinite(fraction) || fraction <= 0 || fraction > 1)
            throw std::runtime_error("Invalid UX metric percentile");
        if (count_ == 0)
            return 0;
        const auto rank = std::max<std::uint64_t>(
            1, static_cast<std::uint64_t>(std::ceil(fraction * static_cast<double>(count_))));
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < bins_.size(); ++i) {
            sum += bins_[i];
            if (sum >= rank)
                return i == bins_.size() - 1 ? maximum_
                                             : std::min(maximum_, static_cast<double>(i + 1));
        }
        return maximum_;
    }
    double maximum() const {
        return maximum_;
    }
    std::uint64_t count() const {
        return count_;
    }
    std::uint64_t above(std::size_t threshold) const {
        return above_.at(threshold);
    }

  private:
    std::array<std::uint64_t, 8193> bins_{};
    std::array<std::uint64_t, 3> above_{};
    static constexpr std::array<double, 3> thresholds_{100, 250, 1000};
    std::uint64_t count_ = 0;
    double maximum_ = 0;
};
} // namespace ux
