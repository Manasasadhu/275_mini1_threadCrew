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
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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
std::vector<ServiceRequest> loadDataParallel(const std::string& filename,
                                              int numberOfThreads) {
    if (numberOfThreads <= 0) numberOfThreads = 1;

    auto wallStart = std::chrono::high_resolution_clock::now();
    std::cout << "Loading NYC 311 data from: " << filename << std::endl;

    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return {};
    }

    // Read entire file into a memory buffer for fast access
    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string buffer(static_cast<std::size_t>(fileSize), '\0');
    file.read(&buffer[0], fileSize);
    file.close();

    // Find end of header line to skip it
    std::size_t headerEnd = buffer.find('\n');
    if (headerEnd == std::string::npos) {
        std::cout << "File is empty or has only a header. 0 records loaded." << std::endl;
        return {};
    }
    std::size_t dataStart = headerEnd + 1;

    // Divide the file content into equal chunks for each thread
    std::size_t dataLen = buffer.size() - dataStart;
    std::size_t chunkSize = dataLen / numberOfThreads;

    std::vector<std::size_t> chunkStarts(numberOfThreads);
    std::vector<std::size_t> chunkEnds(numberOfThreads);

    chunkStarts[0] = dataStart;
    for (int t = 1; t < numberOfThreads; ++t) {
        std::size_t pos = dataStart + t * chunkSize;
        // Adjust split position: scan forward to next newline to ensure we don't split a line in the middle
        while (pos < buffer.size() && buffer[pos] != '\n') ++pos;
        if (pos < buffer.size()) ++pos; // move past the newline
        chunkStarts[t] = pos;
        chunkEnds[t - 1] = pos;
    }
    chunkEnds[numberOfThreads - 1] = buffer.size();

    // Print chunk info for debugging/verification
    for (int t = 0; t < numberOfThreads; ++t) {
        std::cout << "  Chunk " << t << ": bytes ["
                  << chunkStarts[t] << ", " << chunkEnds[t] << ")"
                  << " size=" << (chunkEnds[t] - chunkStarts[t]) << std::endl;
    }

    // Per-thread local storage to avoid locking/contention on a shared vector
    std::vector<std::vector<ServiceRequest>> localResults(numberOfThreads);
    std::vector<std::size_t> localLineCount(numberOfThreads, 0);

    // Spawn threads to process each chunk independently
    std::vector<std::thread> threads;
    for (int t = 0; t < numberOfThreads; ++t) {
        threads.emplace_back([&, t]() {
            std::size_t start = chunkStarts[t];
            std::size_t end   = chunkEnds[t];
            std::string line;
            line.reserve(2048);

            for (std::size_t pos = start; pos < end; ) {
                // Extract one line from the buffer
                line.clear();
                while (pos < end && buffer[pos] != '\n') {
                    if (buffer[pos] != '\r')
                        line += buffer[pos];
                    ++pos;
                }
                if (pos < end) ++pos; // skip newline

                if (line.empty()) continue;
                localLineCount[t]++;

                // Parse the line and create a ServiceRequest object
                auto fields = parseCSVLine(line);
                ServiceRequest req;
                if (req.fromFields(fields)) {
                    localResults[t].push_back(std::move(req));
                }
            }
        });
    }

    // Wait for all threads to complete
    for (auto& th : threads) th.join();

    // Merge thread-local results into the final result vector
    std::size_t totalLines = 1; // count header
    std::size_t totalValid = 0;
    for (int t = 0; t < numberOfThreads; ++t) {
        totalLines += localLineCount[t];
        totalValid += localResults[t].size();
    }

    std::vector<ServiceRequest> result;
    result.reserve(totalValid);
    for (int t = 0; t < numberOfThreads; ++t) {
        result.insert(result.end(),
                      std::make_move_iterator(localResults[t].begin()),
                      std::make_move_iterator(localResults[t].end()));
    }

    auto wallEnd = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(wallEnd - wallStart).count();

    std::cout << "Loaded " << totalValid << " records in "
              << elapsed << " seconds" << std::endl;
    std::cout << "Total lines processed: " << totalLines << std::endl;

    return result;
}

// ---------------------------------------------------------------------------
// Parallel query functions (OpenMP)
// These functions use OpenMP for data parallelism.
// ---------------------------------------------------------------------------

// 1. Date range filter
// Uses OpenMP to iterate over the global dataset in parallel chunks.
std::vector<ServiceRequest> filterByCreatedDateRange(const DateTime& start,
                                                     const DateTime& end,
                                                     int numberOfThreads) {
    omp_set_num_threads(numberOfThreads); // Set the number of threads for this parallel region
    int n = static_cast<int>(g_records.size());
    
    // Thread-local vectors to store results, avoiding race conditions
    std::vector<std::vector<ServiceRequest>> localResults(numberOfThreads);

    // Parallel loop: Divide the loop iterations among threads (static schedule)
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        if (g_records[i].createdDate >= start && g_records[i].createdDate <= end) {
            int t = omp_get_thread_num(); // Get ID of current thread
            localResults[t].push_back(g_records[i]);
        }
    }

    // Combine results from all threads
    std::vector<ServiceRequest> out;
    for (int t = 0; t < numberOfThreads; ++t)
        out.insert(out.end(), localResults[t].begin(), localResults[t].end());
    return out;
}

// 2. Borough filter (case-insensitive)
std::vector<ServiceRequest> filterByBorough(const std::string& borough,
                                            int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(g_records.size());
    std::vector<std::vector<ServiceRequest>> localResults(numberOfThreads);

    std::string target = borough;
    for (auto& c : target) c = static_cast<char>(std::toupper(c));

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        std::string b = g_records[i].borough;
        for (auto& c : b) c = static_cast<char>(std::toupper(c));
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
