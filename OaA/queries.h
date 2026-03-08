#pragma once
#include "ServiceRequestOoA.h"
#include <vector>
#include <string>
#include <algorithm>
#include <omp.h>

// Query 3: Substring search on complaintType (case-insensitive)
inline std::vector<size_t> searchByComplaintOoA(const ServiceRequestOoA& data, const std::string& keyword, int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(data.complaintTypeLower.size());
    std::vector<std::vector<size_t>> localResults(numberOfThreads);
    std::string key = keyword;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        const std::string& comp = data.complaintTypeLower[i];
        if (comp.find(key) != std::string::npos) {
            int t = omp_get_thread_num();
            localResults[t].push_back(i);
        }
    }
    std::vector<size_t> out;
    for (int t = 0; t < numberOfThreads; ++t)
        out.insert(out.end(), localResults[t].begin(), localResults[t].end());
    return out;
}

// Query 4: Bounding box search on latitude/longitude
inline std::vector<size_t> filterByLatLonBoxOoA(const ServiceRequestOoA& data, double minLat, double maxLat, double minLon, double maxLon, int numberOfThreads) {
    omp_set_num_threads(numberOfThreads);
    int n = static_cast<int>(data.latitude.size());
    std::vector<std::vector<size_t>> localResults(numberOfThreads);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; ++i) {
        double lat = data.latitude[i];
        double lon = data.longitude[i];
        if (lat >= minLat && lat <= maxLat && lon >= minLon && lon <= maxLon) {
            int t = omp_get_thread_num();
            localResults[t].push_back(i);
        }
    }
    std::vector<size_t> out;
    for (int t = 0; t < numberOfThreads; ++t)
        out.insert(out.end(), localResults[t].begin(), localResults[t].end());
    return out;
}
