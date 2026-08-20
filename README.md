# SPSCLab
SPSCLab measures how common SPSC ring-buffer optimizations affect throughput under continuous producer/consumer workloads running on separate physical CPU cores.
## Results

| Optimization | Batch 1 | Batch 256 | Batch 4096 |
|---|---|---|---|
| Baseline | 50.0 M/s | 1.07 G/s | 1.23 G/s |
| Cache-line aligned | 67.4 M/s | 1.09 G/s | 1.25 G/s |
| Cached indices | 52.3 M/s | 1.29 G/s | 1.49 G/s |
| Bitmask | 47.9 M/s | 1.20 G/s | 1.52 G/s |
| Mirrored mmap | 25.5 M/s | 1.58 G/s | 2.40 G/s |

## Lessons
### 1. Cache-line alignment
Separating head and tail prevents producer and consumer from invalidating each other's cache line. It helps most for single-item operations: 50.0 → 67.4 M/s (~35%)
With larger batches, each index update is amortized over many items, so the coherence cost becomes a much smaller fraction of total work.
### 2. Cached indices
The cached-index implementation avoids repeatedly reading the other thread's index:
```cpp
// Baseline
tail = tail_.load(std::memory_order_acquire);
```
becomes conceptually:
```cpp
// Cachedif (queue_might_be_full)
    cached_tail = tail_.load(std::memory_order_acquire);
```
This can be surprisingly effective with batching. A batch operation publishes the local index only once, but the producer may still perform multiple batches while the consumer's index has not changed enough to require a fresh value. The cached index lets the producer continue using a locally-held value instead of repeatedly touching the consumer-owned cache line. Similarly, the consumer can continue using a cached head. This is reflected in the results: 1.23 → 1.49 G/s (~21%) at batch 4096.
The important distinction is that caching reduces cross-core metadata traffic, while batching reduces how often that metadata needs to be published. The two optimizations can therefore complement each other.
### 3. Bitmask
For power-of-two capacities:

`index % Capacity`

can be replaced with:

`index & (Capacity - 1)`

To confirm whether the compiler automatically optimizes the modulo operator, perf stat was run comparing Cached Indices (Baseline) vs Bitmask at Batch 1:

```bash
perf stat -r 10 -e cycles,instructions,l1-dcache-loads,l1-dcache-load-misses \
  ./spsc_bench --benchmark_filter='^2_Optimization_IndexCachedOnly/1/real_time$' --benchmark_min_time=5s
# Variant: Bitmask
perf stat -r 10 -e cycles,instructions \
  ./spsc_bench --benchmark_filter='^3_Optimization_BitmaskOnly/1/real_time$' --benchmark_min_time=5s
```

* Baseline (Cached): 38,134,583,126 instructions | 76,199,407,554 cycles
* Bitmask Variant: 37,383,959,845 instructions | 71,619,518,594 cycles

The explicit Bitmask variant yielded only a minor 1.9% instruction reduction and nearly identical execution cycles. This confirms the hypothesis: because Capacity is a compile-time constant power-of-two, the compiler's optimization pass had already converted the modulo operator (%) into an identical bitwise AND (&) sequence under the hood.

### 4. Mirrored mmap
Mirroring the ring buffer in virtual memory makes a wrapped batch appear contiguous, completely eliminating explicit wraparound handling. It dominates for large batches: 1.23 → 2.40 G/s (~95%) at batch 4096.

The primary hypothesis states that Mirrored MMAP does not merely accelerate the wraparound event itself (which only occurs once every 16 batches at Batch 4096). Instead, it frees the compiler to optimize the straight-line code path for every single operation by removing all conditional split-copy logic.

```bash
perf stat -r 10 -e cycles,instructions,branch-loads,l1-icache-load-misses \
  ./spsc_bench --benchmark_filter='^3_Optimization_BitmaskOnly/4096/real_time$' --benchmark_min_time=5s
# Variant: Mirrored MMAP
perf stat -r 10 -e cycles,instructions,branch-loads,l1-icache-load-misses \
  ./spsc_bench --benchmark_filter='^4_Optimization_MirroredMmapOnly/4096/real_time$' --benchmark_min_time=5s
```

* Baseline (Bitmask): 173,703,534,267 instructions | 32,071,529,301 branches
* Mirrored MMAP: 56,483,717,630 instructions | 12,274,578,679 branches

The data reveals a massive 67.4% drop in total instructions and an elimination of 19.79 Billion branches in the Mirrored MMAP variant.
Because a wraparound only happens on 6.25% of the iterations (1 out of 16), this scale of instruction reduction proves that the non-wrapping paths became vastly more efficient. By guaranteeing a single, un-fragmented contiguous memory destination, the compiler was able to drop conditional boundary safety branches, tightly unroll data loops, and fully leverage hardware vector registers (SIMD/AVX) to move large data payloads in fewer clock cycles.
The technique is less attractive for single-item operations, where its additional complexity and setup costs are not offset by contiguous bulk access.

## Takeaway
Different optimizations attack different costs:

* Alignment: reduces cache-line contention.
* Cached indices: reduces cross-core index traffic.
* Bitmask: reduces index calculation overhead.
* Mirrored mmap: eliminates wraparound overhead for large contiguous batches by unleashing aggressive compiler vectorization.

The results suggest that cached indices and batching reinforce each other: batching makes each index update more valuable to amortize, while cached indices reduce unnecessary reads of the producer/consumer's remotely-owned metadata.
## Build and Run

   1. Verify that your system has a local installation of the Google Benchmark framework.
   2. Compile the suite using the standard Release profile to enable compiler optimizations:
   
   g++ -O3 -DNDEBUG -std=c++17 main.cpp -lbenchmark -lpthread -o spsc_bench
   
   3. Run the benchmark tool:
   
   ./spsc_bench
   
   
## References

   1. [Memory Magic Part 4: The Infinite Buffer](https://andreleite.com/posts/2025/nstl/virtual-memory-ring-buffer/)
   2. [What Every Programmer Should Know About Memory](https://liuyehcf.github.io/resources/paper/What-Every-Programmer%E2%80%93Should-Know-About-Memory.pdf)
   3. [Google Benchmark GitHub Repository](https://github.com/google/benchmark)

