#pragma once
#include <atomic>
#include <cstddef>
#include <algorithm>

template <typename T, size_t Capacity>
class SPSC_2_Cached {
public:
    SPSC_2_Cached() : head_(0), cached_tail_(0), tail_(0), cached_head_(0) {
        buffer_ = new T[Capacity];
    }
    ~SPSC_2_Cached() { delete[] buffer_; }

    size_t push_batch(const T* data, size_t count) {
        size_t current_head = head_.load(std::memory_order_relaxed);
        size_t available = Capacity - (current_head - cached_tail_);
        
        if (available < count) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            available = Capacity - (current_head - cached_tail_);
        }
        
        size_t to_push = std::min(count, available);
        for (size_t i = 0; i < to_push; ++i) {
            buffer_[(current_head + i) % Capacity] = data[i];
        }
        head_.store(current_head + to_push, std::memory_order_release);
        return to_push;
    }

    size_t pop_batch(T* data, size_t max_count) {
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t available = cached_head_ - current_tail;
        
        if (available < max_count) {
            cached_head_ = head_.load(std::memory_order_acquire);
            available = cached_head_ - current_tail;
        }
        
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
    size_t cached_tail_;
    std::atomic<size_t> tail_;
    size_t cached_head_;
};
