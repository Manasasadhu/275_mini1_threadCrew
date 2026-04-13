#pragma once
// ---------------------------------------------------------------------------
// simd/simd_queries.h
//
// Manual AVX2 SIMD implementations of Query 4 (lat/lon bounding box) and
// Query 5 (average latitude) operating on contiguous double arrays from the
// Object-of-Arrays layout.
//
// AVX2 packs 4 doubles into a 256-bit __m256d register, allowing 4
// comparisons or additions per instruction.
//
// Build (x86_64 with AVX2 support):
//   g++ -std=c++17 -O2 -mavx2 -mfma -fopenmp -o main main.cpp ServiceRequest.cpp queries.cpp simd_queries.cpp
//
// NOTE: This code requires an x86_64 CPU with AVX2 support (Intel Haswell+
// or AMD Excavator+). It will NOT compile on ARM/Apple Silicon without
// modification. See the #ifdef guards below.
// ---------------------------------------------------------------------------

#include <vector>
#include <cstddef>

#if defined(__x86_64__) || defined(_M_X64)
  #define SIMD_AVX2_AVAILABLE 1
  #include <immintrin.h>   // AVX2 intrinsics: __m256d, _mm256_*
#else
  #define SIMD_AVX2_AVAILABLE 0
#endif

// Forward declaration of OoA struct (defined in ServiceRequest.h)
struct ServiceRequestOoA;

// ---------------------------------------------------------------------------
// Query 4 — Lat/Lon Bounding Box using AVX2 SIMD
//
// Processes 4 latitude and 4 longitude values per iteration using 256-bit
// registers. Each __m256d holds 4 packed doubles. The bounding box check
// (lat >= minLat && lat <= maxLat && lon >= minLon && lon <= maxLon) is
// performed with _mm256_cmp_pd and _mm256_and_pd, producing a 4-bit mask
// via _mm256_movemask_pd. Matching indices are gathered from the mask.
//
// Scalar fallback handles the tail (n % 4 remaining elements).
// ---------------------------------------------------------------------------
std::vector<std::size_t> filterByLatLonBoxOoA_avx2(
    const ServiceRequestOoA& data,
    double minLat, double maxLat,
    double minLon, double maxLon
);

// ---------------------------------------------------------------------------
// Query 5 — Average Latitude using AVX2 SIMD
//
// Accumulates 4 doubles per iteration into a __m256d accumulator using
// _mm256_add_pd. After the main loop, the 4 lanes are reduced to a single
// sum with a horizontal add pattern. Scalar fallback handles the tail.
// ---------------------------------------------------------------------------
double averageLatitudeOoA_avx2(const ServiceRequestOoA& data);
