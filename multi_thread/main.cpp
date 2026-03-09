#include "ServiceRequest.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <omp.h>
#include <mach/mach.h>

static std::vector<ServiceRequest> g_records;

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

// Sample printer for sized-but-not-indexable containers (e.g., map): no-op by default
template <typename ResultT, typename PrintItemFn>
typename std::enable_if<has_size<ResultT>::value && !has_index<ResultT>::value>::type
printSample(const ResultT&, std::size_t, PrintItemFn) {}

// Sample printer for scalars: no-op
template <typename ResultT, typename PrintItemFn>
typename std::enable_if<!has_size<ResultT>::value>::type
printSample(const ResultT&, std::size_t, PrintItemFn) {}

// Prevent compiler from optimizing away benchmarked work
template <typename T>
inline void doNotOptimize(const T& value) {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : "g"(value) : "memory");
#else
    volatile const T* p = &value;
    (void)p;
#endif
}

// Generic benchmark with sampling
template <typename Fn, typename PrintItemFn>
auto benchmark(const std::string& label,
               int runs,
               Fn fn,
               std::size_t sampleN,
               PrintItemFn printItem) -> decltype(fn()) {
    using namespace std::chrono;

    // 1) Untimed run: correctness + sample
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
    return benchmark(label, runs, fn, 0, [](const auto&, std::size_t) {});
}

// ---------------------------------------------------------------------------
// CSV parsing / utilities
// ---------------------------------------------------------------------------

std::string cleanString(const std::string& str) {
    std::string cleaned = str;
    if (!cleaned.empty() && cleaned.front() == '"') cleaned.erase(0, 1);
    if (!cleaned.empty() && cleaned.back()  == '"') cleaned.pop_back();
    return cleaned;
}

std::vector<std::string> parseCSVLine(const std::string& line) {
    std::vector<std::string> fields;
    fields.reserve(44);
    std::string current;
    current.reserve(64);
    bool inQuotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                // escaped quotes ("")
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                current += c;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (c == ',') {
                fields.push_back(cleanString(current));
                current.clear();
            } else if (c == '\r') {
                // skip
            } else {
                current += c;
            }
        }
    }

    fields.push_back(cleanString(current));
    return fields;
}

// RSS (macOS)
static double rssMemMB() {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0.0;
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
}

// NOTE: still single-threaded loader (your existing "fallback").
// Parallel CSV parsing is possible, but keep this as Phase 1/2 baseline.
std::vector<ServiceRequest> loadDataParallel(const std::string& filename, int /*numberOfThreads*/) {
    constexpr std::size_t MAX_RECORDS = 14000000;

    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "[SINGLE-THREAD LOADER] Loading NYC 311 data from: " << filename << "\n";

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << filename << "\n";
        return {};
    }

    std::vector<ServiceRequest> records;
    std::string line;
    std::size_t lineCount = 0;
    std::size_t validRecords = 0;

    // header
    if (std::getline(file, line)) {
        ++lineCount;
        std::cout << "Skipped header: " << line.substr(0, 100) << "...\n";
    }

    while (std::getline(file, line)) {
        ++lineCount;
        if (lineCount % 1000000 == 0) {
            std::cout << "Processed " << lineCount
                      << " lines, loaded " << validRecords << " records...\n";
        }

        auto fields = parseCSVLine(line);
        ServiceRequest req;
        if (req.fromFields(fields)) {
            records.push_back(std::move(req));
            ++validRecords;
            if (validRecords >= MAX_RECORDS) {
                std::cout << "Reached limit of " << MAX_RECORDS << " records, stopping load.\n";
                break;
            }
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> dur = end - start;
    std::cout << "Loaded " << validRecords << " records in " << dur.count() << " seconds\n";
    std::cout << "Total lines processed: " << lineCount << "\n";
    return records;
}

std::vector<ServiceRequest> filterByCreatedDateRange(const DateTime& start,
                                                     const DateTime& end,
                                                     int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_records.size());

    std::vector<std::vector<ServiceRequest>> localResults(numberOfThreads);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        if (g_records[i].createdDate >= start && g_records[i].createdDate <= end) {
            int t = omp_get_thread_num();
            localResults[t].push_back(g_records[i]);
        }
    }

    std::vector<ServiceRequest> out;
    for (int t = 0; t < numberOfThreads; ++t)
        out.insert(out.end(), localResults[t].begin(), localResults[t].end());
    return out;
}

std::vector<ServiceRequest> filterByBorough(const std::string& borough,
                                            int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_records.size());
    std::vector<std::vector<ServiceRequest>> localResults(numberOfThreads);

    std::string target = borough;
    for (auto& c : target) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        std::string b = g_records[i].borough;
        for (auto& c : b) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (b == target) {
            int t = omp_get_thread_num();
            localResults[t].push_back(g_records[i]);
        }
    }

    std::vector<ServiceRequest> out;
    for (int t = 0; t < numberOfThreads; ++t)
        out.insert(out.end(), localResults[t].begin(), localResults[t].end());
    return out;
}

