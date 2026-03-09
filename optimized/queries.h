#pragma once

#include "ServiceRequest.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

uint64_t parseDateKey(const std::string& s);

struct ZoneStatsOoA {
    std::size_t totalCount = 0;
    std::unordered_map<std::string, std::size_t> byComplaintType;
};

// QUERY 1 — Date Range Filter (OoA + OpenMP)
// Returns indices i where createdKey[i] in [startKey, endKey]
std::vector<std::size_t> filterByCreatedDateRangeOoA_omp(
    const ServiceRequestOoA& data,
    uint64_t startKey,
    uint64_t endKey
);

// QUERY 2 — Borough Filter (OoA + OpenMP)
// boroughUpper must be uppercase (e.g. "BROOKLYN")
std::vector<std::size_t> filterByBoroughOoA_omp(
    const ServiceRequestOoA& data,
    const std::string& boroughUpper
);

// QUERY 3 — Complaint Substring Search 
std::vector<std::size_t> searchByComplaintOoA(
    const ServiceRequestOoA& data,
    const std::string& keyword
);

// QUERY 4 — Lat/Lon Bounding Box (OoA + OpenMP)
std::vector<std::size_t> filterByLatLonBoxOoA(
    const ServiceRequestOoA& data,
    double minLat,
    double maxLat,
    double minLon,
    double maxLon
);

// QUERY 5 — Average Latitude (OoA + OpenMP Reduction)
double averageLatitudeOoA_omp(
    const ServiceRequestOoA& data
);

// QUERY 6 — Borough Aggregation (OoA + OpenMP Fast)
// Produces: borough -> {totalCount, complaint histogram}
std::unordered_map<std::string, ZoneStatsOoA>
aggregateByBoroughOoA_omp_fast(
    const ServiceRequestOoA& data
);

// Pretty printing helper
void printTopComplaintPerBorough(
    const std::unordered_map<std::string, ZoneStatsOoA>& zones
);