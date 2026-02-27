#pragma once
#include "ServiceRequest.h"
#include "DateTime.h"
#include <string>
#include <vector>
#include <cstddef>

// =============================================================================
// NYC311Analyzer
//
// All-in-one class for loading and querying the NYC 311 dataset.
// Mirrors the structure of FireDataAnalyzer:
//   - private records vector + private CSV helpers
//   - public load methods
//   - public query methods (each times itself and prints results)
//   - public printDataStatistics()
// =============================================================================
class NYC311Analyzer {
private:
    // -------------------------------------------------------------------------
    // Internal storage — all loaded records live here
    // -------------------------------------------------------------------------
    std::vector<ServiceRequest> records;

    // -------------------------------------------------------------------------
    // cleanString()
    // Strips leading/trailing double-quote characters from a raw CSV field.
    // e.g.  "BROOKLYN"  →  BROOKLYN
    // -------------------------------------------------------------------------
    std::string cleanString(const std::string& str);

    // -------------------------------------------------------------------------
    // parseCSVLine()
    // Splits one CSV line into individual field strings.
    // Handles RFC-4180 quoted fields (commas inside quotes, doubled quotes).
    // Returns a vector where index 0 = col 0, index 1 = col 1, etc.
    // -------------------------------------------------------------------------
    std::vector<std::string> parseCSVLine(const std::string& line);

public:
    // -------------------------------------------------------------------------
    // loadData()
    // Opens the CSV file at `filename`, skips the header row, parses every
    // data row into a ServiceRequest, and stores it in `records`.
    // Prints load time and total records when done.
    // -------------------------------------------------------------------------
    void loadData(const std::string& filename);

    // -------------------------------------------------------------------------
    // Query 1 — filterByDateRange()
    // Returns all records whose createdDate falls within [start, end].
    // Uses DateTime comparison operators (toKey() packed uint64 compare).
    // -------------------------------------------------------------------------
    std::vector<ServiceRequest> filterByDateRange(const DateTime& start,
                                                   const DateTime& end);

    // -------------------------------------------------------------------------
    // Query 2 — filterByBorough()
    // Returns all records matching the given borough (case-insensitive).
    // e.g. "BROOKLYN", "MANHATTAN", "BRONX", "QUEENS", "STATEN ISLAND"
    // -------------------------------------------------------------------------
    std::vector<ServiceRequest> filterByBorough(const std::string& borough);

    // -------------------------------------------------------------------------
    // Query 3 — filterByAgency()
    // Returns all records matching the agency code (case-insensitive).
    // e.g. "NYPD", "DOT", "DEP", "DOHMH"
    // -------------------------------------------------------------------------
    std::vector<ServiceRequest> filterByAgency(const std::string& agency);

    // -------------------------------------------------------------------------
    // Query 4 — filterByComplaintType()
    // Returns all records whose complaintType contains `keyword` (case-insensitive).
    // Substring match — slower than exact match but more flexible.
    // -------------------------------------------------------------------------
    std::vector<ServiceRequest> filterByComplaintType(const std::string& keyword);

    // -------------------------------------------------------------------------
    // Query 5 — filterByStatus()
    // Returns all records matching the given status (case-insensitive).
    // e.g. "Open", "Closed", "Pending"
    // -------------------------------------------------------------------------
    std::vector<ServiceRequest> filterByStatus(const std::string& status);

    // -------------------------------------------------------------------------
    // Query 6 — filterByZip()
    // Returns all records with the given zip code.
    // incidentZip is stored as uint32_t so this is a plain integer compare.
    // -------------------------------------------------------------------------
    std::vector<ServiceRequest> filterByZip(uint32_t zip);

    // -------------------------------------------------------------------------
    // Query 7 — filterByLatLonBox()
    // Returns all records whose coordinates fall inside the bounding box.
    // minLat/maxLat = south/north bound, minLon/maxLon = west/east bound.
    // -------------------------------------------------------------------------
    std::vector<ServiceRequest> filterByLatLonBox(double minLat, double maxLat,
                                                   double minLon, double maxLon);

    // -------------------------------------------------------------------------
    // printDataStatistics()
    // Prints a summary of what is loaded: total records, borough breakdown,
    // top complaint types, date range, and open vs closed counts.
    // -------------------------------------------------------------------------
    void printDataStatistics();

    // Simple accessor so main() can print the total without duplicating logic
    std::size_t size() const { return records.size(); }
};
