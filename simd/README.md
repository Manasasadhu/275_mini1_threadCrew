# simd — AVX2 SIMD Intrinsics Implementation

This folder contains manual AVX2 SIMD implementations of Query 4 (lat/lon bounding box) and Query 5 (average latitude), operating on the contiguous `double` arrays from the Object-of-Arrays (OoA) layout.

## Purpose

This was a targeted experiment to measure whether explicit SIMD vectorization could outperform the compiler's auto-vectorization on numeric-only queries. The OoA layout stores `latitude[]` and `longitude[]` as contiguous `double` arrays — ideal for SIMD processing.

## AVX2 Intrinsics Used

| Intrinsic | Purpose |
|-----------|---------|
| `_mm256_loadu_pd` | Load 4 unaligned doubles into a 256-bit `__m256d` register |
| `_mm256_set1_pd` | Broadcast a scalar to all 4 lanes |
| `_mm256_cmp_pd` | Packed double comparison (returns per-lane mask) |
| `_mm256_and_pd` | Bitwise AND of two `__m256d` masks |
| `_mm256_movemask_pd` | Extract 4-bit integer mask from comparison result |
| `_mm256_add_pd` | Packed double addition (4 additions per instruction) |
| `_mm256_permute2f128_pd` | Swap 128-bit halves for horizontal reduction |
| `_mm256_setzero_pd` | Zero-initialize a 256-bit register |

## Query 4: Lat/Lon Bounding Box (AVX2)

Processes 4 records per iteration:
1. Load 4 latitudes and 4 longitudes into `__m256d` registers
2. Compare against broadcast min/max bounds (4 comparisons)
3. AND all masks together
4. Extract 4-bit mask — each set bit = matching record
5. Scalar tail handles `n % 4` remaining elements

## Query 5: Average Latitude (AVX2)

Accumulates 4 partial sums in parallel:
1. `__m256d` accumulator initialized to zero
2. Each iteration adds 4 doubles with `_mm256_add_pd`
3. Horizontal reduction: permute + add to collapse 4 lanes to 1
4. Scalar tail + divide by `n`

## Build

Requires x86_64 CPU with AVX2 support (Intel Haswell+ / AMD Excavator+):

```bash
g++ -std=c++17 -O2 -mavx2 -mfma -fopenmp -o main \
    main.cpp ../optimized/ServiceRequest.cpp ../optimized/queries.cpp simd_queries.cpp
```

On ARM/Apple Silicon (scalar fallback, no SIMD):

```bash
g++ -std=c++17 -O2 -fopenmp -o main \
    main.cpp ../optimized/ServiceRequest.cpp ../optimized/queries.cpp simd_queries.cpp
```

## Run

```bash
./main [csv_file]
```

## Findings

Manual AVX2 provided a modest speedup for these two numeric queries in isolation. However, the compiler's auto-vectorization under `-O3` with the OoA layout recovered nearly all of the manual SIMD benefit. Since most query bottlenecks are in memory allocation and string handling (workloads SIMD cannot help with), explicit intrinsics were not retained in the final optimized build.