std::vector<ServiceRequest> searchByComplaint(const std::string& keyword,
                                              int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_records.size());
    std::vector<std::vector<ServiceRequest>> localResults(numberOfThreads);

    std::string key = keyword;
    for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        std::string comp = g_records[i].complaintType;
        for (auto& c : comp) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (comp.find(key) != std::string::npos) {
            int t = omp_get_thread_num();
            localResults[t].push_back(g_records[i]);
        }
    }

    std::vector<ServiceRequest> out;
    for (int t = 0; t < numberOfThreads; ++t)
        out.insert(out.end(), localResults[t].begin(), localResults[t].end());
    return out;
}

std::vector<const ServiceRequest*> filterByLatLonBox(double minLat, double maxLat,
                                                     double minLon, double maxLon,
                                                     int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_records.size());
    std::vector<std::vector<const ServiceRequest*>> localResults(numberOfThreads);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        if (g_records[i].latitude  >= minLat && g_records[i].latitude  <= maxLat &&
            g_records[i].longitude >= minLon && g_records[i].longitude <= maxLon) {
            int t = omp_get_thread_num();
            localResults[t].push_back(&g_records[i]);
        }
    }

    std::vector<const ServiceRequest*> out;
    for (int t = 0; t < numberOfThreads; ++t)
        out.insert(out.end(), localResults[t].begin(), localResults[t].end());
    return out;
}

double averageLatitude(int numberOfThreads) {
    if (g_records.empty()) return 0.0;
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_records.size());
    double sum = 0.0;

    #pragma omp parallel for reduction(+:sum)
    for (int i = 0; i < n; ++i) {
        sum += g_records[i].latitude;
    }

    return sum / static_cast<double>(g_records.size());
}

struct ZoneStats {
    std::size_t totalCount = 0;
    std::map<std::string, std::size_t> byComplaintType;
    std::map<std::string, std::size_t> byAgency;
    std::map<std::string, std::size_t> byStatus;
};

// Slow scaling version (critical section)
std::map<std::string, ZoneStats> aggregateByBorough_omp_critical() {
    std::map<std::string, ZoneStats> result;

    #pragma omp parallel for schedule(static)
    for (std::size_t i = 0; i < g_records.size(); ++i) {
        const auto& r = g_records[i];
        const std::string key = r.borough.empty() ? "(unknown)" : r.borough;

        #pragma omp critical
        {
            ZoneStats& z = result[key];
            z.totalCount++;
            if (!r.complaintType.empty()) z.byComplaintType[r.complaintType]++;
            if (!r.agency.empty())        z.byAgency[r.agency]++;
            if (!r.status.empty())        z.byStatus[r.status]++;
        }
    }

    return result;
}

// Merge helper
static void mergeZoneStats(ZoneStats& dst, const ZoneStats& src) {
    dst.totalCount += src.totalCount;
    for (const auto& kv : src.byComplaintType) dst.byComplaintType[kv.first] += kv.second;
    for (const auto& kv : src.byAgency)        dst.byAgency[kv.first]        += kv.second;
    for (const auto& kv : src.byStatus)        dst.byStatus[kv.first]        += kv.second;
}

// Faster scaling version: thread-local unordered_map, then merge
std::map<std::string, ZoneStats> aggregateByBorough_omp_fast(int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int T = numberOfThreads;
    std::vector<std::unordered_map<std::string, ZoneStats>> local(static_cast<std::size_t>(T));

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        auto& mp = local[static_cast<std::size_t>(tid)];

        #pragma omp for schedule(static)
        for (std::size_t i = 0; i < g_records.size(); ++i) {
            const auto& r = g_records[i];
            const std::string key = r.borough.empty() ? "(unknown)" : r.borough;

            ZoneStats& z = mp[key];
            z.totalCount++;
            if (!r.complaintType.empty()) z.byComplaintType[r.complaintType]++;
            if (!r.agency.empty())        z.byAgency[r.agency]++;
            if (!r.status.empty())        z.byStatus[r.status]++;
        }
    }

    std::map<std::string, ZoneStats> result;
    for (auto& mp : local) {
        for (auto& kv : mp) {
            mergeZoneStats(result[kv.first], kv.second);
        }
    }
    return result;
}

template<typename MapT>
void printTopZones(const MapT& zones) {
    std::size_t totalZones = zones.size();
    std::size_t index = 0;

    std::cout << "  Results - (" << totalZones << "/" << totalZones << "):\n";

    for (const auto& kv : zones) {
        const auto& borough = kv.first;
        const auto& z = kv.second;

        std::string topComplaint = "(none)";
        std::size_t topCount = 0;

        for (const auto& c : z.byComplaintType) {
            if (c.second > topCount) {
                topCount = c.second;
                topComplaint = c.first;
            }
        }

        std::cout << "    [" << index++ << "] "
                  << "borough=" << borough
                  << " total=" << z.totalCount
                  << " top_complaint=" << topComplaint
                  << " count=" << topCount
                  << "\n";
    }
}

