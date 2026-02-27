#include "DataStore.h"
#include <algorithm>
#include <cctype>
#include <string>

// ---------------------------------------------------------------------------
// Internal helper: case-insensitive equality
// ---------------------------------------------------------------------------
static bool iequal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// Internal helper: case-insensitive substring search
// ---------------------------------------------------------------------------
static bool icontains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(),   needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

// ---------------------------------------------------------------------------
// DataStore::DataStore
// ---------------------------------------------------------------------------
DataStore::DataStore(std::unique_ptr<IDataReader> reader)
    : reader_(std::move(reader)) {}

// ---------------------------------------------------------------------------
// DataStore::load
// ---------------------------------------------------------------------------
std::size_t DataStore::load(const std::string& filePath) {
    records_.clear();
    if (!reader_->open(filePath)) return 0;
    std::size_t n = reader_->readAll(records_);
    reader_->close();
    return n;
}

std::size_t DataStore::skippedRows() const {
    return reader_ ? reader_->skippedRows() : 0;
}

// ---------------------------------------------------------------------------
// filterByCreatedDateRange
// ---------------------------------------------------------------------------
QueryResult DataStore::filterByCreatedDateRange(const DateTime& start,
                                                const DateTime& end) const {
    QueryResult result;
    for (const auto& r : records_) {
        if (r.createdDate.valid &&
            r.createdDate >= start &&
            r.createdDate <= end)
            result.push_back(&r);
    }
    return result;
}

// ---------------------------------------------------------------------------
// filterByBorough
// ---------------------------------------------------------------------------
QueryResult DataStore::filterByBorough(const std::string& borough) const {
    QueryResult result;
    for (const auto& r : records_)
        if (iequal(r.borough, borough))
            result.push_back(&r);
    return result;
}

// ---------------------------------------------------------------------------
// filterByAgency
// ---------------------------------------------------------------------------
QueryResult DataStore::filterByAgency(const std::string& agency) const {
    QueryResult result;
    for (const auto& r : records_)
        if (iequal(r.agency, agency))
            result.push_back(&r);
    return result;
}

// ---------------------------------------------------------------------------
// filterByComplaintType  (substring match)
// ---------------------------------------------------------------------------
QueryResult DataStore::filterByComplaintType(const std::string& keyword) const {
    QueryResult result;
    for (const auto& r : records_)
        if (icontains(r.complaintType, keyword))
            result.push_back(&r);
    return result;
}

// ---------------------------------------------------------------------------
// filterByStatus
// ---------------------------------------------------------------------------
QueryResult DataStore::filterByStatus(const std::string& status) const {
    QueryResult result;
    for (const auto& r : records_)
        if (iequal(r.status, status))
            result.push_back(&r);
    return result;
}

// ---------------------------------------------------------------------------
// filterByZip
// ---------------------------------------------------------------------------
QueryResult DataStore::filterByZip(uint32_t zip) const {
    QueryResult result;
    for (const auto& r : records_)
        if (r.incidentZip == zip)
            result.push_back(&r);
    return result;
}

// ---------------------------------------------------------------------------
// filterByLatLonBox
// ---------------------------------------------------------------------------
QueryResult DataStore::filterByLatLonBox(double minLat, double maxLat,
                                         double minLon, double maxLon) const {
    QueryResult result;
    for (const auto& r : records_) {
        if (r.latitude  >= minLat && r.latitude  <= maxLat &&
            r.longitude >= minLon && r.longitude <= maxLon)
            result.push_back(&r);
    }
    return result;
}

// ---------------------------------------------------------------------------
// filterByCouncilDistrict
// ---------------------------------------------------------------------------
QueryResult DataStore::filterByCouncilDistrict(int16_t district) const {
    QueryResult result;
    for (const auto& r : records_)
        if (r.councilDistrict == district)
            result.push_back(&r);
    return result;
}
