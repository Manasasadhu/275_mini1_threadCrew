#include "queries.h"

#include <iostream>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <omp.h>

static uint8_t to24h(uint8_t h, bool isPM) {
    if (!isPM) return (h == 12) ? 0 : h;
    return (h == 12) ? 12 : static_cast<uint8_t>(h + 12);
}

// Parse "MM/DD/YYYY HH:MM:SS AM/PM" -> packed key
uint64_t parseDateKey(const std::string& s) {
    if (s.empty()) return 0;

    unsigned mm=0, dd=0, yyyy=0, hh=0, mi=0, ss=0;
    char ampm[3] = {};
    int n = std::sscanf(s.c_str(), "%u/%u/%u %u:%u:%u %2s",
                        &mm, &dd, &yyyy, &hh, &mi, &ss, ampm);
    if (n < 7) return 0;

    uint8_t hour24 = to24h(static_cast<uint8_t>(hh), (ampm[0]=='P' || ampm[0]=='p'));

    return (static_cast<uint64_t>(yyyy) << 40) |
           (static_cast<uint64_t>(mm)   << 32) |
           (static_cast<uint64_t>(dd)   << 24) |
           (static_cast<uint64_t>(hour24) << 16) |
           (static_cast<uint64_t>(mi)   << 8) |
           static_cast<uint64_t>(ss);
}

// QUERY 1 — Date Range Filter
std::vector<std::size_t> filterByCreatedDateRangeOoA_omp(
    const ServiceRequestOoA& data,
    uint64_t startKey,
    uint64_t endKey
) {
    const std::size_t n = data.createdKey.size();
    std::vector<std::size_t> out;
    if (n == 0) return out;

    // Mark array to avoid concurrent push_back
    std::vector<unsigned char> keep(n, 0);

    #pragma omp parallel for schedule(static)
    for (std::size_t i = 0; i < n; ++i) {
        uint64_t k = data.createdKey[i];
        if (k >= startKey && k <= endKey) keep[i] = 1;
    }

    out.reserve(n / 10 + 1);
    for (std::size_t i = 0; i < n; ++i) {
        if (keep[i]) out.push_back(i);
    }
    return out;
}

// QUERY 2 — Borough Filter
std::vector<std::size_t> filterByBoroughOoA_omp(
    const ServiceRequestOoA& data,
    const std::string& boroughUpper
) {
    const std::size_t n = data.boroughUpper.size();
    std::vector<std::size_t> out;
    if (n == 0) return out;

    std::vector<unsigned char> keep(n, 0);

    #pragma omp parallel for schedule(static)
    for (std::size_t i = 0; i < n; ++i) {
        // boroughUpper already precomputed in loader
        if (!data.boroughUpper[i].empty() && data.boroughUpper[i] == boroughUpper) {
            keep[i] = 1;
        }
    }

    out.reserve(n / 10 + 1);
    for (std::size_t i = 0; i < n; ++i) {
        if (keep[i]) out.push_back(i);
    }
    return out;
}

// QUERY 3 — Complaint Substring Search
std::vector<std::size_t> searchByComplaintOoA(
    const ServiceRequestOoA& data,
    const std::string& keyword
) {
    const int n = static_cast<int>(data.complaintTypeLower.size());
    if (n <= 0) return {};

    // Determine actual OpenMP thread count used
    const int T = omp_get_max_threads();

    std::vector<std::vector<std::size_t>> localResults(T);

    std::string key = keyword;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        const std::string& comp = data.complaintTypeLower[static_cast<std::size_t>(i)];
        if (comp.find(key) != std::string::npos) {
            int t = omp_get_thread_num();
            localResults[t].push_back(static_cast<std::size_t>(i));
        }
    }

    std::vector<std::size_t> out;
    for (int t = 0; t < T; ++t)
        out.insert(out.end(), localResults[t].begin(), localResults[t].end());
    return out;
}

