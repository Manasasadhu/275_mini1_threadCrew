#include "ServiceRequestOoA.h"
#include "queries.h"
#include <iostream>
#include <chrono>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <array>
#include <omp.h>


// ------------------------- helpers for date key -------------------------
static uint8_t to24h(uint8_t h, bool isPM) {
   if (!isPM) return (h == 12) ? 0 : h;
   return (h == 12) ? 12 : (uint8_t)(h + 12);
}


// Parse "MM/DD/YYYY HH:MM:SS AM/PM" -> packed key
static uint64_t parseDateKey(const std::string& s) {
   if (s.empty()) return 0;


   unsigned mm=0, dd=0, yyyy=0, hh=0, mi=0, ss=0;
   char ampm[3] = {};
   int n = std::sscanf(s.c_str(), "%u/%u/%u %u:%u:%u %2s",
                       &mm, &dd, &yyyy, &hh, &mi, &ss, ampm);
   if (n < 7) return 0;


   uint8_t hour24 = to24h((uint8_t)hh, (ampm[0]=='P' || ampm[0]=='p'));


   return ((uint64_t)yyyy << 40) |
          ((uint64_t)mm   << 32) |
          ((uint64_t)dd   << 24) |
          ((uint64_t)hour24 << 16) |
          ((uint64_t)mi   << 8) |
          (uint64_t)ss;
}


// ------------------------- ZoneStats (used by borough aggregation) -------------------------
struct ZoneStatsOoA {
   std::size_t totalCount = 0;
   std::unordered_map<std::string, std::size_t> byComplaintType;
};


// ------------------------- Query 3: average latitude (OpenMP) [optimized] -------------------------
static double averageLatitudeOoA_omp(const ServiceRequestOoA& data) {
   std::size_t n = data.latitude.size();
   if (n == 0) return 0.0;


   double sum = 0.0;


   // num_threads removed (threads set once in main with omp_set_num_threads)
   #pragma omp parallel for reduction(+:sum) schedule(static)
   for (std::size_t i = 0; i < n; ++i) {
       sum += data.latitude[i];
   }


   return sum / (double)n;
}


// ------------------------- Query 4: aggregateByBorough (OpenMP fast) [optimized] -------------------------
// Uses fixed 6 buckets to avoid hashing borough string inside the hot loop.
// Uses data.boroughUpper (precomputed in loader) to avoid per-record uppercasing.
static std::unordered_map<std::string, ZoneStatsOoA>
aggregateByBoroughOoA_omp_fast(const ServiceRequestOoA& data, int threads) {
   std::size_t n = data.boroughUpper.size();
   std::unordered_map<std::string, ZoneStatsOoA> result;
   if (n == 0) return result;


   int T = threads > 0 ? threads : omp_get_max_threads();


   auto boroughIndex = [&](const std::string& b) -> int {
       if (b == "BRONX") return 0;
       if (b == "BROOKLYN") return 1;
       if (b == "MANHATTAN") return 2;
       if (b == "QUEENS") return 3;
       if (b == "STATEN ISLAND") return 4;
       return 5; // unknown / empty / other
   };


   // thread-local: 6 borough buckets
   std::vector<std::array<ZoneStatsOoA, 6>> local(T);


   // reduce rehashing in complaint maps (tune if needed)
   for (int t = 0; t < T; ++t) {
       for (int b = 0; b < 6; ++b) {
           local[t][b].byComplaintType.reserve(256);
       }
   }


   #pragma omp parallel num_threads(T)
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


   // merge thread-local into 6 merged buckets
   std::array<ZoneStatsOoA, 6> merged;
   for (int b = 0; b < 6; ++b) merged[b].byComplaintType.reserve(512);


   for (int t = 0; t < T; ++t) {
       for (int b = 0; b < 6; ++b) {
           merged[b].totalCount += local[t][b].totalCount;
           for (auto& kv : local[t][b].byComplaintType) {
               merged[b].byComplaintType[kv.first] += kv.second;
           }
       }
   }


   // Build result map for existing printTopComplaintPerBorough()
   result.reserve(8);
   result["BRONX"] = std::move(merged[0]);
   result["BROOKLYN"] = std::move(merged[1]);
   result["MANHATTAN"] = std::move(merged[2]);
   result["QUEENS"] = std::move(merged[3]);
   result["STATEN ISLAND"] = std::move(merged[4]);
   result["(unknown)"] = std::move(merged[5]);


   return result;
}


static void printTopComplaintPerBorough(
   const std::unordered_map<std::string, ZoneStatsOoA>& zones) {


   std::vector<std::string> keys;
   keys.reserve(zones.size());
   for (const auto& kv : zones) keys.push_back(kv.first);
   std::sort(keys.begin(), keys.end());


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


       std::cout << borough
                 << " total=" << z.totalCount
                 << " top_complaint=\"" << topComplaint << "\""
                 << " (" << topCount << ")\n";
   }
}


