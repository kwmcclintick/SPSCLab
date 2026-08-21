
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
    // 1. Enforce that Capacity is a power of two at compile time
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two.");
    // 2. Enforce that the total byte size matches or exceeds a standard 4096-byte page size
    static_assert((Capacity * sizeof(T)) >= 4096, "Capacity in bytes must be at least one page size (4096 bytes).");

public:
    SPSC_4_MirroredMmap() : head_(0), tail_(0) {
        size_t bytes = Capacity * sizeof(T);
        size_t page_size = sysconf(_SC_PAGESIZE);
        
        // Runtime check to make sure the system page size matches our compile-time assumption
        if (page_size != 4096 || (bytes % page_size) != 0) {
            throw std::runtime_error("Capacity does not align perfectly with hardware page size configuration.");
        }

        buffer_bytes_ = bytes;
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

        size_t available = Capacity - (current_head - current_tail);
        size_t to_push = std::min(count, available);
        if (to_push == 0) return 0;

        // FIXED: Using % Capacity instead of a member variable. 
        // Because Capacity is a compile-time power-of-two, the compiler completely eliminates 'div'
        std::memcpy(&buffer_[current_head % Capacity], data, to_push * sizeof(T));
        head_.store(current_head + to_push, std::memory_order_release);
        return to_push;
    }

    size_t pop_batch(T* data, size_t max_count) {
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t current_head = head_.load(std::memory_order_acquire);

        size_t available = current_head - current_tail;
        size_t to_pop = std::min(max_count, available);
        if (to_pop == 0) return 0;

        // FIXED: Using % Capacity instead of a member variable.
        std::memcpy(data, &buffer_[current_tail % Capacity], to_pop * sizeof(T));
        tail_.store(current_tail + to_pop, std::memory_order_release);
        return to_pop;
    }

private:
    T* buffer_;
    size_t buffer_bytes_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

