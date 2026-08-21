#pragma once
#include <atomic>
#include <cstddef>
#include <algorithm>
#include <vector>
#include <cstring>

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
        if (to_push == 0) return 0;

        // Use bitmask to find the starting array slot index
        size_t head_index = current_head & BITMASK;
        
        if (head_index + to_push <= Capacity) {
            // Case 1: Contiguous block fits perfectly without wrapping
            std::memcpy(&buffer_[head_index], data, to_push * sizeof(T));
        } else {
            // Case 2: The block wraps around. Execute TWO separate memcpys.
            size_t first_chunk = Capacity - head_index;
            size_t second_chunk = to_push - first_chunk;

            std::memcpy(&buffer_[head_index], data, first_chunk * sizeof(T));
            std::memcpy(&buffer_[0], data + first_chunk, second_chunk * sizeof(T));
        }

        head_.store(current_head + to_push, std::memory_order_release);
        return to_push;
    }

    size_t pop_batch(T* data, size_t max_count) {
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t current_head = head_.load(std::memory_order_acquire);

        size_t available = current_head - current_tail;
        size_t to_pop = std::min(max_count, available);
        if (to_pop == 0) return 0;

        // Use bitmask to find the starting array slot index
        size_t tail_index = current_tail & BITMASK;

        if (tail_index + to_pop <= Capacity) {
            // Case 1: Contiguous block fits perfectly without wrapping
            std::memcpy(data, &buffer_[tail_index], to_pop * sizeof(T));
        } else {
            // Case 2: The block wraps around. Execute TWO separate memcpys.
            size_t first_chunk = Capacity - tail_index;
            size_t second_chunk = to_pop - first_chunk;

            std::memcpy(data, &buffer_[tail_index], first_chunk * sizeof(T));
            std::memcpy(data + first_chunk, &buffer_[0], second_chunk * sizeof(T));
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

