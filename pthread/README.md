# pthread — POSIX Threads Implementation

This folder contains a raw POSIX threads (`pthread`) implementation of the NYC 311 query engine, replacing all OpenMP pragmas with explicit `pthread_create` / `pthread_join` calls.

## Purpose

This was an early prototype to evaluate whether manual thread management offered any performance advantage over OpenMP's higher-level abstractions. The conclusion: runtimes were indistinguishable from the OpenMP version across all six queries, while the code was significantly more verbose. OpenMP was retained for the final multi-thread implementation.

## Key Differences from OpenMP Version

- Thread creation: `pthread_create()` / `pthread_join()` instead of `#pragma omp parallel for`
- Work partitioning: manual index range calculation per thread (`threadRange()`)
- Thread-local results: each thread writes to its own struct field, merged after join
- No OpenMP dependency — links only against `-lpthread` (included in `-pthread` flag)

## Query Implementations

All six queries follow the same pattern:
1. Define a per-thread argument struct with `tid` and a local result container
2. Spawn `T` threads, each processing indices `[start, end)` of `g_records`
3. Join all threads
4. Merge thread-local results sequentially

## Build

```bash
g++ -std=c++17 -O2 -pthread -o main main.cpp ServiceRequest.cpp
```

## Run

```bash
./main [csv_file] [num_threads]
```

- `csv_file` (optional): Path to the NYC 311 CSV file
- `num_threads` (optional): Number of threads (default: 4)
