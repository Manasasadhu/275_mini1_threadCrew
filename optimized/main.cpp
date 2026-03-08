#include "ServiceRequest.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstring>
#include <omp.h>
#include <mach/mach.h>
#include <map>
#include <cmath>
#include <unordered_map>
#include <string_view>

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
// Limit for maximum records to load
constexpr std::size_t MAX_RECORDS = 10000000;
#include <atomic>

// Single-threaded, streaming loader (memory efficient)
std::vector<ServiceRequest> loadDataParallel(const std::string& filename, int /*numberOfThreads*/) {
    constexpr std::size_t MAX_RECORDS = 10000000;
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "[SINGLE-THREAD FALLBACK] Loading NYC 311 data from: " << filename << std::endl;
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
    // Read data lines
    while (std::getline(file, line)) {
        lineCount++;
        if (lineCount % 1000000 == 0) {
            std::cout << "Processed " << lineCount << " lines, loaded " << validRecords << " records..." << std::endl;
        }
        auto fields = parseCSVLine(line);
        ServiceRequest req;
        if (req.fromFields(fields)) {
            records.push_back(std::move(req));
            validRecords++;
            if (validRecords >= MAX_RECORDS) {
                std::cout << "Reached limit of " << MAX_RECORDS << " records, stopping load." << std::endl;
                break;
            }
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    std::cout << "Loaded " << validRecords << " records in " << duration.count() << " seconds" << std::endl;
    std::cout << "Total lines processed: " << lineCount << std::endl;
    return records;
}

// 3. Complaint substring search (case-insensitive)
std::vector<ServiceRequest> searchByComplaint(const std::string& keyword,
                                              int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_complaintTypeLower.size());
    std::vector<std::vector<int>> localResults(numberOfThreads);

    std::string key = keyword;
    for (auto& c : key) c = static_cast<char>(std::tolower(c));

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        const std::string& comp = g_complaintTypeLower[i];
        if (comp.find(key) != std::string::npos) {
            int t = omp_get_thread_num();
            localResults[t].push_back(i);
        }
    }

    std::vector<ServiceRequest> out;
    for (int t = 0; t < numberOfThreads; ++t)
        for (int idx : localResults[t])
            out.push_back(g_records[idx]);
    return out;
}

// SIMD-friendly, compiler-optimizable version of query 3 (improved)
std::vector<ServiceRequest> searchByComplaint_SIMD(const std::string& keyword, int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_complaintTypeLower.size());
    std::vector<std::vector<int>> localResults(numberOfThreads);

    // Lowercase the keyword once (SIMD-friendly)
    std::string key = keyword;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        const std::string& comp = g_complaintTypeLower[i];
        if (comp.find(key) != std::string::npos) {
            int t = omp_get_thread_num();
            localResults[t].push_back(i);
        }
    }

    std::vector<ServiceRequest> out;
    for (int t = 0; t < numberOfThreads; ++t)
        for (int idx : localResults[t])
            out.push_back(g_records[idx]);
    return out;
}

// SIMD-friendly, compiler-optimizable version of query 4 (lat/lon bounding box)
std::vector<const ServiceRequest*> filterByLatLonBox_SIMD(double minLat, double maxLat,
                                                          double minLon, double maxLon,
                                                          int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_latitude.size());
    std::vector<std::vector<int>> localResults(numberOfThreads);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        double lat = g_latitude[i];
        double lon = g_longitude[i];
        bool inBox = (lat >= minLat) & (lat <= maxLat) & (lon >= minLon) & (lon <= maxLon);
        if (inBox) {
            int t = omp_get_thread_num();
            localResults[t].push_back(i);
        }
    }

    std::vector<const ServiceRequest*> out;
    for (int t = 0; t < numberOfThreads; ++t)
        for (int idx : localResults[t])
            out.push_back(&g_records[idx]);
    return out;
}


int main(int argc, char* argv[]) {
    std::string filename = "/Users/aravindreddy/Downloads/SJSU ClassWork/275 EAD/Mini1_Datasets/311_combined.csv.";
    int numberOfThreads = 6;
    if (argc > 1 && std::string(argv[1]).length() > 0) {
        filename = argv[1];
    }
    if (argc > 2) {
        numberOfThreads = std::atoi(argv[2]);
    } else {
        // Check OMP_THREAD_COUNT environment variable if not provided as argument
        const char* env_threads = std::getenv("OMP_THREAD_COUNT");
        if (env_threads) {
            numberOfThreads = std::atoi(env_threads);
        }
    }
    if (numberOfThreads <= 0) numberOfThreads = static_cast<int>(std::thread::hardware_concurrency());
    std::cout << "Thread count: " << numberOfThreads << std::endl;

    double memBefore = rssMemMB();
    std::cout << "Memory before load: " << memBefore << " MB" << std::endl;

    g_records = loadDataParallel(filename, numberOfThreads);

    double memAfter = rssMemMB();
    std::cout << "Memory after load: " << memAfter << " MB" << std::endl;
    std::cout << "Memory delta: " << (memAfter - memBefore) << " MB" << std::endl;

    if (g_records.empty()) {
        std::cerr << "No records loaded. Exiting." << std::endl;
        return 1;
    }

    // Benchmark helpers
    std::cout << "\n Query Outputs -" << std::endl;

    auto measureVectorQuery = [&](const std::string& label, int runs, auto query) {
        using namespace std::chrono;
        std::size_t count = 0;
        auto startTime = high_resolution_clock::now();
        for (int i = 0; i < runs; ++i) {
            auto res = query();
            if (i == 0) count = res.size();
        }
        auto endTime = high_resolution_clock::now();
        double total = duration<double>(endTime - startTime).count();
        std::cout << label << " -> size=" << count
                  << ", total=" << total << "s"
                  << ", avg=" << (total / runs) << "s\n";
    };

    auto measureScalarQuery = [&](const std::string& label, int runs, auto query) {
        using namespace std::chrono;
        double val = 0.0;
        auto startTime = high_resolution_clock::now();
        for (int i = 0; i < runs; ++i) {
            val = query();
        }
        auto endTime = high_resolution_clock::now();
        double total = duration<double>(endTime - startTime).count();
        std::cout << label << " -> value=" << val
                  << ", total=" << total << "s"
                  << ", avg=" << (total / runs) << "s\n";
    };

    const int runs = 15;

    // Complaint "rodent" (SIMD-optimized)
    measureVectorQuery("complaint 'rodent' (SIMD)", runs,
        [&]() { return searchByComplaint_SIMD("rodent", numberOfThreads); });

    // Lat/lon box (NYC, SIMD-optimized)
    measureVectorQuery("lat/lon box (SIMD)", runs,
        [&]() { return filterByLatLonBox_SIMD(40.5, 40.9, -74.25, -73.7, numberOfThreads); });

    
    return 0;
}