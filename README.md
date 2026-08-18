# SPSCLab

SPSCLab measures how common SPSC ring-buffer optimizations affect throughput
under continuous producer/consumer workloads running on separate physical
CPU cores.

## Results

| Optimization | Batch 1 | Batch 256 | Batch 4096 |
|---|---:|---:|---:|
| Baseline | 50.0 M/s | 1.07 G/s | 1.23 G/s |
| Cache-line aligned | **67.4 M/s** | 1.09 G/s | 1.25 G/s |
| Cached indices | 52.3 M/s | **1.29 G/s** | **1.49 G/s** |
| Bitmask | 47.9 M/s | 1.20 G/s | 1.52 G/s |
| Mirrored mmap | 25.5 M/s | **1.58 G/s** | **2.40 G/s** |

## Lessons

### 1. Cache-line alignment

Separating `head` and `tail` prevents producer and consumer from invalidating
each other's cache line.

It helps most for single-item operations:

**50.0 → 67.4 M/s (~35%)**

With larger batches, each index update is amortized over many items, so the
coherence cost becomes a much smaller fraction of total work.

### 2. Cached indices

The cached-index implementation avoids repeatedly reading the other thread's
index:

```cpp
// Baseline
tail = tail_.load(std::memory_order_acquire);
```

becomes conceptually:

```cpp
// Cached
if (queue_might_be_full)
    cached_tail = tail_.load(std::memory_order_acquire);
```

This can be surprisingly effective with batching.

A batch operation publishes the local index only once, but the producer may
still perform multiple batches while the consumer's index has not changed
enough to require a fresh value. The cached index lets the producer continue
using a locally-held value instead of repeatedly touching the consumer-owned
cache line.

Similarly, the consumer can continue using a cached `head`.

This is reflected in the results:

**1.23 → 1.49 G/s (~21%) at batch 4096.**

The important distinction is that caching reduces **cross-core metadata
traffic**, while batching reduces how often that metadata needs to be
published. The two optimizations can therefore complement each other.

### 3. Bitmask

For power-of-two capacities:

```cpp
index % Capacity
```

can be replaced with:

```cpp
index & (Capacity - 1)
```

The benefit is workload-dependent because the compiler can often optimize
constant modulo operations itself.

### 4. Mirrored mmap

Mirroring the ring buffer in virtual memory makes a wrapped batch appear
contiguous, eliminating explicit wraparound handling.

It dominates for large batches:

**1.23 → 2.40 G/s (~95%) at batch 4096.**

The technique is less attractive for single-item operations, where its
additional complexity and setup costs are not offset by contiguous bulk
access.

## Takeaway

Different optimizations attack different costs:

* **Alignment:** reduces cache-line contention.
* **Cached indices:** reduces cross-core index traffic.
* **Bitmask:** reduces index calculation overhead.
* **Mirrored mmap:** eliminates wraparound overhead for large contiguous
  batches.

The results suggest that **cached indices and batching reinforce each other**:
batching makes each index update more valuable to amortize, while cached indices
reduce unnecessary reads of the producer/consumer's remotely-owned metadata.


## Build and Run

   1. Verify that your system has a local installation of the Google Benchmark framework.
   2. Compile and run the suite using the standard Release profile `./build.sh`

Then run `./spsc_bench`

## References

   1. [Memory Magic Part 4: The Infinite Buffer](https://andreleite.com/posts/2025/nstl/virtual-memory-ring-buffer/)
   2. [What Every Programmer Should Know About Memory](https://liuyehcf.github.io/resources/paper/What-Every-Programmer%E2%80%93Should-Know-About-Memory.pdf)
   3. [Google Benchmark GitHub Repository](https://github.com/google/benchmark)