// QUERY 4 — Lat/Lon Bounding Box
std::vector<std::size_t> filterByLatLonBoxOoA(
    const ServiceRequestOoA& data,
    double minLat,
    double maxLat,
    double minLon,
    double maxLon
) {
    const int n = static_cast<int>(data.latitude.size());
    if (n <= 0) return {};

    const int T = omp_get_max_threads();
    std::vector<std::vector<std::size_t>> localResults(T);

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        double lat = data.latitude[idx];
        double lon = data.longitude[idx];

        if (lat >= minLat && lat <= maxLat &&
            lon >= minLon && lon <= maxLon) {
            int t = omp_get_thread_num();
            localResults[t].push_back(idx);
        }
    }

    std::vector<std::size_t> out;
    for (int t = 0; t < T; ++t)
        out.insert(out.end(), localResults[t].begin(), localResults[t].end());
    return out;
}

// QUERY 5 — Average Latitude (Reduction)
double averageLatitudeOoA_omp(const ServiceRequestOoA& data) {
    const std::size_t n = data.latitude.size();
    if (n == 0) return 0.0;

    double sum = 0.0;
    #pragma omp parallel for reduction(+:sum) schedule(static)
    for (std::size_t i = 0; i < n; ++i) {
        sum += data.latitude[i];
    }

    return sum / static_cast<double>(n);
}

// QUERY 6 — Borough Aggregation (Fast)
std::unordered_map<std::string, ZoneStatsOoA>
aggregateByBoroughOoA_omp_fast(const ServiceRequestOoA& data) {
    const std::size_t n = data.boroughUpper.size();
    std::unordered_map<std::string, ZoneStatsOoA> result;
    if (n == 0) return result;

    const int T = omp_get_max_threads();

    auto boroughIndex = [&](const std::string& b) -> int {
        if (b == "BRONX") return 0;
        if (b == "BROOKLYN") return 1;
        if (b == "MANHATTAN") return 2;
        if (b == "QUEENS") return 3;
        if (b == "STATEN ISLAND") return 4;
        return 5;
    };

    // Thread-local: 6 buckets per thread
    std::vector<std::array<ZoneStatsOoA, 6>> local(T);

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();

        #pragma omp for schedule(static)
        for (std::size_t i = 0; i < n; ++i) {
            int b = boroughIndex(data.boroughUpper[i]);

            local[tid][b].totalCount++;

            const std::string& comp = data.complaintType[i];
            if (!comp.empty()) {
                local[tid][b].byComplaintType[comp]++;
            }
        }
    }

    // Merge buckets (single-thread)
    std::array<ZoneStatsOoA, 6> merged;

    for (int t = 0; t < T; ++t) {
        for (int b = 0; b < 6; ++b) {
            merged[b].totalCount += local[t][b].totalCount;
            for (const auto& kv : local[t][b].byComplaintType) {
                merged[b].byComplaintType[kv.first] += kv.second;
            }
        }
    }

    result.reserve(8);
    result["BRONX"] = std::move(merged[0]);
    result["BROOKLYN"] = std::move(merged[1]);
    result["MANHATTAN"] = std::move(merged[2]);
    result["QUEENS"] = std::move(merged[3]);
    result["STATEN ISLAND"] = std::move(merged[4]);
    result["(unknown)"] = std::move(merged[5]);

    return result;
}

void printTopComplaintPerBorough(
    const std::unordered_map<std::string, ZoneStatsOoA>& zones
) {
    std::vector<std::string> keys;
    keys.reserve(zones.size());
    for (const auto& kv : zones) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());

    std::cout << "  Results - (" << keys.size() << "/" << keys.size() << "):\n";

    std::size_t index = 0;
    for (const auto& borough : keys) {
        const ZoneStatsOoA& z = zones.at(borough);

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
                  << " top_complaint=\"" << topComplaint << "\""
                  << " (" << topCount << ")"
                  << "\n";
    }
}