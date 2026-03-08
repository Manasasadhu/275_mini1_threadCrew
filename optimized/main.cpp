#include "ServiceRequest.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <omp.h>
#include <mach/mach.h>

// Global dataset — filled once during loading, read-only during queries
static std::vector<ServiceRequest> g_records;

// ---------------------------------------------------------------------------
// Utility functions (reused from single_thread/main.cpp)
// ---------------------------------------------------------------------------

// Helper function to remove surrounding quotes from a string if present.
std::string cleanString(const std::string& str) {
    std::string cleaned = str;
    if (!cleaned.empty() && cleaned.front() == '"') cleaned.erase(0, 1);
    if (!cleaned.empty() && cleaned.back()  == '"') cleaned.pop_back();
    return cleaned;
}

// Parse a single CSV line into a vector of strings.
// Handles fields enclosed in quotes and embedded commas.
std::vector<std::string> parseCSVLine(const std::string& line) {
    std::vector<std::string> fields;
    fields.reserve(44); // Reserve space for expected number of columns to avoid reallocations
    std::string current;
    current.reserve(64);
    bool inQuotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                // Handle escaped quotes ("")
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
                // End of field
                fields.push_back(cleanString(current));
                current.clear();
            } else if (c == '\r') {
                // skip carriage returns
            } else {
                current += c;
            }
        }
    }
    fields.push_back(cleanString(current)); // Add the last field
    return fields;
}

// Returns current Resident Set Size (RSS) memory usage in MB (macOS specific).
static double rssMemMB() {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0.0;
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
}

// ---------------------------------------------------------------------------
// Parallel CSV loader using std::thread
// Loads data by splitting the file into chunks and processing them in parallel.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Sequential CSV loader — same logic as single_thread/main.cpp.
// Parallelism is only applied to the query functions, not loading.
// ---------------------------------------------------------------------------
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

    // Skip header
    if (std::getline(file, line)) {
        lineCount++;
        std::cout << "Skipped header: " << line.substr(0, 100) << "..." << std::endl;
    }

    // Read data lines — one at a time, no parallel loading
    while (std::getline(file, line)) {
        lineCount++;
        if (lineCount % 100000 == 0) {
            std::cout << "Processed " << lineCount << " lines, loaded " << validRecords << " records..." << std::endl;
        }

        std::vector<std::string> fields = parseCSVLine(line);
        ServiceRequest req;
        if (req.fromFields(fields)) {
            records.push_back(std::move(req));
            validRecords++;
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

// ---------------------------------------------------------------------------
// Helper: case-insensitive string equality without allocating a temp string.
// Compares character-by-character, converting to uppercase on the fly.
// ---------------------------------------------------------------------------
static bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Parallel query functions (OpenMP)
//
// Key optimizations vs the naive version:
//   1. Store indices instead of copying full ServiceRequest objects.
//      Each record has ~20 std::string fields — copying millions of them
//      hammers the heap allocator which serializes across threads.
//   2. Case-insensitive comparisons done in-place (no temp string alloc).
//   3. Pre-reserve thread-local vectors to reduce reallocation.
//   4. Single pass to build the final result from indices.
// ---------------------------------------------------------------------------

// 1. Date range filter — returns pointers to avoid copying records
std::vector<const ServiceRequest*> filterByCreatedDateRange(const DateTime& start,
                                                            const DateTime& end,
                                                            int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_records.size());

    // Each thread collects pointers (8 bytes each vs ~500+ bytes per record copy)
    std::vector<std::vector<const ServiceRequest*>> localPtrs(numberOfThreads);
    for (auto& v : localPtrs) v.reserve(n / numberOfThreads / 4);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        if (g_records[i].createdDate >= start && g_records[i].createdDate <= end) {
            localPtrs[omp_get_thread_num()].push_back(&g_records[i]);
        }
    }

    std::size_t total = 0;
    for (int t = 0; t < numberOfThreads; ++t) total += localPtrs[t].size();

    std::vector<const ServiceRequest*> out;
    out.reserve(total);
    for (int t = 0; t < numberOfThreads; ++t)
        out.insert(out.end(), localPtrs[t].begin(), localPtrs[t].end());
    return out;
}

