#include "ServiceRequest.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <mach/mach.h>
#include <algorithm> 

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
    const std::size_t RECORD_LIMIT = 16000000; // 10 Million record limit for testing; adjust as needed

    // Skip header
    if (std::getline(file, line)) {
        lineCount++;
        std::cout << "Skipped header: " << line.substr(0, 100) << "..." << std::endl;
    }

    // Read data lines
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

// global dataset container
static std::vector<ServiceRequest> g_records;

// 1. range query on createdDate
std::vector<ServiceRequest> filterByCreatedDateRange(const DateTime& start,
                                                     const DateTime& end) {
    std::vector<ServiceRequest> out;
    for (const auto& r : g_records) {
        if (r.createdDate >= start && r.createdDate <= end)
            out.push_back(r);
    }
    return out;
}

// 2. exact match (case-insensitive) on borough
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

// 3. substring search on complaintType (case-insensitive)
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

// 4. bounding box on latitude/longitude
// Returns pointers to the matching records instead of copying full objects.
// This keeps the memory footprint small even when the result set is large.
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

// 5. sort a copy by createdDate ascending 
std::vector<ServiceRequest> sortByCreatedDate() {
    std::vector<ServiceRequest> recs = g_records;  // copy
    std::sort(recs.begin(), recs.end(), [](const ServiceRequest &a, const ServiceRequest &b) {
        return a.createdDate < b.createdDate;
    });
    return recs;
}

// 6. compute average latitude of the loaded records - demonstrates an aggregation (reduce) operation.
double averageLatitude() {
    if (g_records.empty()) return 0.0;
    double sum = 0.0;
    for (const auto &r : g_records) sum += r.latitude;
    return sum / g_records.size();
}

// Takes all 20 million records and groups them by zip code, building a summary (total complaints, breakdown by type/agency/status) for each unique zip code found in the dataset.

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

    // print a few sample records for verification
    std::cout << "\n=== Sample Records ===" << std::endl;
    for (int i = 0; i < 5 && i < (int)g_records.size(); ++i) {
        const auto &r = g_records[i];
        std::cout << "#" << i+1 << ": "
                  << r.uniqueKey << " | "
                  << r.createdDate.toString() << " | "
                  << r.borough << " | "
                  << r.complaintType << "\n";
    }

    // run each example query to verify functionality and measure performance
    std::cout << "\n=== Query Outputs ===" << std::endl;

    // helper lambdas for timing
    auto measureVectorQuery = [&](const std::string &label, int runs, auto query) {
        using namespace std::chrono;
        std::size_t count = 0;
        auto startTime = high_resolution_clock::now();
        for (int i = 0; i < runs; ++i) {
            auto res = query();           // result must support .size()
            if (i == 0)
                count = res.size();
        }
        auto endTime = high_resolution_clock::now();
        double total = duration<double>(endTime - startTime).count();
        std::cout << label << " -> size=" << count
                  << ", total=" << total << "s"
                  << ", avg=" << (total / runs) << "s\n";
    };

    auto measureScalarQuery = [&](const std::string &label, int runs, auto query) {
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

    const int runs = 15;  // number of repetitions for timing

    // date range query
    DateTime start = DateTime::parse("01/01/2013 12:00:00 AM");
    DateTime end   = DateTime::parse("12/31/2013 11:59:59 PM");
    measureVectorQuery("date range 2013", runs,
                       [&](){ return filterByCreatedDateRange(start, end); });

    // borough filter
    measureVectorQuery("borough BROOKLYN", runs,
                       [&](){ return filterByBorough("BROOKLYN"); });

    // complaint substring
    measureVectorQuery("complaint 'rodent'", runs,
                       [&](){ return searchByComplaint("rodent"); });

    // lat/lon box example (rough NYC box)
    measureVectorQuery("lat/lon box", runs,
                       [&](){ return filterByLatLonBox(40.5, 40.9, -74.25, -73.7); });

    // average latitude
    measureScalarQuery("average latitude", runs, [](){ return averageLatitude(); });

    return 0;
}
