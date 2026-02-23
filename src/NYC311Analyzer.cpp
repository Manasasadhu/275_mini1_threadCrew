#include "NYC311Analyzer.h"
#include <algorithm>   // std::min, std::tolower, std::search
#include <cctype>      // std::tolower
#include <chrono>      // timing inside each query
#include <iostream>
#include <fstream>
#include <map>

// =============================================================================
// Private helpers
// =============================================================================

// -----------------------------------------------------------------------------
// cleanString — strips surrounding double-quotes if present
// -----------------------------------------------------------------------------
std::string NYC311Analyzer::cleanString(const std::string& str) {
    std::string cleaned = str;
    if (!cleaned.empty() && cleaned.front() == '"') cleaned.erase(0, 1);
    if (!cleaned.empty() && cleaned.back()  == '"') cleaned.pop_back();
    return cleaned;
}

// -----------------------------------------------------------------------------
// parseCSVLine — splits one CSV line respecting quoted fields
//
// Mirrors FireDataAnalyzer::parseCSVLine but also handles the doubled-quote
// escape sequence ("" inside a quoted field means a literal ").
// The NYC 311 Resolution Description field often contains commas and quotes,
// so this is necessary.
// -----------------------------------------------------------------------------
std::vector<std::string> NYC311Analyzer::parseCSVLine(const std::string& line) {
    std::vector<std::string> fields;
    fields.reserve(44);          // NYC 311 has exactly 44 columns
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
                    ++i;           // skip the second quote
                } else {
                    inQuotes = false;   // closing quote
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
    fields.push_back(cleanString(current));   // last field (no trailing comma)
    return fields;
}

// =============================================================================
// loadData
// =============================================================================
void NYC311Analyzer::loadData(const std::string& filename) {
    auto start = std::chrono::high_resolution_clock::now();

    std::cout << "Loading NYC 311 data from: " << filename << std::endl;

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return;
    }

    // Pre-allocate to avoid repeated reallocation across 11 million records
    records.reserve(12'000'000);

    // Read and discard the header row
    std::string line;
    std::getline(file, line);

    int lineCount  = 0;
    int skipCount  = 0;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        lineCount++;

        try {
            std::vector<std::string> fields = parseCSVLine(line);

            ServiceRequest record;
            if (record.fromFields(fields)) {
                records.push_back(record);
            } else {
                skipCount++;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing line " << lineCount
                      << ": " << e.what() << std::endl;
            skipCount++;
        }
    }

    file.close();

    auto end      = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Loaded "   << records.size() << " records in "
              << duration.count() << " milliseconds" << std::endl;
    std::cout << "Skipped "  << skipCount << " malformed rows" << std::endl;
}

// =============================================================================
// Internal string helpers (case-insensitive comparison)
// =============================================================================

// Exact case-insensitive match
static bool iequal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

// Case-insensitive substring search (haystack contains needle?)
static bool icontains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(),   needle.end(),
        [](char a, char b){
            return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
        });
    return it != haystack.end();
}

// =============================================================================
// Query 1 — filterByDateRange
// =============================================================================
std::vector<ServiceRequest> NYC311Analyzer::filterByDateRange(
        const DateTime& start, const DateTime& end) {

    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<ServiceRequest> results;
    for (const auto& record : records) {
        if (record.createdDate.valid &&
            record.createdDate >= start &&
            record.createdDate <= end) {
            results.push_back(record);
        }
    }

    auto t1       = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

    std::cout << "Query completed in " << duration.count() << " microseconds" << std::endl;
    std::cout << "Found " << results.size()
              << " records between " << start.toString()
              << " and " << end.toString() << std::endl;

    return results;
}

// =============================================================================
// Query 2 — filterByBorough
// =============================================================================
std::vector<ServiceRequest> NYC311Analyzer::filterByBorough(const std::string& borough) {
    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<ServiceRequest> results;
    for (const auto& record : records) {
        if (iequal(record.borough, borough))
            results.push_back(record);
    }

    auto t1       = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

    std::cout << "Query completed in " << duration.count() << " microseconds" << std::endl;
    std::cout << "Found " << results.size()
              << " records for borough: " << borough << std::endl;

    return results;
}

// =============================================================================
// Query 3 — filterByAgency
// =============================================================================
std::vector<ServiceRequest> NYC311Analyzer::filterByAgency(const std::string& agency) {
    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<ServiceRequest> results;
    for (const auto& record : records) {
        if (iequal(record.agency, agency))
            results.push_back(record);
    }

    auto t1       = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

    std::cout << "Query completed in " << duration.count() << " microseconds" << std::endl;
    std::cout << "Found " << results.size()
              << " records for agency: " << agency << std::endl;

    return results;
}