// ------------------------- Query 5: filterByCreatedDateRange (OpenMP) -------------------------
static std::vector<std::size_t>
filterByCreatedDateRangeOoA_omp(const ServiceRequestOoA& data,
                               uint64_t startKey,
                               uint64_t endKey,
                               int threads) {
   std::size_t n = data.createdKey.size();
   std::vector<std::size_t> out;
   if (n == 0) return out;


   std::vector<char> keep(n, 0);


   #pragma omp parallel for num_threads(threads) schedule(static)
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


// ------------------------- Query 6: filterByBorough (OpenMP) -------------------------
static std::vector<std::size_t>
filterByBoroughOoA_omp(const ServiceRequestOoA& data,
                      const std::string& boroughUpper,
                      int threads) {
   std::size_t n = data.boroughUpper.size();
   std::vector<std::size_t> out;
   if (n == 0) return out;


   std::vector<char> keep(n, 0);


   #pragma omp parallel for num_threads(threads) schedule(static)
   for (std::size_t i = 0; i < n; ++i) {
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


// ------------------------- main -------------------------
int main(int argc, char* argv[]) {
   std::string filename = (argc > 1)
       ? argv[1]
       : "/Users/Asha/Desktop/Asha workspace/275-mini1/dataset/311_combined.csv";


   int numberOfThreads = 6;
   if (argc > 2) numberOfThreads = std::atoi(argv[2]);
   if (numberOfThreads <= 0) numberOfThreads = 1;


   ServiceRequestOoA data;
   if (!loadServiceRequestOoA(filename, data)) {
       std::cerr << "Failed to load data.\n";
       return 1;
   }


   std::cout << "Loaded " << data.uniqueKey.size() << " records into OoA structure.\n";
   if (!data.complaintType.empty()) {
       std::cout << "First complaintType: " << data.complaintType[0] << "\n";
       std::cout << "First latitude: " << data.latitude[0]
                 << ", longitude: " << data.longitude[0] << "\n";
   }
   std::cout << "Using threads: " << numberOfThreads << "\n";


   // set OpenMP threads once (reduces overhead for repeated calls)
   omp_set_num_threads(numberOfThreads);


   const int runs = 15;
   const int runsAgg = 5; // aggregation is heavy; fewer runs gives cleaner results


   // ---- timing helpers (warm-up + measurement) ----
   auto measureVectorQuery = [&](const std::string& label, int runsX, auto query) {
       using namespace std::chrono;
       std::size_t count = 0;


       auto warm = query();
       count = warm.size();


       auto t0 = high_resolution_clock::now();
       for (int i = 0; i < runsX; ++i) {
           auto res = query();
           if (i == 0) count = res.size();
       }
       auto t1 = high_resolution_clock::now();
       double total = duration<double>(t1 - t0).count();


       std::cout << label << " -> size=" << count
                 << ", total=" << total << "s"
                 << ", avg=" << (total / runsX) << "s\n";
   };


   auto measureScalarQuery = [&](const std::string& label, int runsX, auto query) {
       using namespace std::chrono;
       double val = 0.0;


       val = query(); // warm-up


       auto t0 = high_resolution_clock::now();
       for (int i = 0; i < runsX; ++i) val = query();
       auto t1 = high_resolution_clock::now();
       double total = duration<double>(t1 - t0).count();


       std::cout << label << " -> value=" << val
                 << ", total=" << total << "s"
                 << ", avg=" << (total / runsX) << "s\n";
   };


   auto measureMapQuery = [&](const std::string& label, int runsX, auto query) {
       using namespace std::chrono;
       std::size_t zoneCount = 0;


       auto warm = query();
       zoneCount = warm.size();


       auto t0 = high_resolution_clock::now();
       for (int i = 0; i < runsX; ++i) {
           auto res = query();
           if (i == 0) zoneCount = res.size();
       }
       auto t1 = high_resolution_clock::now();
       double total = duration<double>(t1 - t0).count();


       std::cout << label << " -> zones=" << zoneCount
                 << ", total=" << total << "s"
                 << ", avg=" << (total / runsX) << "s\n";
   };


   // ------------------------- Query 1 -------------------------
   measureVectorQuery("complaint 'rodent' (OoA, omp)", runs,
       [&]() { return searchByComplaintOoA(data, "rodent", numberOfThreads); });


   // ------------------------- Query 2 -------------------------
   measureVectorQuery("lat/lon box (OoA, omp)", runs,
       [&]() { return filterByLatLonBoxOoA(data, 40.5, 40.9, -74.25, -73.7, numberOfThreads); });


   // ------------------------- Query 3 [optimized] -------------------------
   measureScalarQuery("average latitude (OoA, omp)", runs,
       [&]() { return averageLatitudeOoA_omp(data); });


   // ------------------------- Query 4 [optimized] -------------------------
   measureMapQuery("borough aggregation (OoA, omp fast)", runsAgg,
       [&]() { return aggregateByBoroughOoA_omp_fast(data, numberOfThreads); });


   // ------------------------- Query 5 -------------------------
   uint64_t startKey = parseDateKey("01/01/2013 12:00:00 AM");
   uint64_t endKey   = parseDateKey("12/31/2013 11:59:59 PM");


   measureVectorQuery("createdDate range 2013 (OoA, omp)", runs,
       [&]() { return filterByCreatedDateRangeOoA_omp(data, startKey, endKey, numberOfThreads); });


   // ------------------------- Query 6 -------------------------
   measureVectorQuery("borough BROOKLYN (OoA, omp)", runs,
       [&]() { return filterByBoroughOoA_omp(data, "BROOKLYN", numberOfThreads); });


   // Print borough totals + top complaint once
   std::cout << "\n=== Borough Totals + Top Complaint (OoA, omp fast) ===\n";
   auto zones = aggregateByBoroughOoA_omp_fast(data, numberOfThreads);
   printTopComplaintPerBorough(zones);


   return 0;
}

