#include "ServiceRequest.h"
#include "queries.h"

#include <iostream>
#include <chrono>
#include <string>
#include <vector>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <iomanip>
#include <omp.h>

// Detect if type has .size()
template <typename T>
class has_size {
private:
    template <typename U>
    static auto test(int) -> decltype(std::declval<const U&>().size(), std::true_type{});
    template <typename>
    static std::false_type test(...);
public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

// Detect if type supports operator[](size_t) (vector-like)
template <typename T>
class has_index {
private:
    template <typename U>
    static auto test(int) -> decltype(std::declval<const U&>()[std::size_t{}], std::true_type{});
    template <typename>
    static std::false_type test(...);
public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

// Prevent compiler from optimizing away results (important for tiny queries)
template <typename T>
inline void doNotOptimize(const T& value) {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : "g"(value) : "memory");
#else
    volatile const T* p = &value;
    (void)p;
#endif
}

// Print summary for sized results
template <typename ResultT>
typename std::enable_if<has_size<ResultT>::value>::type
printSummary(const std::string& label, int runs, double total, const ResultT& first) {
    std::cout << label
              << " -> size=" << first.size()
              << ", total=" << total << "s"
              << ", avg=" << (total / runs) << "s\n";
}

// Print summary for scalar results
template <typename ResultT>
typename std::enable_if<!has_size<ResultT>::value>::type
printSummary(const std::string& label, int runs, double total, const ResultT& first) {
    std::cout << label
              << " -> value=" << first
              << ", total=" << total << "s"
              << ", avg=" << (total / runs) << "s\n";
}

// Sample printer for vector-like containers (size + operator[])
template <typename ResultT, typename PrintItemFn>
typename std::enable_if<has_size<ResultT>::value && has_index<ResultT>::value>::type
printSample(const ResultT& res, std::size_t sampleN, PrintItemFn printItem) {
    if (sampleN == 0) return;
    std::size_t n = res.size();
    std::size_t k = (sampleN < n) ? sampleN : n;

    std::cout << "  Results - (" << k << "/" << n << "):\n";
    for (std::size_t i = 0; i < k; ++i) {
        printItem(res[i], i);
    }
}

// Sample printer for sized-but-not-indexable containers: no-op by default
template <typename ResultT, typename PrintItemFn>
typename std::enable_if<has_size<ResultT>::value && !has_index<ResultT>::value>::type
printSample(const ResultT&, std::size_t, PrintItemFn) {}

// Sample printer for scalars: no-op
template <typename ResultT, typename PrintItemFn>
typename std::enable_if<!has_size<ResultT>::value>::type
printSample(const ResultT&, std::size_t, PrintItemFn) {}

// Generic benchmark with sampling
template <typename Fn, typename PrintItemFn>
auto benchmark(const std::string& label,
               int runs,
               Fn fn,
               std::size_t sampleN,
               PrintItemFn printItem) -> decltype(fn()) {
    using namespace std::chrono;

    // 1) Untimed run: correctness + sample printing
    auto first = fn();
    printSample(first, sampleN, printItem);

    // 2) Timed runs: no printing
    auto start = high_resolution_clock::now();
    for (int i = 0; i < runs; ++i) {
        auto r = fn();
        doNotOptimize(r);
    }
    auto end = high_resolution_clock::now();

    double total = duration<double>(end - start).count();
    printSummary(label, runs, total, first);

    return first;
}

// Overload: no sample printing
template <typename Fn>
auto benchmark(const std::string& label, int runs, Fn fn) -> decltype(fn()) {
    return benchmark(label, runs, fn, 0, [](const auto&, std::size_t){});
}

int main(int argc, char* argv[]) {
    std::string filename =
        (argc > 1) ? argv[1]
                   : "/Users/aravindreddy/Downloads/SJSU ClassWork/275 EAD/Mini1_Datasets/311_combined.csv.";

    using clock = std::chrono::high_resolution_clock;

    ServiceRequestOoA data;

    auto loadStart = clock::now();
    bool ok = loadServiceRequestOoA(filename, data);
    auto loadEnd = clock::now();

    double loadSeconds = std::chrono::duration<double>(loadEnd - loadStart).count();

    if (!ok) {
        std::cerr << "Failed to load data.\n";
        return 1;
    }

    std::cout << std::fixed << std::setprecision(6);

    std::cout << "[LOAD] file=\"" << filename << "\"\n"
              << "       records=" << data.uniqueKey.size()
              << ", time=" << loadSeconds << "s\n";

    std::cout << "Using threads (OpenMP): "
              << omp_get_max_threads()
              << "\n";

    const int runs = 15;
    const int runsAgg = 15;         // aggregation is heavier
    const std::size_t sampleN = 5;  // print only first 5 results once

    // Precompute date keys once
    uint64_t startKey = parseDateKey("01/01/2013 12:00:00 AM");
    uint64_t endKey   = parseDateKey("12/31/2013 11:59:59 PM");

    std::cout << "\nQuery Outputs \n";

    // Query 1: Date range
    std::cout << "\n[Query 1] Date Range - filtering requests created in year 2013.\n"
              << "Scans createdKey[] and returns indices that fall within the date bounds.\n";

    benchmark("date range 2013 (OoA)", runs,
        [&]() { return filterByCreatedDateRangeOoA_omp(data, startKey, endKey); },
        sampleN,
        [&](std::size_t idx, std::size_t i) {
            std::cout << "    [" << i << "] idx=" << idx
                      << " key=" << data.uniqueKey[idx]
                      << " borough=" << data.boroughUpper[idx]
                      << " complaint=" << data.complaintType[idx]
                      << "\n";
        }
    );

    // Query 2: Borough filter
    std::cout << "\n[Query 2] Borough Filter - selecting all requests from BROOKLYN.\n"
              << "Uses boroughUpper[] (precomputed) and returns matching indices.\n";

    benchmark("borough BROOKLYN (OoA)", runs,
        [&]() { return filterByBoroughOoA_omp(data, "BROOKLYN"); },
        sampleN,
        [&](std::size_t idx, std::size_t i) {
            std::cout << "    [" << i << "] idx=" << idx
                      << " key=" << data.uniqueKey[idx]
                      << " complaint=" << data.complaintType[idx]
                      << "\n";
        }
    );

    // Query 3: Complaint substring
    std::cout << "\n[Query 3] Complaint Search - substring match on complaintType for \"rodent\".\n"
              << "Uses complaintTypeLower[] to avoid per-record lowercase conversion.\n";

    benchmark("complaint 'rodent' (OoA)", runs,
        [&]() { return searchByComplaintOoA(data, "rodent"); },
        sampleN,
        [&](std::size_t idx, std::size_t i) {
            std::cout << "    [" << i << "] idx=" << idx
                      << " key=" << data.uniqueKey[idx]
                      << " complaint=" << data.complaintType[idx]
                      << " borough=" << data.boroughUpper[idx]
                      << "\n";
        }
    );

    // Query 4: Lat/Lon box
    std::cout << "\n[Query 4] Lat/Lon Box - selecting requests within NYC bounding box.\n"
              << "Numeric filter on latitude[] and longitude[] returning indices.\n";

    benchmark("lat/lon box (OoA)", runs,
        [&]() { return filterByLatLonBoxOoA(data, 40.5, 40.9, -74.25, -73.7); },
        sampleN,
        [&](std::size_t idx, std::size_t i) {
            std::cout << "    [" << i << "] idx=" << idx
                      << " key=" << data.uniqueKey[idx]
                      << " lat=" << data.latitude[idx]
                      << " lon=" << data.longitude[idx]
                      << "\n";
        }
    );

    // Query 5: Average latitude
    std::cout << "\n[Query 5] Average Latitude - computing mean latitude over all records.\n"
              << "OpenMP reduction (sum) over latitude[] then divides by N.\n";

    benchmark("average latitude (OoA)", runs,
        [&]() { return averageLatitudeOoA_omp(data); }
    );

    // Query 6: Borough aggregation
    std::cout << "\n[Query 6] Borough Aggregation - total requests + top complaint per borough.\n"
              << "Builds thread-local buckets and merges to reduce contention.\n";

    auto zones = benchmark("borough aggregation (OoA, omp fast)", runsAgg,
        [&]() { return aggregateByBoroughOoA_omp_fast(data); }
    );

    std::cout << "\n=== Borough Totals + Top Complaint (OoA, omp fast) ===\n";
    printTopComplaintPerBorough(zones);

    return 0;
}