// 2. Borough filter — case-insensitive, returns pointers, no temp string alloc
std::vector<const ServiceRequest*> filterByBorough(const std::string& borough,
                                                   int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_records.size());

    std::vector<std::vector<const ServiceRequest*>> localPtrs(numberOfThreads);
    for (auto& v : localPtrs) v.reserve(n / numberOfThreads / 4);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        if (equalsIgnoreCase(g_records[i].borough, borough)) {
            localPtrs[omp_get_thread_num()].push_back(&g_records[i]);
        }
    }

    std::size_t total = 0;
    for (int t = 0; t < numberOfThreads; ++t) total += localPtrs[t].size();

    std::vector<const ServiceRequest*> out;
    out.reserve(total);
    for (int t = 0; t < numberOfThreads; ++t)
        out.insert(out.end(), localPtrs[t].begin(), localPtrs[t].end());
    return out;
}

/*
// 3. Complaint substring search (case-insensitive)
std::vector<ServiceRequest> searchByComplaint(const std::string& keyword,
                                              int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_records.size());
    std::vector<std::vector<ServiceRequest>> localResults(numberOfThreads);

    std::string key = keyword;
    for (auto& c : key) c = static_cast<char>(std::tolower(c));

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        std::string comp = g_records[i].complaintType;
        for (auto& c : comp) c = static_cast<char>(std::tolower(c));
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

// 4. Lat/lon bounding box filter (returns pointers to avoid copying)
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

//5. Sort by createdDate (Sequential Sort)
// Note: std::sort is used here on a copy of the data. 
// A full parallel sort (e.g. __gnu_parallel::sort) could be used if available.
std::vector<ServiceRequest> sortByCreatedDate(int numberOfThreads) {
    // OpenMP is not used for std::sort, but we set thread count for consistency if we were to use parallel algorithms
    omp_set_num_threads(numberOfThreads);
    std::vector<ServiceRequest> recs = g_records; // Create a copy to sort, keeping original order intact

    std::sort(recs.begin(), recs.end(),
        [](const ServiceRequest& a, const ServiceRequest& b) {
            return a.createdDate < b.createdDate;
        });

    return recs;
}

// 6. Average latitude (OpenMP reduction)
// Calculates the sum of latitudes using parallel reduction.
double averageLatitude(int numberOfThreads) {
    if (g_records.empty()) return 0.0;
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_records.size());
    double sum = 0.0;

    // 'reduction(+:sum)' tells OpenMP to create private copies of 'sum' for each thread,
    // and then add them together into the global 'sum' at the end.
    #pragma omp parallel for reduction(+:sum)
    for (int i = 0; i < n; ++i) {
        sum += g_records[i].latitude;
    }

    return sum / g_records.size();
}
*/
// ---------------------------------------------------------------------------
// main() — benchmark harness
// ---------------------------------------------------------------------------

// Helper: returns current wall-clock time as a formatted string
static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::tm buf;
    localtime_r(&t, &buf);
    std::ostringstream oss;
    oss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

