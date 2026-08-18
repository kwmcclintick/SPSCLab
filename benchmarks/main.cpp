#include <benchmark/benchmark.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include "spsc_0_baseline_constexpr.hpp"
#include "spsc_1_aligned.hpp"
#include "spsc_2_cached.hpp"
#include "spsc_3_bitmask.hpp"
#include "spsc_4_mirrored_mmap.hpp"

constexpr size_t CAPACITY = 65536;
constexpr size_t WORKLOAD_ITEMS = 10'000'000;

// Producer and consumer are pinned to separate physical cores.
constexpr int PRODUCER_CPU = 0;
constexpr int CONSUMER_CPU = 2;

// -----------------------------------------------------------------------------
// CPU pause
// -----------------------------------------------------------------------------

#if defined(__x86_64__) || defined(_M_X64)

inline void cpu_pause() {
    asm volatile("pause");
}

#else

inline void cpu_pause() {}

#endif

// -----------------------------------------------------------------------------
// CPU affinity
// -----------------------------------------------------------------------------

void validate_cpu(int cpu) {
    const long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);

    if (cpu < 0 || cpu >= cpu_count) {
        throw std::runtime_error(
            "CPU " + std::to_string(cpu) +
            " is unavailable. System has " +
            std::to_string(cpu_count) +
            " online CPUs."
        );
    }
}

void pin_thread_to_cpu(std::thread& thread, int cpu) {
    cpu_set_t cpuset;

    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    const int rc = pthread_setaffinity_np(
        thread.native_handle(),
        sizeof(cpu_set_t),
        &cpuset
    );

    if (rc != 0) {
        throw std::runtime_error(
            "pthread_setaffinity_np failed for CPU " +
            std::to_string(cpu) +
            ": " +
            std::to_string(rc)
        );
    }
}

// -----------------------------------------------------------------------------
// Queue construction
// -----------------------------------------------------------------------------

template <typename QueueType>
QueueType create_queue() {
    return QueueType();
}

// -----------------------------------------------------------------------------
// Streaming workload
//
// Producer and consumer operate continuously and concurrently.
//
// There is NO external producer/consumer synchronization after the threads
// start. The queue itself provides all synchronization.
//
// batch_size:
//     Number of items passed to push_batch/pop_batch at a time.
//
// batch=1:
//     True single-item streaming.
//
// batch=256 / 4096:
//     Continuously running batched producer/consumer.
//
// The queue is deliberately allowed to become temporarily full or empty,
// causing the producer/consumer to observe the other side's index.
// -----------------------------------------------------------------------------

template <typename QueueType>
void BM_SPSC_Streaming_Workload(benchmark::State& state) {
    const size_t batch_size = state.range(0);

    validate_cpu(PRODUCER_CPU);
    validate_cpu(CONSUMER_CPU);

    for (auto _ : state) {
        auto queue = create_queue<QueueType>();

        // ---------------------------------------------------------------------
        // Start barrier.
        //
        // This is only used to make producer and consumer begin at roughly
        // the same time. It is NOT involved in the actual queue workload.
        // ---------------------------------------------------------------------

        std::atomic<bool> start{false};

        // ---------------------------------------------------------------------
        // Producer
        // ---------------------------------------------------------------------

        std::thread producer([&]() {
            std::vector<int> write_batch(batch_size, 100);

            while (!start.load(std::memory_order_acquire)) {
                cpu_pause();
            }

            size_t total_pushed = 0;

            while (total_pushed < WORKLOAD_ITEMS) {
                const size_t remaining =
                    WORKLOAD_ITEMS - total_pushed;

                const size_t current_batch =
                    std::min(batch_size, remaining);

                const size_t pushed =
                    queue.push_batch(
                        write_batch.data(),
                        current_batch
                    );

                total_pushed += pushed;

                if (pushed == 0) {
                    cpu_pause();
                }
            }
        });

        // ---------------------------------------------------------------------
        // Consumer
        // ---------------------------------------------------------------------

        std::thread consumer([&]() {
            std::vector<int> read_batch(batch_size, 0);

            while (!start.load(std::memory_order_acquire)) {
                cpu_pause();
            }

            size_t total_popped = 0;

            while (total_popped < WORKLOAD_ITEMS) {
                const size_t remaining =
                    WORKLOAD_ITEMS - total_popped;

                const size_t current_batch =
                    std::min(batch_size, remaining);

                const size_t popped =
                    queue.pop_batch(
                        read_batch.data(),
                        current_batch
                    );

                total_popped += popped;

                if (popped > 0) {
                    for (size_t i = 0; i < popped; ++i) {
                        benchmark::DoNotOptimize(read_batch[i]);
                    }
                } else {
                    cpu_pause();
                }
            }
        });

        // ---------------------------------------------------------------------
        // Pin BEFORE starting the actual workload.
        //
        // Both threads are already waiting on `start`, so this avoids doing
        // meaningful queue work before affinity is established.
        // ---------------------------------------------------------------------

        pin_thread_to_cpu(
            producer,
            PRODUCER_CPU
        );

        pin_thread_to_cpu(
            consumer,
            CONSUMER_CPU
        );

        // Start both threads.
        start.store(
            true,
            std::memory_order_release
        );

        producer.join();
        consumer.join();

        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(
        state.iterations() * WORKLOAD_ITEMS
    );
}

// -----------------------------------------------------------------------------
// Arguments
// -----------------------------------------------------------------------------

static void CustomArguments(
    benchmark::internal::Benchmark* b
) {
    for (int batch_size : {1, 256, 4096}) {
        b->Arg(batch_size);
    }
}

// -----------------------------------------------------------------------------
// Benchmarks
// -----------------------------------------------------------------------------

BENCHMARK(
    BM_SPSC_Streaming_Workload<
        SPSC_0_Baseline_Constexpr<int, CAPACITY>
    >
)
    ->Name("0_Baseline_CompileTimeSize")
    ->Apply(CustomArguments)
    ->UseRealTime();

BENCHMARK(
    BM_SPSC_Streaming_Workload<
        SPSC_1_Aligned<int, CAPACITY>
    >
)
    ->Name("1_Optimization_CacheLineAlignedOnly")
    ->Apply(CustomArguments)
    ->UseRealTime();

BENCHMARK(
    BM_SPSC_Streaming_Workload<
        SPSC_2_Cached<int, CAPACITY>
    >
)
    ->Name("2_Optimization_IndexCachedOnly")
    ->Apply(CustomArguments)
    ->UseRealTime();

BENCHMARK(
    BM_SPSC_Streaming_Workload<
        SPSC_3_Bitmask<int, CAPACITY>
    >
)
    ->Name("3_Optimization_BitmaskOnly")
    ->Apply(CustomArguments)
    ->UseRealTime();

BENCHMARK(
    BM_SPSC_Streaming_Workload<
        SPSC_4_MirroredMmap<int, CAPACITY>
    >
)
    ->Name("4_Optimization_MirroredMmapOnly")
    ->Apply(CustomArguments)
    ->UseRealTime();

BENCHMARK_MAIN();
