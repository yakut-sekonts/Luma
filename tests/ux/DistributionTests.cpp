#include "tests/ux/UxDistribution.h"
#include <iostream>
#include <limits>
#include <vector>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "Invalid measurement was accepted");
}
} // namespace
int main() {
    try {
        ux::Distribution distribution;
        require(distribution.percentile(.95) == 0 && distribution.maximum() == 0 &&
                    distribution.count() == 0,
                "Empty measurements are not empty");
        std::vector<double> reference;
        for (int i = 0; i < 100003; ++i) {
            const double value = static_cast<double>((i * 7919) % 300017) / 100;
            reference.push_back(value);
            distribution.add(value);
        }
        std::sort(reference.begin(), reference.end());
        require(distribution.count() == reference.size() &&
                    distribution.maximum() == reference.back(),
                "Count/maximum differs from independent exact sample");
        for (double p : {.01, .5, .95, .99, 1.0}) {
            const auto rank =
                static_cast<std::size_t>(std::ceil(p * static_cast<double>(reference.size())));
            const auto exact = reference[rank - 1];
            const auto measured = distribution.percentile(p);
            require(measured >= exact && measured <= exact + 1,
                    "Quantile lost its conservative 1 ms bound");
        }
        for (std::size_t i = 0; const double threshold : {100, 250, 1000}) {
            require(distribution.above(i++) ==
                        static_cast<std::uint64_t>(
                            std::count_if(reference.begin(), reference.end(),
                                          [threshold](double value) { return value > threshold; })),
                    "Stall threshold count differs from exact samples");
        }
        distribution = {};
        for (double value : {100.0, 250.0, 1000.0})
            distribution.add(value);
        require(distribution.above(0) == 2 && distribution.above(1) == 1 &&
                    distribution.above(2) == 0,
                "Threshold comparison is not strictly greater");
        distribution = {};
        distribution.add(9000);
        distribution.add(12000);
        require(distribution.percentile(.5) == 12000 && distribution.maximum() == 12000,
                "Overflow bin must conservatively preserve the maximum");
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        for (double value : {-1.0, nan, std::numeric_limits<double>::infinity()})
            rejects([&] { distribution.add(value); });
        for (double p : {-1.0, 0.0, 1.1, nan})
            rejects([&] { distribution.percentile(p); });
        std::cout << "Bounded-memory UX metrics verified against exact samples\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