// =============================================================================
// Query 4 — filterByComplaintType  (substring match)
// =============================================================================
std::vector<ServiceRequest> NYC311Analyzer::filterByComplaintType(const std::string& keyword) {
    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<ServiceRequest> results;
    for (const auto& record : records) {
        if (icontains(record.complaintType, keyword))
            results.push_back(record);
    }

    auto t1       = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

    std::cout << "Query completed in " << duration.count() << " microseconds" << std::endl;
    std::cout << "Found " << results.size()
              << " records with complaint type containing: " << keyword << std::endl;

    return results;
}

// =============================================================================
// Query 5 — filterByStatus
// =============================================================================
std::vector<ServiceRequest> NYC311Analyzer::filterByStatus(const std::string& status) {
    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<ServiceRequest> results;
    for (const auto& record : records) {
        if (iequal(record.status, status))
            results.push_back(record);
    }

    auto t1       = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

    std::cout << "Query completed in " << duration.count() << " microseconds" << std::endl;
    std::cout << "Found " << results.size()
              << " records with status: " << status << std::endl;

    return results;
}

// =============================================================================
// Query 6 — filterByZip
// =============================================================================
std::vector<ServiceRequest> NYC311Analyzer::filterByZip(uint32_t zip) {
    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<ServiceRequest> results;
    for (const auto& record : records) {
        if (record.incidentZip == zip)
            results.push_back(record);
    }

    auto t1       = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

    std::cout << "Query completed in " << duration.count() << " microseconds" << std::endl;
    std::cout << "Found " << results.size()
              << " records for zip: " << zip << std::endl;

    return results;
}

// =============================================================================
// Query 7 — filterByLatLonBox
// =============================================================================
std::vector<ServiceRequest> NYC311Analyzer::filterByLatLonBox(
        double minLat, double maxLat, double minLon, double maxLon) {

    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<ServiceRequest> results;
    for (const auto& record : records) {
        if (record.latitude  >= minLat && record.latitude  <= maxLat &&
            record.longitude >= minLon && record.longitude <= maxLon)
            results.push_back(record);
    }

    auto t1       = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

    std::cout << "Query completed in " << duration.count() << " microseconds" << std::endl;
    std::cout << "Found " << results.size()
              << " records in bounding box ["
              << minLat << "," << maxLat << "] ["
              << minLon << "," << maxLon << "]" << std::endl;

    return results;
}

// =============================================================================
// printDataStatistics
// =============================================================================
void NYC311Analyzer::printDataStatistics() {
    if (records.empty()) {
        std::cout << "No data loaded." << std::endl;
        return;
    }

    // Tally counts per borough and per complaint type
    std::map<std::string, int> boroughCounts;
    std::map<std::string, int> complaintCounts;
    std::map<std::string, int> statusCounts;
    std::string minDate = "9999-99-99";
    std::string maxDate = "0000-00-00";

    for (const auto& r : records) {
        boroughCounts[r.borough]++;
        complaintCounts[r.complaintType]++;
        statusCounts[r.status]++;

        // Track date range using createdDate toString (YYYY-MM-DD HH:MM:SS)
        if (r.createdDate.valid) {
            std::string d = r.createdDate.toString().substr(0, 10);
            if (d < minDate) minDate = d;
            if (d > maxDate) maxDate = d;
        }
    }

    std::cout << "\n=== DATA STATISTICS ===" << std::endl;
    std::cout << "Total records : " << records.size()   << std::endl;
    std::cout << "Date range    : " << minDate << " to " << maxDate << std::endl;

    std::cout << "\nBorough distribution:" << std::endl;
    for (const auto& pair : boroughCounts)
        std::cout << "  " << pair.first << ": " << pair.second << " records" << std::endl;

    std::cout << "\nStatus distribution:" << std::endl;
    for (const auto& pair : statusCounts)
        std::cout << "  " << pair.first << ": " << pair.second << " records" << std::endl;

    // Top 10 complaint types by count
    std::vector<std::pair<int,std::string>> sorted;
    sorted.reserve(complaintCounts.size());
    for (const auto& p : complaintCounts)
        sorted.push_back({p.second, p.first});
    std::sort(sorted.rbegin(), sorted.rend());   // descending by count

    std::cout << "\nTop 10 complaint types:" << std::endl;
    int shown = 0;
    for (const auto& p : sorted) {
        if (shown++ >= 10) break;
        std::cout << "  " << p.second << ": " << p.first << " records" << std::endl;
    }
}
