// ---------------------------------------------------------------------------
// pthread/main.cpp
//
// Raw POSIX-thread (pthread) implementation of the NYC 311 query engine.
// This replaces all OpenMP pragmas with explicit pthread_create / pthread_join
// calls to demonstrate manual thread management, work partitioning, and
// thread-local result merging.
//
// Build:
//   g++ -std=c++17 -O2 -pthread -o main main.cpp ServiceRequest.cpp
//
// Run:
//   ./main [csv_file] [num_threads]
// ---------------------------------------------------------------------------

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
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <pthread.h>
#include <mach/mach.h>

// ---------------------------------------------------------------------------
// Global dataset
// ---------------------------------------------------------------------------
static std::vector<ServiceRequest> g_records;
static int g_numThreads = 4;   // default, overridden by argv[2]

// ---------------------------------------------------------------------------
// Utility: compute [start, end) range for thread `tid` over `n` elements
// ---------------------------------------------------------------------------
static void threadRange(int tid, int T, std::size_t n,
                        std::size_t& start, std::size_t& end) {
    std::size_t chunk = n / static_cast<std::size_t>(T);
    std::size_t rem   = n % static_cast<std::size_t>(T);
    start = static_cast<std::size_t>(tid) * chunk + std::min(static_cast<std::size_t>(tid), rem);
    end   = start + chunk + (static_cast<std::size_t>(tid) < rem ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Benchmark helpers (same as other versions, no OpenMP dependency)
// ---------------------------------------------------------------------------
template <typename T>
class has_size {
    template <typename U>
    static auto test(int) -> decltype(std::declval<const U&>().size(), std::true_type{});
    template <typename> static std::false_type test(...);
public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T>
class has_index {
    template <typename U>
    static auto test(int) -> decltype(std::declval<const U&>()[std::size_t{}], std::true_type{});
    template <typename> static std::false_type test(...);
public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T>
inline void doNotOptimize(const T& value) {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : "g"(value) : "memory");
#else
    volatile const T* p = &value; (void)p;
#endif
}

template <typename ResultT>
typename std::enable_if<has_size<ResultT>::value>::type
printSummary(const std::string& label, int runs, double total, const ResultT& first) {
    std::cout << label << " -> size=" << first.size()
              << ", total=" << total << "s, avg=" << (total / runs) << "s\n";
}
template <typename ResultT>
typename std::enable_if<!has_size<ResultT>::value>::type
printSummary(const std::string& label, int runs, double total, const ResultT& first) {
    std::cout << label << " -> value=" << first
              << ", total=" << total << "s, avg=" << (total / runs) << "s\n";
}

template <typename ResultT, typename PrintItemFn>
typename std::enable_if<has_size<ResultT>::value && has_index<ResultT>::value>::type
printSample(const ResultT& res, std::size_t sampleN, PrintItemFn printItem) {
    if (sampleN == 0) return;
    std::size_t k = std::min(sampleN, res.size());
    std::cout << "  Results - (" << k << "/" << res.size() << "):\n";
    for (std::size_t i = 0; i < k; ++i) printItem(res[i], i);
}
template <typename ResultT, typename PrintItemFn>
typename std::enable_if<has_size<ResultT>::value && !has_index<ResultT>::value>::type
printSample(const ResultT&, std::size_t, PrintItemFn) {}
template <typename ResultT, typename PrintItemFn>
typename std::enable_if<!has_size<ResultT>::value>::type
printSample(const ResultT&, std::size_t, PrintItemFn) {}

template <typename Fn, typename PrintItemFn>
auto benchmark(const std::string& label, int runs, Fn fn,
               std::size_t sampleN, PrintItemFn printItem) -> decltype(fn()) {
    auto first = fn();
    printSample(first, sampleN, printItem);
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < runs; ++i) { auto r = fn(); doNotOptimize(r); }
    auto end = std::chrono::high_resolution_clock::now();
    double total = std::chrono::duration<double>(end - start).count();
    printSummary(label, runs, total, first);
    return first;
}
template <typename Fn>
auto benchmark(const std::string& label, int runs, Fn fn) -> decltype(fn()) {
    return benchmark(label, runs, fn, 0, [](const auto&, std::size_t){});
}

// ---------------------------------------------------------------------------
// CSV parsing & loading (identical to other versions, single-threaded)
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
                if (i + 1 < line.size() && line[i + 1] == '"') { current += '"'; ++i; }
                else inQuotes = false;
            } else current += c;
        } else {
            if (c == '"') inQuotes = true;
            else if (c == ',') { fields.push_back(cleanString(current)); current.clear(); }
            else if (c != '\r') current += c;
        }
    }
    fields.push_back(cleanString(current));
    return fields;
}

