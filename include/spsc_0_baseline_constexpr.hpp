#pragma once
#include <atomic>
#include <cstddef>
#include <algorithm>

template <typename T, size_t Capacity>
class SPSC_0_Baseline_Constexpr {
public:
    SPSC_0_Baseline_Constexpr() : head_(0), tail_(0) {
        buffer_ = new T[Capacity];
    }
    ~SPSC_0_Baseline_Constexpr() { delete[] buffer_; }

    size_t push_batch(const T* data, size_t count) {
        size_t current_head = head_.load(std::memory_order_relaxed);
        size_t current_tail = tail_.load(std::memory_order_acquire);
        size_t available = Capacity - (current_head - current_tail);
        
        size_t to_push = std::min(count, available);
        for (size_t i = 0; i < to_push; ++i) {
            buffer_[(current_head + i) % Capacity] = data[i];
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
            data[i] = buffer_[(current_tail + i) % Capacity];
        }
        tail_.store(current_tail + to_pop, std::memory_order_release);
        return to_pop;
    }

private:
    T* buffer_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};
