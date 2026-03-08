#include "ServiceRequest.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <mach/mach.h>
#include <algorithm> 
#include <map>
#include <cmath>
#include <type_traits>
#include <utility>
#include <iomanip>

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

    std::cout << "  Top 5 Results - (" << k << "/" << n << "):\n";
    for (std::size_t i = 0; i < k; ++i) {
        printItem(res[i], i);
    }
}

// Sample printer for sized-but-not-indexable containers (e.g., map): do nothing by default
template <typename ResultT, typename PrintItemFn>
typename std::enable_if<has_size<ResultT>::value && !has_index<ResultT>::value>::type
printSample(const ResultT&, std::size_t, PrintItemFn) {
    // no-op (map/set). You already print map summaries separately.
}

// Sample printer for scalars: no-op
template <typename ResultT, typename PrintItemFn>
typename std::enable_if<!has_size<ResultT>::value>::type
printSample(const ResultT&, std::size_t, PrintItemFn) {}

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
    return benchmark(label, runs, fn, 0, [](const auto&, std::size_t){});
}

// cleanString — strips surrounding double-quotes if present
std::string cleanString(const std::string& str) {
    std::string cleaned = str;
    if (!cleaned.empty() && cleaned.front() == '"') cleaned.erase(0, 1);
    if (!cleaned.empty() && cleaned.back()  == '"') cleaned.pop_back();
    return cleaned;
}

// parseCSVLine — splits one CSV line respecting quoted fields
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
                // Doubled quote inside a quoted field → literal "
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
                // ignore Windows carriage return
            } else {
                current += c;
            }
        }
    }
    fields.push_back(cleanString(current));  
    return fields;
}

// rssMemMB — returns physical RAM usage in MB (macOS specific)
static double rssMemMB() {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0.0;
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
}