int main(int argc, char* argv[]) { 
    std::string filename =
        "/Users/aravindreddy/Downloads/SJSU ClassWork/275 EAD/Mini1_Datasets/311_combined.csv.";

    int numberOfThreads = 10;

    if (argc > 2) {
        numberOfThreads = std::atoi(argv[2]);
        omp_set_num_threads(numberOfThreads);
    } 
    std::cout << "Thread count: " << numberOfThreads << "\n";

    double memBefore = rssMemMB();
    std::cout << "Memory before load: " << memBefore << " MB\n";

    g_records = loadDataParallel(filename, numberOfThreads);

    double memAfter = rssMemMB();
    std::cout << "Memory after load: " << memAfter << " MB\n";
    std::cout << "Memory delta: " << (memAfter - memBefore) << " MB\n";

    if (g_records.empty()) {
        std::cerr << "No records loaded. Exiting.\n";
        return 1;
    }

    std::cout << std::fixed << std::setprecision(6);

    const int runs = 15;
    const std::size_t sampleN = 5;

    std::cout << "\n=== Query Outputs ===\n";

    // Query 1
    std::cout << "\n[Query 1] Date Range - Filtering service requests created in calendar year 2013.\n"
              << "This query scans all records and selects those whose createdDate falls within the specified range.\n";

    DateTime start = DateTime::parse("01/01/2013 12:00:00 AM");
    DateTime end   = DateTime::parse("12/31/2013 11:59:59 PM");

    benchmark("date range 2013", runs,
        [&](){ return filterByCreatedDateRange(start, end, numberOfThreads); },
        sampleN,
        [&](const ServiceRequest& r, std::size_t i){
            std::cout << "    [" << i << "] key=" << r.uniqueKey
                      << " created=" << r.createdDate.toString()
                      << " borough=" << r.borough
                      << "\n";
        }
    );

    // Query 2
    std::cout << "\n[Query 2] Borough filter - Selecting all service requests from borough: BROOKLYN.\n"
              << "This demonstrates case-insensitive categorical filtering.\n";

    benchmark("borough BROOKLYN", runs,
        [&](){ return filterByBorough("BROOKLYN", numberOfThreads); },
        sampleN,
        [&](const ServiceRequest& r, std::size_t i){
            std::cout << "    [" << i << "] key=" << r.uniqueKey
                      << " borough=" << r.borough
                      << " complaint=" << r.complaintType
                      << "\n";
        }
    );

    // Query 3
    std::cout << "\n[Query 3] Complaint search - Searching complaintType for keyword: \"rodent\".\n"
              << "This performs a case-insensitive substring search across all records.\n";

    benchmark("complaint 'rodent'", runs,
        [&](){ return searchByComplaint("rodent", numberOfThreads); },
        sampleN,
        [&](const ServiceRequest& r, std::size_t i){
            std::cout << "    [" << i << "] key=" << r.uniqueKey
                      << " complaint=" << r.complaintType
                      << " borough=" << r.borough
                      << "\n";
        }
    );

    // Query 4
    std::cout << "\n[Query 4] Latitude/Longitude Filtering - Filtering service requests within NYC geographic bounding box.\n"
              << "Records must fall within specified latitude and longitude limits.\n";

    benchmark("lat/lon box", runs,
        [&](){ return filterByLatLonBox(40.5, 40.9, -74.25, -73.7, numberOfThreads); },
        sampleN,
        [&](const ServiceRequest* r, std::size_t i){
            if (!r) return;
            std::cout << "    [" << i << "] key=" << r->uniqueKey
                      << " lat=" << r->latitude
                      << " lon=" << r->longitude
                      << "\n";
        }
    );

    // Query 5
    std::cout << "\n[Query 5] Average Latitude - Computing average latitude of all loaded service requests.\n"
              << "This demonstrates a full-dataset aggregation (reduce operation) using OpenMP reduction.\n";

    benchmark("average latitude", runs,
        [&](){ return averageLatitude(numberOfThreads); }
    );

    // Query 6
    std::cout << "\n[Query 6] Borough Aggregation - Aggregating records by borough and identifying the most frequent complaint type.\n"
              << "For each borough: total request count and most common complaint are computed.\n";

    // Optional warm-up (OpenMP thread pool / first-touch effects)
    (void)aggregateByBorough_omp_fast(numberOfThreads);

    auto agg_fast = benchmark("borough aggregation (omp fast)", runs,
        [&](){ return aggregateByBorough_omp_fast(numberOfThreads); }
    );

    std::cout << "\n Borough Totals + Top Complaint -\n";
    printTopZones(agg_fast);


    return 0;
}