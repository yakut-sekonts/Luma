#include "core/cache/ByteLruCache.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using Cache = luma::ByteLruCache<std::string, std::string>;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::shared_ptr<const std::string> value(const char *text) {
    return std::make_shared<const std::string>(text);
}
void bounds(const Cache &cache, std::size_t bytes, std::size_t count, std::size_t budget) {
    const auto statistics = cache.statistics();
    require(statistics.usedBytes == bytes && statistics.entries == count &&
                statistics.budgetBytes == budget && bytes <= budget,
            "Cache byte/item accounting differs from the independent expected state.");
}
void eviction() {
    Cache cache(12);
    const auto generation = cache.generation();
    cache.put("a", value("first"), 4, generation);
    cache.put("b", value("second"), 4, generation);
    cache.put("c", value("third"), 4, generation);
    bounds(cache, 12, 3, 12);
    const auto active = cache.get("a");
    require(active && *active == "first", "Cache did not return the original immutable value.");
    cache.put("d", value("fourth"), 4, generation);
    require(!cache.get("b") && cache.get("a") == active && cache.get("c") && cache.get("d"),
            "Eviction discarded the recently used value or retained the least recent one.");
    bounds(cache, 12, 3, 12);
    cache.put("a", value("replacement"), 8, generation);
    bounds(cache, 12, 2, 12);
    require(*active == "first" && *cache.get("a") == "replacement",
            "Replacing a cache entry mutated a value retained by the viewport.");
    cache.clear();
    bounds(cache, 0, 0, 12);
    require(*active == "first", "Purge invalidated an externally retained active value.");
}
void generations() {
    Cache cache(12);
    auto generation = cache.generation();
    const auto item = value("immutable");
    cache.put("old", item, 4, generation);
    cache.clear();
    require(cache.generation() != generation, "Purge did not invalidate in-flight retention.");
    cache.put("late", item, 4, generation);
    bounds(cache, 0, 0, 12);
    generation = cache.generation();
    cache.put("new", item, 4, generation);
    bounds(cache, 4, 1, 12);
    cache.setBudget(0);
    cache.put("late", item, 4, generation);
    cache.put("zero", item, 1, cache.generation());
    bounds(cache, 0, 0, 0);
    require(!cache.get("new"), "Zero pressure budget retained optional values.");
    generation = cache.generation();
    cache.setBudget(8);
    cache.put("late", item, 4, generation);
    bounds(cache, 0, 0, 8);
    cache.put("resumed", item, 4, cache.generation());
    require(cache.get("resumed") == item, "Cache retention did not resume after pressure.");
    bounds(cache, 4, 1, 8);
}
void rejectedEntries() {
    Cache cache(8);
    const auto generation = cache.generation();
    const auto item = value("original");
    cache.put("a", item, 4, generation);
    cache.put("a", value("oversize"), 9, generation);
    cache.put("a", {}, 4, generation);
    cache.put("a", value("zero"), 0, generation);
    cache.put("a", value("stale"), 4, generation + 1);
    require(cache.get("a") == item, "Rejected replacement destroyed a valid cached entry.");
    bounds(cache, 4, 1, 8);
    cache.put("b", value("second"), 4, generation);
    cache.setBudget(4);
    require(!cache.get("a") && cache.get("b"), "Budget shrink did not evict the oldest entry.");
    bounds(cache, 4, 1, 4);
}
void integerBoundary() {
    const auto maximum = std::numeric_limits<std::size_t>::max();
    Cache cache(maximum);
    const auto generation = cache.generation();
    // Declared accounting costs, not huge allocations. Check overflow-safe eviction.
    cache.put("large", value("small object"), maximum - 1, generation);
    cache.put("next", value("another small object"), 2, generation);
    require(!cache.get("large") && cache.get("next"), "Byte accounting overflowed at SIZE_MAX.");
    bounds(cache, 2, 1, maximum);
}
} // namespace
int main() {
    try {
        eviction();
        generations();
        rejectedEntries();
        integerBoundary();
        std::cout
            << "Preview cache: LRU/budget, immutable active ownership, stale generations, "
               "pressure purge/resume, invalid replacements and SIZE_MAX accounting passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
