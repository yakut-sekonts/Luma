#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>

namespace luma {
struct CacheStatistics {
    std::size_t usedBytes = 0, budgetBytes = 0, entries = 0;
};
// The budget bounds cache-owned references. Active jobs may retain a value
// after eviction; releasing their references remains the caller's responsibility.
template <class Key, class Value> class ByteLruCache {
  public:
    explicit ByteLruCache(std::size_t budget) : budget_(budget) {}

    std::shared_ptr<const Value> get(const Key &key) {
        std::lock_guard lock(mutex_);
        const auto found = entries_.find(key);
        if (found == entries_.end())
            return {};
        order_.splice(order_.begin(), order_, found->second.position);
        return found->second.value;
    }

    std::uint64_t generation() const {
        std::lock_guard lock(mutex_);
        return generation_;
    }

    CacheStatistics statistics() const {
        std::lock_guard lock(mutex_);
        return {used_, budget_, entries_.size()};
    }

    void put(const Key &key, std::shared_ptr<const Value> value, std::size_t bytes,
             std::uint64_t generation) {
        std::lock_guard lock(mutex_);
        if (!value || bytes == 0 || bytes > budget_ || generation != generation_)
            return;
        if (const auto found = entries_.find(key); found != entries_.end()) {
            used_ -= found->second.bytes;
            order_.erase(found->second.position);
            entries_.erase(found);
        }
        while (used_ > budget_ - bytes)
            evictOldest();
        bool insertedOrder = false;
        try {
            order_.push_front(key);
            insertedOrder = true;
            entries_.emplace(key, Entry{std::move(value), bytes, order_.begin()});
        } catch (const std::bad_alloc &) {
            if (insertedOrder)
                order_.pop_front();
            // Cache retention is optional; a successfully rendered image or
            // opened photo must remain usable when retention cannot allocate.
            return;
        } catch (...) {
            if (insertedOrder)
                order_.pop_front();
            throw;
        }
        used_ += bytes;
    }

    void setBudget(std::size_t bytes) {
        std::lock_guard lock(mutex_);
        ++generation_;
        budget_ = bytes;
        while (used_ > budget_)
            evictOldest();
    }

    void clear() {
        std::lock_guard lock(mutex_);
        ++generation_;
        entries_.clear();
        order_.clear();
        used_ = 0;
    }

  private:
    struct Entry {
        std::shared_ptr<const Value> value;
        std::size_t bytes;
        typename std::list<Key>::iterator position;
    };
    void evictOldest() {
        const auto found = entries_.find(order_.back());
        used_ -= found->second.bytes;
        entries_.erase(found);
        order_.pop_back();
    }
    mutable std::mutex mutex_;
    std::size_t budget_, used_ = 0;
    std::uint64_t generation_ = 0;
    std::list<Key> order_;
    std::unordered_map<Key, Entry> entries_;
};
} // namespace luma
