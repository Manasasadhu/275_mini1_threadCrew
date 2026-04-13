// ---------------------------------------------------------------------------
// simd/simd_queries.cpp
//
// AVX2 SIMD implementations of Query 4 (lat/lon bounding box) and
// Query 5 (average latitude).
//
// These operate on the contiguous double arrays from the OoA layout,
// processing 4 doubles per clock cycle using 256-bit YMM registers.
//
// Key AVX2 intrinsics used:
//   _mm256_loadu_pd   — load 4 unaligned doubles into __m256d
//   _mm256_set1_pd    — broadcast a scalar to all 4 lanes
//   _mm256_cmp_pd     — packed double comparison (returns mask)
//   _mm256_and_pd     — bitwise AND of two __m256d masks
//   _mm256_movemask_pd — extract 4-bit mask from comparison result
//   _mm256_add_pd     — packed double addition
//
// Build:
//   g++ -std=c++17 -O2 -mavx2 -mfma -fopenmp -o main \
//       main.cpp ServiceRequest.cpp queries.cpp simd_queries.cpp
// ---------------------------------------------------------------------------

#include "simd_queries.h"
#include "../optimized/ServiceRequest.h"

#include <iostream>
#include <cstddef>

#if SIMD_AVX2_AVAILABLE
// ===================================================================
// AVX2 implementation — x86_64 only
// ===================================================================

// -------------------------------------------------------------------
// Query 4: Lat/Lon Bounding Box (AVX2)
//
// Strategy:
//   1. Broadcast min/max lat/lon into __m256d registers (4 copies each)
//   2. Load 4 latitudes and 4 longitudes per iteration
//   3. Perform 4 comparisons: lat >= minLat, lat <= maxLat,
//      lon >= minLon, lon <= maxLon
//   4. AND all 4 masks together
//   5. Extract 4-bit integer mask with _mm256_movemask_pd
//   6. Each set bit indicates a matching record
// -------------------------------------------------------------------
std::vector<std::size_t> filterByLatLonBoxOoA_avx2(
    const ServiceRequestOoA& data,
    double minLat, double maxLat,
    double minLon, double maxLon)
{
    const std::size_t n = data.latitude.size();
    std::vector<std::size_t> out;
    if (n == 0) return out;

    const double* lat = data.latitude.data();
    const double* lon = data.longitude.data();

    // Broadcast bounding box limits to all 4 lanes of 256-bit registers
    __m256d vMinLat = _mm256_set1_pd(minLat);
    __m256d vMaxLat = _mm256_set1_pd(maxLat);
    __m256d vMinLon = _mm256_set1_pd(minLon);
    __m256d vMaxLon = _mm256_set1_pd(maxLon);

    // Process 4 records at a time (4 doubles = 256 bits)
    std::size_t i = 0;
    const std::size_t nVec = n - (n % 4);   // aligned end

    for (; i < nVec; i += 4) {
        // Load 4 latitudes and 4 longitudes (unaligned load)
        __m256d vLat = _mm256_loadu_pd(lat + i);
        __m256d vLon = _mm256_loadu_pd(lon + i);

        // Compare: lat >= minLat  (each lane: all-ones if true, all-zeros if false)
        __m256d cmpLatGe = _mm256_cmp_pd(vLat, vMinLat, _CMP_GE_OQ);
        // Compare: lat <= maxLat
        __m256d cmpLatLe = _mm256_cmp_pd(vLat, vMaxLat, _CMP_LE_OQ);
        // Compare: lon >= minLon
        __m256d cmpLonGe = _mm256_cmp_pd(vLon, vMinLon, _CMP_GE_OQ);
        // Compare: lon <= maxLon
        __m256d cmpLonLe = _mm256_cmp_pd(vLon, vMaxLon, _CMP_LE_OQ);

        // AND all four conditions together
        __m256d mask = _mm256_and_pd(
            _mm256_and_pd(cmpLatGe, cmpLatLe),
            _mm256_and_pd(cmpLonGe, cmpLonLe)
        );

        // Extract 4-bit mask: bit j is set if record (i+j) matches
        int bits = _mm256_movemask_pd(mask);

        // Gather matching indices from the bitmask
        if (bits) {
            if (bits & 1) out.push_back(i);
            if (bits & 2) out.push_back(i + 1);
            if (bits & 4) out.push_back(i + 2);
            if (bits & 8) out.push_back(i + 3);
        }
    }

    // Scalar tail: handle remaining elements (n % 4)
    for (; i < n; ++i) {
        if (lat[i] >= minLat && lat[i] <= maxLat &&
            lon[i] >= minLon && lon[i] <= maxLon) {
            out.push_back(i);
        }
    }

    return out;
}

// -------------------------------------------------------------------
// Query 5: Average Latitude (AVX2)
//
// Strategy:
//   1. Accumulate 4 partial sums in a __m256d register
//   2. Each iteration adds 4 doubles with _mm256_add_pd
//   3. After the loop, horizontally reduce the 4 lanes to one sum
//   4. Add scalar tail, divide by n
// -------------------------------------------------------------------
double averageLatitudeOoA_avx2(const ServiceRequestOoA& data) {
    const std::size_t n = data.latitude.size();
    if (n == 0) return 0.0;

    const double* lat = data.latitude.data();

    // Accumulator: 4 partial sums in parallel
    __m256d vSum = _mm256_setzero_pd();

    std::size_t i = 0;
    const std::size_t nVec = n - (n % 4);

    for (; i < nVec; i += 4) {
        __m256d vLat = _mm256_loadu_pd(lat + i);
        vSum = _mm256_add_pd(vSum, vLat);
    }

    // Horizontal reduction: sum the 4 lanes of vSum
    // vSum = [a, b, c, d]
    // Step 1: hadd -> [a+b, a+b, c+d, c+d]  (not exact, use permute)
    __m256d vHigh = _mm256_permute2f128_pd(vSum, vSum, 0x01);  // swap 128-bit halves
    __m256d vAdd1 = _mm256_add_pd(vSum, vHigh);                // [a+c, b+d, ...]
    __m256d vShuf = _mm256_shuffle_pd(vAdd1, vAdd1, 0x05);     // swap within 128-bit lanes
    __m256d vAdd2 = _mm256_add_pd(vAdd1, vShuf);               // [a+b+c+d, ...]

    double sum;
    _mm256_storeu_pd(&sum, vAdd2);   // only first lane needed (stores 4, we read 1)

    // Scalar tail
    for (; i < n; ++i)
        sum += lat[i];

    return sum / static_cast<double>(n);
}

#else
// ===================================================================
// Fallback — non-x86 platforms (e.g., Apple Silicon ARM)
// Plain scalar implementation, no SIMD.
// ===================================================================

std::vector<std::size_t> filterByLatLonBoxOoA_avx2(
    const ServiceRequestOoA& data,
    double minLat, double maxLat,
    double minLon, double maxLon)
{
    const std::size_t n = data.latitude.size();
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < n; ++i) {
        if (data.latitude[i] >= minLat && data.latitude[i] <= maxLat &&
            data.longitude[i] >= minLon && data.longitude[i] <= maxLon)
            out.push_back(i);
    }
    return out;
}

double averageLatitudeOoA_avx2(const ServiceRequestOoA& data) {
    const std::size_t n = data.latitude.size();
    if (n == 0) return 0.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        sum += data.latitude[i];
    return sum / static_cast<double>(n);
}

#endif
