#pragma once
#include <atomic>
#include <cstddef>
#include <algorithm>
#include <cstring>

template <typename T, size_t Capacity>
class SPSC_2_Cached {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two.");

public:
    SPSC_2_Cached() : head_(0), cached_tail_(0), tail_(0), cached_head_(0) {
        buffer_ = new T[Capacity];
    }

    ~SPSC_2_Cached() {
        delete[] buffer_;
    }

    size_t push_batch(const T* data, size_t count) {
        size_t current_head = head_.load(std::memory_order_relaxed);
        size_t available = Capacity - (current_head - cached_tail_);

        if (available < count) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            available = Capacity - (current_head - cached_tail_);
        }

        size_t to_push = std::min(count, available);
        if (to_push == 0) return 0;

        size_t head_index = current_head % Capacity;

        if (head_index + to_push <= Capacity) {
            // Case 1: Contiguous block fits perfectly without wrapping
            std::memcpy(&buffer_[head_index], data, to_push * sizeof(T));
        } else {
            // Case 2: The block wraps around. Execute TWO separate memcpys.
            size_t first_chunk = Capacity - head_index;
            size_t second_chunk = to_push - first_chunk;

            std::memcpy(&buffer_[head_index], data, first_chunk * sizeof(T));
            std::memcpy(buffer_, data + first_chunk, second_chunk * sizeof(T));
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
        if (to_pop == 0) return 0;

        size_t tail_index = current_tail % Capacity;

        if (tail_index + to_pop <= Capacity) {
            // Case 1: Contiguous block fits perfectly without wrapping
            std::memcpy(data, &buffer_[tail_index], to_pop * sizeof(T));
        } else {
            // Case 2: The block wraps around. Execute TWO separate memcpys.
            size_t first_chunk = Capacity - tail_index;
            size_t second_chunk = to_pop - first_chunk;

            std::memcpy(data, &buffer_[tail_index], first_chunk * sizeof(T));
            std::memcpy(data + first_chunk, buffer_, second_chunk * sizeof(T));
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