int main(int argc, char* argv[]) {
    std::string filename = "sample_data.csv";
    if (argc > 1) {
        filename = argv[1];
    } else {
        std::cout << "Usage: " << argv[0] << " <csv_filename> [num_threads]" << std::endl;
        std::cout << "No file provided, defaulting to: " << filename << std::endl;
    }

    // Allow thread count to be passed as a second argument for benchmarking;
    // otherwise default to hardware concurrency.
    int numberOfThreads = static_cast<int>(std::thread::hardware_concurrency());
    if (argc > 2) {
        numberOfThreads = std::atoi(argv[2]);
    }
    if (numberOfThreads <= 0) numberOfThreads = 1;
    std::cout << "\n[" << timestamp() << "] Thread count: " << numberOfThreads << std::endl;

    double memBefore = rssMemMB();
    std::cout << "[" << timestamp() << "] Memory before load: " << memBefore << " MB" << std::endl;

    g_records = loadData(filename);

    double memAfter = rssMemMB();
    std::cout << "[" << timestamp() << "] Memory after load: " << memAfter << " MB" << std::endl;
    std::cout << "[" << timestamp() << "] Memory delta: " << (memAfter - memBefore) << " MB" << std::endl;

    if (g_records.empty()) {
        std::cerr << "No records loaded. Exiting." << std::endl;
        return 1;
    }

    // Sample records
    std::cout << "\n=== Sample Records ===" << std::endl;
    for (int i = 0; i < 5 && i < static_cast<int>(g_records.size()); ++i) {
        const auto& r = g_records[i];
        std::cout << "#" << i + 1 << ": "
                  << r.uniqueKey << " | "
                  << r.createdDate.toString() << " | "
                  << r.borough << " | "
                  << r.complaintType << "\n";
    }

    // Benchmark helpers
    std::cout << "\n=== Query Outputs ===" << std::endl;

    auto measureVectorQuery = [&](const std::string& label, int runs, auto query) {
        using namespace std::chrono;
        std::cout << "\n[" << timestamp() << "] Starting query: " << label
                  << " (" << runs << " runs)" << std::endl;

        std::size_t count = 0;
        double totalTime = 0.0;

        for (int i = 0; i < runs; ++i) {
            auto iterStart = high_resolution_clock::now();
            auto res = query();
            auto iterEnd = high_resolution_clock::now();
            double iterTime = duration<double>(iterEnd - iterStart).count();
            totalTime += iterTime;

            if (i == 0) count = res.size();

            std::cout << "  [" << timestamp() << "] Run " << (i + 1) << "/" << runs
                      << ": " << iterTime << "s"
                      << " (result size=" << res.size() << ")" << std::endl;
        }

        std::cout << "[" << timestamp() << "] " << label
                  << " -> size=" << count
                  << ", total=" << totalTime << "s"
                  << ", avg=" << (totalTime / runs) << "s\n";
    };

    auto measureScalarQuery = [&](const std::string& label, int runs, auto query) {
        using namespace std::chrono;
        std::cout << "\n[" << timestamp() << "] Starting query: " << label
                  << " (" << runs << " runs)" << std::endl;

        double val = 0.0;
        double totalTime = 0.0;

        for (int i = 0; i < runs; ++i) {
            auto iterStart = high_resolution_clock::now();
            val = query();
            auto iterEnd = high_resolution_clock::now();
            double iterTime = duration<double>(iterEnd - iterStart).count();
            totalTime += iterTime;

            std::cout << "  [" << timestamp() << "] Run " << (i + 1) << "/" << runs
                      << ": " << iterTime << "s"
                      << " (value=" << val << ")" << std::endl;
        }

        std::cout << "[" << timestamp() << "] " << label
                  << " -> value=" << val
                  << ", total=" << totalTime << "s"
                  << ", avg=" << (totalTime / runs) << "s\n";
    };

    const int runs = 15;

    // Date range 2013
    DateTime start = DateTime::parse("01/01/2013 12:00:00 AM");
    DateTime end   = DateTime::parse("12/31/2013 11:59:59 PM");
    measureVectorQuery("date range 2013", runs,
        [&]() { return filterByCreatedDateRange(start, end, numberOfThreads); });

    // Borough BROOKLYN
    measureVectorQuery("borough BROOKLYN", runs,
        [&]() { return filterByBorough("BROOKLYN", numberOfThreads); });

    // Queries 3-6 disabled for this run
    // measureVectorQuery("complaint 'rodent'", runs, [&]() { return searchByComplaint("rodent", numberOfThreads); });
    // measureVectorQuery("sorted date", runs, [&]() { return sortByCreatedDate(numberOfThreads); });
    // measureVectorQuery("lat/lon box", runs, [&]() { return filterByLatLonBox(40.5, 40.9, -74.25, -73.7, numberOfThreads); });
    // measureScalarQuery("average latitude", runs, [&]() { return averageLatitude(numberOfThreads); });

    return 0;
}
