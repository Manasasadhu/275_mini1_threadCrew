// ---------------------------------------------------------------------------
// simd/main.cpp
//
// Benchmarks AVX2 SIMD implementations of Query 4 (lat/lon bounding box)
// and Query 5 (average latitude) against the scalar OpenMP versions.
//
// This demonstrates manual vectorization using Intel AVX2 intrinsics on
// the Object-of-Arrays (OoA) memory layout. The contiguous double arrays
// (latitude[], longitude[]) are ideal for SIMD: 4 doubles fit in one
// 256-bit YMM register, allowing 4 comparisons or additions per cycle.
//
// Build (x86_64):
//   g++ -std=c++17 -O2 -mavx2 -mfma -fopenmp -o main \
//       main.cpp ../optimized/ServiceRequest.cpp ../optimized/queries.cpp simd_queries.cpp
//
// Build (ARM/Apple Silicon — scalar fallback):
//   g++ -std=c++17 -O2 -fopenmp -o main \
//       main.cpp ../optimized/ServiceRequest.cpp ../optimized/queries.cpp simd_queries.cpp
//
// Run:
//   ./main [csv_file]
// ---------------------------------------------------------------------------

#include "../optimized/ServiceRequest.h"
#include "../optimized/queries.h"
#include "simd_queries.h"

#include <iostream>
#include <chrono>
#include <string>
#include <vector>
#include <iomanip>

template <typename T>
inline void doNotOptimize(const T& value) {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : "g"(value) : "memory");
#else
    volatile const T* p = &value; (void)p;
#endif
}

template <typename Fn>
double timeit(const std::string& label, int runs, Fn fn) {
    // Warmup
    auto first = fn();
    doNotOptimize(first);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < runs; ++i) {
        auto r = fn();
        doNotOptimize(r);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double total = std::chrono::duration<double>(end - start).count();
    double avg = total / runs;
    std::cout << "  " << label << ": total=" << total << "s, avg=" << avg << "s\n";
    return avg;
}

int main(int argc, char* argv[]) {
    std::string filename =
        (argc > 1) ? argv[1]
                   : "/Users/aravindreddy/Downloads/SJSU ClassWork/275 EAD/Mini1_Datasets/311_combined.csv.";

    ServiceRequestOoA data;
    std::cout << "[LOAD] Loading data...\n";
    auto t0 = std::chrono::high_resolution_clock::now();
    bool ok = loadServiceRequestOoA(filename, data);
    auto t1 = std::chrono::high_resolution_clock::now();

    if (!ok) { std::cerr << "Failed to load data.\n"; return 1; }

    std::cout << "[LOAD] " << data.uniqueKey.size() << " records in "
              << std::chrono::duration<double>(t1 - t0).count() << "s\n";
    std::cout << std::fixed << std::setprecision(6);

#if SIMD_AVX2_AVAILABLE
    std::cout << "\n[INFO] AVX2 SIMD is AVAILABLE on this platform.\n";
#else
    std::cout << "\n[INFO] AVX2 not available (non-x86). Using scalar fallback.\n";
#endif

    const int runs = 15;

    // =================================================================
    // Query 4: Lat/Lon Bounding Box — AVX2 vs OpenMP scalar
    // =================================================================
    std::cout << "\n=== Query 4: Lat/Lon Bounding Box ===\n";
    std::cout << "Bounding box: lat=[40.5, 40.9], lon=[-74.25, -73.7]\n\n";

    double avgOmp4 = timeit("OpenMP (scalar)", runs, [&]() {
        return filterByLatLonBoxOoA(data, 40.5, 40.9, -74.25, -73.7);
    });

    double avgAvx4 = timeit("AVX2 SIMD", runs, [&]() {
        return filterByLatLonBoxOoA_avx2(data, 40.5, 40.9, -74.25, -73.7);
    });

    // Verify result counts match
    auto resOmp4 = filterByLatLonBoxOoA(data, 40.5, 40.9, -74.25, -73.7);
    auto resAvx4 = filterByLatLonBoxOoA_avx2(data, 40.5, 40.9, -74.25, -73.7);
    std::cout << "\n  OpenMP result size: " << resOmp4.size() << "\n";
    std::cout << "  AVX2  result size:  " << resAvx4.size() << "\n";
    if (resOmp4.size() == resAvx4.size())
        std::cout << "  [OK] Result sizes match.\n";
    else
        std::cout << "  [MISMATCH] Results differ!\n";

    std::cout << "\n  Speedup (AVX2 vs OpenMP): " << (avgOmp4 / avgAvx4) << "x\n";

    // =================================================================
    // Query 5: Average Latitude — AVX2 vs OpenMP scalar
    // =================================================================
    std::cout << "\n=== Query 5: Average Latitude ===\n\n";

    double avgOmp5 = timeit("OpenMP (reduction)", runs, [&]() {
        return averageLatitudeOoA_omp(data);
    });

    double avgAvx5 = timeit("AVX2 SIMD", runs, [&]() {
        return averageLatitudeOoA_avx2(data);
    });

    double valOmp = averageLatitudeOoA_omp(data);
    double valAvx = averageLatitudeOoA_avx2(data);
    std::cout << "\n  OpenMP result: " << valOmp << "\n";
    std::cout << "  AVX2  result:  " << valAvx << "\n";
    double diff = std::abs(valOmp - valAvx);
    std::cout << "  Difference:    " << diff;
    if (diff < 1e-6) std::cout << " [OK - within tolerance]\n";
    else             std::cout << " [WARNING - divergence]\n";

    std::cout << "\n  Speedup (AVX2 vs OpenMP): " << (avgOmp5 / avgAvx5) << "x\n";

    std::cout << "\n=== Done ===\n";
    return 0;
}