static double rssMemMB() {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0.0;
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
}

std::vector<ServiceRequest> loadData(const std::string& filename) {
    constexpr std::size_t MAX_RECORDS = 14000000;
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "[LOADER] Loading NYC 311 data from: " << filename << "\n";

    std::ifstream file(filename);
    if (!file.is_open()) { std::cerr << "Error opening file: " << filename << "\n"; return {}; }

    std::vector<ServiceRequest> records;
    std::string line;
    std::size_t lineCount = 0, validRecords = 0;
    if (std::getline(file, line)) { ++lineCount; }

    while (std::getline(file, line)) {
        ++lineCount;
        if (lineCount % 1000000 == 0)
            std::cout << "Processed " << lineCount << " lines, loaded " << validRecords << " records...\n";
        auto fields = parseCSVLine(line);
        ServiceRequest req;
        if (req.fromFields(fields)) {
            records.push_back(std::move(req));
            if (++validRecords >= MAX_RECORDS) {
                std::cout << "Reached limit of " << MAX_RECORDS << " records.\n";
                break;
            }
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Loaded " << validRecords << " records in "
              << std::chrono::duration<double>(end - start).count() << " seconds\n";
    return records;
}

// ===================================================================
// QUERY 1 — Date Range Filter (pthreads)
// Each thread scans its chunk and collects matches into a local vector.
// After all threads join, local vectors are merged sequentially.
// ===================================================================
struct DateRangeArgs {
    int tid;
    DateTime start;
    DateTime end;
    std::vector<ServiceRequest> results;   // thread-local output
};

static void* dateRangeWorker(void* arg) {
    auto* a = static_cast<DateRangeArgs*>(arg);
    std::size_t lo, hi;
    threadRange(a->tid, g_numThreads, g_records.size(), lo, hi);

    for (std::size_t i = lo; i < hi; ++i) {
        if (g_records[i].createdDate >= a->start &&
            g_records[i].createdDate <= a->end) {
            a->results.push_back(g_records[i]);
        }
    }
    return nullptr;
}

std::vector<ServiceRequest> filterByCreatedDateRange_pthread(
    const DateTime& start, const DateTime& end)
{
    int T = g_numThreads;
    std::vector<pthread_t> threads(T);
    std::vector<DateRangeArgs> args(T);

    // Create threads
    for (int t = 0; t < T; ++t) {
        args[t].tid   = t;
        args[t].start = start;
        args[t].end   = end;
        pthread_create(&threads[t], nullptr, dateRangeWorker, &args[t]);
    }

    // Join threads
    for (int t = 0; t < T; ++t)
        pthread_join(threads[t], nullptr);

    // Merge thread-local results
    std::vector<ServiceRequest> out;
    for (int t = 0; t < T; ++t)
        out.insert(out.end(),
                   std::make_move_iterator(args[t].results.begin()),
                   std::make_move_iterator(args[t].results.end()));
    return out;
}

// ===================================================================
// QUERY 2 — Borough Filter (pthreads)
// ===================================================================
struct BoroughArgs {
    int tid;
    std::string target;
    std::vector<ServiceRequest> results;
};

static void* boroughWorker(void* arg) {
    auto* a = static_cast<BoroughArgs*>(arg);
    std::size_t lo, hi;
    threadRange(a->tid, g_numThreads, g_records.size(), lo, hi);

    for (std::size_t i = lo; i < hi; ++i) {
        std::string b = g_records[i].borough;
        for (auto& c : b) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (b == a->target)
            a->results.push_back(g_records[i]);
    }
    return nullptr;
}

std::vector<ServiceRequest> filterByBorough_pthread(const std::string& borough) {
    int T = g_numThreads;
    std::string target = borough;
    for (auto& c : target) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    std::vector<pthread_t> threads(T);
    std::vector<BoroughArgs> args(T);

    for (int t = 0; t < T; ++t) {
        args[t].tid    = t;
        args[t].target = target;
        pthread_create(&threads[t], nullptr, boroughWorker, &args[t]);
    }
    for (int t = 0; t < T; ++t)
        pthread_join(threads[t], nullptr);

    std::vector<ServiceRequest> out;
    for (int t = 0; t < T; ++t)
        out.insert(out.end(),
                   std::make_move_iterator(args[t].results.begin()),
                   std::make_move_iterator(args[t].results.end()));
    return out;
}

// ===================================================================
// QUERY 3 — Complaint Substring Search (pthreads)
// ===================================================================
struct ComplaintArgs {
    int tid;
    std::string keyword;
    std::vector<ServiceRequest> results;
};

static void* complaintWorker(void* arg) {
    auto* a = static_cast<ComplaintArgs*>(arg);
    std::size_t lo, hi;
    threadRange(a->tid, g_numThreads, g_records.size(), lo, hi);

    for (std::size_t i = lo; i < hi; ++i) {
        std::string comp = g_records[i].complaintType;
        for (auto& c : comp) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (comp.find(a->keyword) != std::string::npos)
            a->results.push_back(g_records[i]);
    }
    return nullptr;
}

std::vector<ServiceRequest> searchByComplaint_pthread(const std::string& keyword) {
    int T = g_numThreads;
    std::string key = keyword;
    for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::vector<pthread_t> threads(T);
    std::vector<ComplaintArgs> args(T);

    for (int t = 0; t < T; ++t) {
        args[t].tid     = t;
        args[t].keyword = key;
        pthread_create(&threads[t], nullptr, complaintWorker, &args[t]);
    }
    for (int t = 0; t < T; ++t)
        pthread_join(threads[t], nullptr);

    std::vector<ServiceRequest> out;
    for (int t = 0; t < T; ++t)
        out.insert(out.end(),
                   std::make_move_iterator(args[t].results.begin()),
                   std::make_move_iterator(args[t].results.end()));
    return out;
}

// ===================================================================
// QUERY 4 — Lat/Lon Bounding Box (pthreads)
// Returns pointers to avoid copying string-heavy structs.
// ===================================================================
struct LatLonArgs {
    int tid;
    double minLat, maxLat, minLon, maxLon;
    std::vector<const ServiceRequest*> results;
};

static void* latLonWorker(void* arg) {
    auto* a = static_cast<LatLonArgs*>(arg);
    std::size_t lo, hi;
    threadRange(a->tid, g_numThreads, g_records.size(), lo, hi);

    for (std::size_t i = lo; i < hi; ++i) {
        if (g_records[i].latitude  >= a->minLat &&
            g_records[i].latitude  <= a->maxLat &&
            g_records[i].longitude >= a->minLon &&
            g_records[i].longitude <= a->maxLon) {
            a->results.push_back(&g_records[i]);
        }
    }
    return nullptr;
}

std::vector<const ServiceRequest*> filterByLatLonBox_pthread(
    double minLat, double maxLat, double minLon, double maxLon)
{
    int T = g_numThreads;
    std::vector<pthread_t> threads(T);
    std::vector<LatLonArgs> args(T);

    for (int t = 0; t < T; ++t) {
        args[t] = {t, minLat, maxLat, minLon, maxLon, {}};
        pthread_create(&threads[t], nullptr, latLonWorker, &args[t]);
    }
    for (int t = 0; t < T; ++t)
        pthread_join(threads[t], nullptr);

    std::vector<const ServiceRequest*> out;
    for (int t = 0; t < T; ++t)
        out.insert(out.end(), args[t].results.begin(), args[t].results.end());
    return out;
}

// ===================================================================
// QUERY 5 — Average Latitude (pthreads)
// Each thread computes a partial sum; main thread reduces.
// ===================================================================
struct AvgLatArgs {
    int tid;
    double partialSum;
};

static void* avgLatWorker(void* arg) {
    auto* a = static_cast<AvgLatArgs*>(arg);
    std::size_t lo, hi;
    threadRange(a->tid, g_numThreads, g_records.size(), lo, hi);

    double sum = 0.0;
    for (std::size_t i = lo; i < hi; ++i)
        sum += g_records[i].latitude;
    a->partialSum = sum;
    return nullptr;
}

double averageLatitude_pthread() {
    if (g_records.empty()) return 0.0;
    int T = g_numThreads;
    std::vector<pthread_t> threads(T);
    std::vector<AvgLatArgs> args(T);

    for (int t = 0; t < T; ++t) {
        args[t] = {t, 0.0};
        pthread_create(&threads[t], nullptr, avgLatWorker, &args[t]);
    }
    for (int t = 0; t < T; ++t)
        pthread_join(threads[t], nullptr);

    double total = 0.0;
    for (int t = 0; t < T; ++t)
        total += args[t].partialSum;
    return total / static_cast<double>(g_records.size());
}

// ===================================================================
// QUERY 6 — Borough Aggregation (pthreads)
// Thread-local maps, merged after join (same strategy as OpenMP fast).
// ===================================================================
struct ZoneStats {
    std::size_t totalCount = 0;
    std::map<std::string, std::size_t> byComplaintType;
};

struct AggArgs {
    int tid;
    std::unordered_map<std::string, ZoneStats> localMap;
};

static void* aggWorker(void* arg) {
    auto* a = static_cast<AggArgs*>(arg);
    std::size_t lo, hi;
    threadRange(a->tid, g_numThreads, g_records.size(), lo, hi);

    for (std::size_t i = lo; i < hi; ++i) {
        const auto& r = g_records[i];
        const std::string key = r.borough.empty() ? "(unknown)" : r.borough;
        ZoneStats& z = a->localMap[key];
        z.totalCount++;
        if (!r.complaintType.empty())
            z.byComplaintType[r.complaintType]++;
    }
    return nullptr;
}

std::map<std::string, ZoneStats> aggregateByBorough_pthread() {
    int T = g_numThreads;
    std::vector<pthread_t> threads(T);
    std::vector<AggArgs> args(T);

    for (int t = 0; t < T; ++t) {
        args[t].tid = t;
        pthread_create(&threads[t], nullptr, aggWorker, &args[t]);
    }
    for (int t = 0; t < T; ++t)
        pthread_join(threads[t], nullptr);

    // Merge all thread-local maps
    std::map<std::string, ZoneStats> result;
    for (int t = 0; t < T; ++t) {
        for (auto& kv : args[t].localMap) {
            ZoneStats& dst = result[kv.first];
            dst.totalCount += kv.second.totalCount;
            for (auto& c : kv.second.byComplaintType)
                dst.byComplaintType[c.first] += c.second;
        }
    }
    return result;
}

template<typename MapT>
void printTopZones(const MapT& zones) {
    std::size_t index = 0;
    std::cout << "  Results - (" << zones.size() << "/" << zones.size() << "):\n";
    for (const auto& kv : zones) {
        std::string topComplaint = "(none)";
        std::size_t topCount = 0;
        for (const auto& c : kv.second.byComplaintType) {
            if (c.second > topCount) { topCount = c.second; topComplaint = c.first; }
        }
        std::cout << "    [" << index++ << "] borough=" << kv.first
                  << " total=" << kv.second.totalCount
                  << " top_complaint=" << topComplaint
                  << " count=" << topCount << "\n";
    }
}

// ===================================================================
// main()
// ===================================================================
int main(int argc, char* argv[]) {
    std::string filename =
        (argc > 1) ? argv[1]
                   : "/Users/aravindreddy/Downloads/SJSU ClassWork/275 EAD/Mini1_Datasets/311_combined.csv.";

    if (argc > 2) g_numThreads = std::atoi(argv[2]);
    if (g_numThreads < 1) g_numThreads = 4;

    std::cout << "Using threads (pthreads): " << g_numThreads << "\n";

    double memBefore = rssMemMB();
    std::cout << "Memory before load: " << memBefore << " MB\n";

    g_records = loadData(filename);

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

    std::cout << "\n=== Query Outputs (pthreads) ===\n";

    // Query 1 — Date Range
    std::cout << "\n[Query 1] Date Range - filtering requests created in year 2013.\n";
    DateTime start = DateTime::parse("01/01/2013 12:00:00 AM");
    DateTime end   = DateTime::parse("12/31/2013 11:59:59 PM");

    benchmark("date range 2013 (pthread)", runs,
        [&](){ return filterByCreatedDateRange_pthread(start, end); },
        sampleN,
        [](const ServiceRequest& r, std::size_t i){
            std::cout << "    [" << i << "] key=" << r.uniqueKey
                      << " created=" << r.createdDate.toString()
                      << " borough=" << r.borough << "\n";
        }
    );

    // Query 2 — Borough Filter
    std::cout << "\n[Query 2] Borough filter - selecting all requests from BROOKLYN.\n";
    benchmark("borough BROOKLYN (pthread)", runs,
        [&](){ return filterByBorough_pthread("BROOKLYN"); },
        sampleN,
        [](const ServiceRequest& r, std::size_t i){
            std::cout << "    [" << i << "] key=" << r.uniqueKey
                      << " borough=" << r.borough
                      << " complaint=" << r.complaintType << "\n";
        }
    );

    // Query 3 — Complaint Search
    std::cout << "\n[Query 3] Complaint search - keyword: \"rodent\".\n";
    benchmark("complaint 'rodent' (pthread)", runs,
        [&](){ return searchByComplaint_pthread("rodent"); },
        sampleN,
        [](const ServiceRequest& r, std::size_t i){
            std::cout << "    [" << i << "] key=" << r.uniqueKey
                      << " complaint=" << r.complaintType
                      << " borough=" << r.borough << "\n";
        }
    );

    // Query 4 — Lat/Lon Box
    std::cout << "\n[Query 4] Lat/Lon bounding box - NYC area.\n";
    benchmark("lat/lon box (pthread)", runs,
        [&](){ return filterByLatLonBox_pthread(40.5, 40.9, -74.25, -73.7); },
        sampleN,
        [](const ServiceRequest* r, std::size_t i){
            if (!r) return;
            std::cout << "    [" << i << "] key=" << r->uniqueKey
                      << " lat=" << r->latitude
                      << " lon=" << r->longitude << "\n";
        }
    );

    // Query 5 — Average Latitude
    std::cout << "\n[Query 5] Average Latitude.\n";
    benchmark("average latitude (pthread)", runs,
        [&](){ return averageLatitude_pthread(); }
    );

    // Query 6 — Borough Aggregation
    std::cout << "\n[Query 6] Borough Aggregation.\n";
    auto agg = benchmark("borough aggregation (pthread)", runs,
        [&](){ return aggregateByBorough_pthread(); }
    );
    std::cout << "\n Borough Totals + Top Complaint -\n";
    printTopZones(agg);

    return 0;
}