// loadData — loads CSV data into vector of ServiceRequest
std::vector<ServiceRequest> loadData(const std::string& filename) {
    auto start = std::chrono::high_resolution_clock::now();

    std::cout << "Loading NYC 311 data from: " << filename << std::endl;

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return {};
    }

    std::vector<ServiceRequest> records;
    std::string line;
    std::size_t lineCount = 0;
    std::size_t validRecords = 0;
    const std::size_t RECORD_LIMIT = 14000000; // 14 Million record limit for testing

    // Skipping header
    if (std::getline(file, line)) {
        lineCount++;
        std::cout << "Skipped header: " << line.substr(0, 100) << "..." << std::endl;
    }

    // Read data lines
    while (std::getline(file, line)) {
        lineCount++;
        if (lineCount % 1000000 == 0) {
            std::cout << "Processed " << lineCount << " lines, loaded " << validRecords << " records..." << std::endl;
        }
        std::vector<std::string> fields = parseCSVLine(line);
        ServiceRequest req;
        if (req.fromFields(fields)) {
            records.push_back(std::move(req));
            validRecords++;
            if (validRecords >= RECORD_LIMIT) {
                std::cout << "Reached limit of " << RECORD_LIMIT << " records, stopping load." << std::endl;
                break;
            }
        } else {
            std::cerr << "Malformed record at line " << lineCount << std::endl;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;

    std::cout << "Loaded " << validRecords << " records in " << duration.count() << " seconds" << std::endl;
    std::cout << "Total lines processed: " << lineCount << std::endl;

    return records;
}

static std::vector<ServiceRequest> g_records;

// Query 1 - range query on createdDate
std::vector<ServiceRequest> filterByCreatedDateRange(const DateTime& start,
                                                     const DateTime& end) {
    std::vector<ServiceRequest> out;
    for (const auto& r : g_records) {
        if (r.createdDate >= start && r.createdDate <= end)
            out.push_back(r);
    }
    return out;
}

// Query 2 - exact match (case-insensitive) on borough
std::vector<ServiceRequest> filterByBorough(const std::string& borough) {
    std::vector<ServiceRequest> out;
    for (const auto& r : g_records) {
        if (!r.borough.empty()) {
            std::string b = r.borough;
            for (auto& c : b) c = std::toupper(c);
            std::string t = borough;
            for (auto& c : t) c = std::toupper(c);
            if (b == t)
                out.push_back(r);
        }
    }
    return out;
}

// Query 3 - substring search on complaintType (case-insensitive)
std::vector<ServiceRequest> searchByComplaint(const std::string& keyword) {
    std::vector<ServiceRequest> out;
    std::string key = keyword;
    for (auto& c : key) c = std::tolower(c);
    for (const auto& r : g_records) {
        std::string comp = r.complaintType;
        for (auto& c : comp) c = std::tolower(c);
        if (comp.find(key) != std::string::npos) {
            out.push_back(r);
        }
    }
    return out;
}

// Query 4 - bounding box on latitude/longitude
// Returns pointers to the matching records instead of copying full objects.
std::vector<const ServiceRequest*> filterByLatLonBox(double minLat, double maxLat,
                                                     double minLon, double maxLon) {
    std::vector<const ServiceRequest*> out;
    out.reserve(1024); // start with a small capacity to avoid repeated reallocation
    for (const auto& r : g_records) {
        if (r.latitude >= minLat && r.latitude <= maxLat &&
            r.longitude >= minLon && r.longitude <= maxLon) {
            out.push_back(&r);
        }
    }
    return out;
}

// Query 5 - compute average latitude of the loaded records - demonstrates an aggregation (reduce) operation.
double averageLatitude() {
    if (g_records.empty()) return 0.0;
    double sum = 0.0;
    for (const auto &r : g_records) sum += r.latitude;
    return sum / g_records.size();
}

// Query 6 - Aggregation: Borough totals + top complaint (SERIAL)
struct ZoneStats {
    std::size_t totalCount = 0;
    std::map<std::string, std::size_t> byComplaintType;
};

std::map<std::string, ZoneStats> aggregateByBorough() {
    std::map<std::string, ZoneStats> result;

    for (const auto& r : g_records) {
        const std::string key = r.borough.empty() ? "(unknown)" : r.borough;
        ZoneStats& z = result[key];
        z.totalCount++;

        if (!r.complaintType.empty())
            z.byComplaintType[r.complaintType]++;
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

int main() {
    const std::string filename = "/Users/aravindreddy/Downloads/SJSU ClassWork/275 EAD/Mini1_Datasets/311_combined.csv.";

    double memBefore = rssMemMB();
    std::cout << "Memory before load: " << memBefore << " MB" << std::endl;

    g_records = loadData(filename);

    double memAfter = rssMemMB();
    std::cout << "Memory after load: " << memAfter << " MB" << std::endl;
    std::cout << "Memory delta: " << (memAfter - memBefore) << " MB" << std::endl;

    if (g_records.empty()) {
        std::cerr << "No records loaded. Exiting." << std::endl;
        return 1;
    }

    std::cout << std::fixed << std::setprecision(6);

    std::cout << "\nQuery Outputs - " << std::endl;

    const int runs = 20;  // number of runs 
    const std::size_t sampleN = 5;

    std::cout << "\n[Query 1] Date Range - Filtering service requests created in calendar year 2013.\n"
          << "This query scans all records and selects those whose createdDate falls within the specified range.\n";

    DateTime start = DateTime::parse("01/01/2013 12:00:00 AM");
    DateTime end   = DateTime::parse("12/31/2013 11:59:59 PM");
    benchmark("date range 2013", runs,
    [&](){ return filterByCreatedDateRange(start, end); },
    sampleN,
    [&](const ServiceRequest& r, std::size_t i){
        std::cout << "    [" << i << "] key=" << r.uniqueKey
                  << " created=" << r.createdDate.toString()   // or however you print DateTime
                  << " borough=" << r.borough
                  << "\n";
    }
);

    std::cout << "\n[Query 2] Borough filter - Selecting all service requests from borough: BROOKLYN.\n"
          << "This demonstrates case-insensitive categorical filtering.\n";

    benchmark("borough BROOKLYN", runs,
    [&](){ return filterByBorough("BROOKLYN"); },
    sampleN,
    [&](const ServiceRequest& r, std::size_t i){
        std::cout << "    [" << i << "] key=" << r.uniqueKey
                  << " borough=" << r.borough
                  << " complaint=" << r.complaintType
                  << "\n";
    }
);

    std::cout << "\n[Query 3] Complaint search - Searching complaintType for keyword: \"rodent\".\n"
          << "This performs a case-insensitive substring search across all records.\n";

    benchmark("complaint 'rodent'", runs,
    [&](){ return searchByComplaint("rodent"); },
    sampleN,
    [&](const ServiceRequest& r, std::size_t i){
        std::cout << "    [" << i << "] key=" << r.uniqueKey
                  << " complaint=" << r.complaintType
                  << " borough=" << r.borough
                  << "\n";
    }
);

    std::cout << "\n[Query 4] Latitude/Longitude Filtering - Filtering service requests within NYC geographic bounding box.\n"
          << "Records must fall within specified latitude and longitude limits.\n";
    benchmark("lat/lon box", runs,
    [&](){ return filterByLatLonBox(40.5, 40.9, -74.25, -73.7); },
    sampleN,
    [&](const ServiceRequest* r, std::size_t i){
        if (!r) return;
        std::cout << "    [" << i << "] key=" << r->uniqueKey
                  << " lat=" << r->latitude
                  << " lon=" << r->longitude
                  << "\n";
    }
);

    std::cout << "\n[Query 5] Average Latitude - Computing average latitude of all loaded service requests.\n"
          << "This demonstrates a full-dataset aggregation (reduce operation).\n";

    benchmark("average latitude", runs,
          [](){ return averageLatitude(); });


    std::cout << "\n[Query 6] Borough Aggregation - Aggregating records by borough and identifying the most frequent complaint type.\n"
          << "For each borough: total request count and most common complaint are computed.\n";

    auto agg = benchmark("borough aggregation total+top complaint",
                     runs,
                     [](){ return aggregateByBorough(); });

    // Print once after benchmarking
    std::cout << "\n=== Borough Totals + Top Complaint ===\n";
    printTopZones(agg);

    return 0;
}