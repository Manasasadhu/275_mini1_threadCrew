#include "ServiceRequestOoA.h"
#include "queries.h"
#include <iostream>
#include <chrono>

int main(int argc, char* argv[]) {
    std::string filename = argc > 1 ? argv[1] : "/Users/aravindreddy/Downloads/SJSU ClassWork/275 EAD/Mini1_Datasets/311_combined.csv.";
    ServiceRequestOoA data;
    if (!loadServiceRequestOoA(filename, data)) {
        std::cerr << "Failed to load data." << std::endl;
        return 1;
    }
    std::cout << "Loaded " << data.uniqueKey.size() << " records into OoA structure." << std::endl;
    // Example: print first record's complaintType and lat/lon
    if (!data.complaintType.empty()) {
        std::cout << "First complaintType: " << data.complaintType[0] << std::endl;
        std::cout << "First latitude: " << data.latitude[0] << ", longitude: " << data.longitude[0] << std::endl;
    }
    int numberOfThreads = 6;
    if (argc > 2) {
        numberOfThreads = std::atoi(argv[2]);
    }
    const int runs = 15;
    // Benchmark helpers
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
    // Complaint "rodent" (OoA)
    measureVectorQuery("complaint 'rodent' (OoA)", runs,
        [&]() { return searchByComplaintOoA(data, "rodent", numberOfThreads); });
    // Lat/lon box (OoA)
    measureVectorQuery("lat/lon box (OoA)", runs,
        [&]() { return filterByLatLonBoxOoA(data, 40.5, 40.9, -74.25, -73.7, numberOfThreads); });
    return 0;
}
