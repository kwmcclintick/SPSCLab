#pragma once
#include <atomic>
#include <cstddef>
#include <algorithm>
#include <vector>

template <typename T, size_t Capacity>
class SPSC_3_Bitmask {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two.");
public:
    SPSC_3_Bitmask() : head_(0), tail_(0) {
        buffer_.resize(Capacity);
    }

    size_t push_batch(const T* data, size_t count) {
        size_t current_head = head_.load(std::memory_order_relaxed);
        size_t current_tail = tail_.load(std::memory_order_acquire);
        size_t available = Capacity - (current_head - current_tail);
        
        size_t to_push = std::min(count, available);
        for (size_t i = 0; i < to_push; ++i) {
            buffer_[(current_head + i) & BITMASK] = data[i];
        }
        head_.store(current_head + to_push, std::memory_order_release);
        return to_push;
    }

    size_t pop_batch(T* data, size_t max_count) {
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t current_head = head_.load(std::memory_order_acquire);
        size_t available = current_head - current_tail;
        
        size_t to_pop = std::min(max_count, available);
        for (size_t i = 0; i < to_pop; ++i) {
            data[i] = buffer_[(current_tail + i) & BITMASK];
        }
        tail_.store(current_tail + to_pop, std::memory_order_release);
        return to_pop;
    }

private:
    static constexpr size_t BITMASK = Capacity - 1;
    std::vector<T> buffer_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};
