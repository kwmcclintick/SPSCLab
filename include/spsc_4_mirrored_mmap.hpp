#pragma once
#include <atomic>
#include <cstddef>
#include <algorithm>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>

#ifndef CONST_MEMFD
#define CONST_MEMFD
#include <sys/syscall.h>
#ifdef __linux__
#include <linux/memfd.h>
#endif
#endif

template <typename T, size_t Capacity>
class SPSC_4_MirroredMmap {
public:
    SPSC_4_MirroredMmap() : head_(0), tail_(0) {
        size_t bytes = Capacity * sizeof(T);
        size_t page_size = sysconf(_SC_PAGESIZE);
        buffer_bytes_ = ((bytes + page_size - 1) / page_size) * page_size;
        real_capacity_ = buffer_bytes_ / sizeof(T);

        int fd = memfd_create("spsc_mirrored_buffer", MFD_CLOEXEC);
        if (fd == -1 || ftruncate(fd, buffer_bytes_) == -1) {
            throw std::runtime_error("Failed to setup anonymous file storage layout.");
        }

        void* target_hint = mmap(nullptr, 2 * buffer_bytes_, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (target_hint == MAP_FAILED) throw std::runtime_error("Virtual address space exhaustion.");

        void* r1 = mmap(target_hint, buffer_bytes_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
        void* r2 = mmap(static_cast<char*>(target_hint) + buffer_bytes_, buffer_bytes_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
        
        close(fd);
        if (r1 == MAP_FAILED || r2 == MAP_FAILED) throw std::runtime_error("mmap mirroring execution failed.");
        buffer_ = static_cast<T*>(r1);
    }

    ~SPSC_4_MirroredMmap() {
        munmap(buffer_, 2 * buffer_bytes_);
    }

    size_t push_batch(const T* data, size_t count) {
        size_t current_head = head_.load(std::memory_order_relaxed);
        size_t current_tail = tail_.load(std::memory_order_acquire);
        size_t available = real_capacity_ - (current_head - current_tail);
        
        size_t to_push = std::min(count, available);
        if (to_push == 0) return 0;

        std::memcpy(&buffer_[current_head % real_capacity_], data, to_push * sizeof(T));
        head_.store(current_head + to_push, std::memory_order_release);
        return to_push;
    }

    size_t pop_batch(T* data, size_t max_count) {
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t current_head = head_.load(std::memory_order_acquire);
        size_t available = current_head - current_tail;
        
        size_t to_pop = std::min(max_count, available);
        if (to_pop == 0) return 0;

        std::memcpy(data, &buffer_[current_tail % real_capacity_], to_pop * sizeof(T));
        tail_.store(current_tail + to_pop, std::memory_order_release);
        return to_pop;
    }

private:
    T* buffer_;
    size_t buffer_bytes_;
    size_t real_capacity_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};
