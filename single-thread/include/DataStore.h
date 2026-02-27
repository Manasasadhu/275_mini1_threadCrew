#pragma once
#include "IDataReader.h"
#include "ServiceRequest.h"
#include "DateTime.h"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// QueryResult
//   Lightweight result handle: a vector of const pointers into the store's
//   internal record array.  No record is copied; ownership stays with
//   DataStore.  Pointers remain valid as long as the DataStore is alive and
//   load() has not been called again.
// ---------------------------------------------------------------------------
using QueryResult = std::vector<const ServiceRequest*>;

// ---------------------------------------------------------------------------
// DataStore  —  Facade for loading and querying 311 service-request records.
//
//   Responsibilities:
//     1. Accept any IDataReader implementation (dependency injection).
//     2. Load records from the reader into contiguous storage.
//     3. Expose a clean query API for range/exact searches.
//
//   All queries in Phase 1 are linear scans (O(n)).  This is intentional:
//   it gives us a clean serial baseline for comparison in Phases 2 and 3.
//
//   Pattern: Facade  (hides reader + storage details behind one interface)
// ---------------------------------------------------------------------------
class DataStore {
public:
    // Inject the reader strategy at construction time.
    explicit DataStore(std::unique_ptr<IDataReader> reader);
    ~DataStore() = default;

    // Non-copyable (records_ could be gigabytes)
    DataStore(const DataStore&)            = delete;
    DataStore& operator=(const DataStore&) = delete;

    // ------------------------------------------------------------------
    // load: read all records via the injected reader.
    // Returns number of records loaded.
    // ------------------------------------------------------------------
    std::size_t load(const std::string& filePath);

    // ------------------------------------------------------------------
    // Accessors
    // ------------------------------------------------------------------
    std::size_t size()        const { return records_.size(); }
    std::size_t skippedRows() const;

    // Direct access for iteration (read-only)
    const std::vector<ServiceRequest>& records() const { return records_; }

    // ------------------------------------------------------------------
    // Query API  —  all return QueryResult (pointers into records_)
    // ------------------------------------------------------------------

    // Filter by open/closed date range  [start, end]
    QueryResult filterByCreatedDateRange(const DateTime& start,
                                         const DateTime& end) const;

    // Filter by borough (case-insensitive)
    QueryResult filterByBorough(const std::string& borough) const;

    // Filter by agency code  e.g. "NYPD", "DOT", "DOHMH"
    QueryResult filterByAgency(const std::string& agency) const;

    // Filter by complaint type (partial, case-insensitive substring)
    QueryResult filterByComplaintType(const std::string& keyword) const;

    // Filter by status  e.g. "Open", "Closed"
    QueryResult filterByStatus(const std::string& status) const;

    // Filter by zip code
    QueryResult filterByZip(uint32_t zip) const;

    // Bounding-box query on latitude / longitude
    QueryResult filterByLatLonBox(double minLat, double maxLat,
                                   double minLon, double maxLon) const;

    // Filter by council district
    QueryResult filterByCouncilDistrict(int16_t district) const;

private:
    std::unique_ptr<IDataReader>   reader_;
    std::vector<ServiceRequest>    records_;
};
