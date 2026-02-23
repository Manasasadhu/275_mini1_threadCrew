#include "NYC311Analyzer.h"
#include "DateTime.h"

#include <chrono>
#include <iomanip>
#include <iostream>

// macOS-specific: reads the process's physical RAM usage (Resident Set Size).
// On Linux you would parse /proc/self/status instead.
#include <mach/mach.h>

// =============================================================================
// rssMemMB()
//
// Returns how many megabytes of physical RAM this process is currently using.
// Called before and after loadData() so we can measure the memory cost of
// storing 11 million records in the vector.
// =============================================================================
static double rssMemMB() {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0.0;
    // resident_size is in bytes — divide by 1024² to get megabytes
    return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
}

// =============================================================================
// main()
// =============================================================================
int main() {
    std::cout << "=== NYC 311 Analyzer ===" << std::endl;

    NYC311Analyzer analyzer;

    // -------------------------------------------------------------------------
    // Load data
    // Snapshot RSS before and after so we can compute the memory delta —
    // i.e., how many MB the full dataset actually occupies in RAM.
    // -------------------------------------------------------------------------
    double memBefore = rssMemMB();
    analyzer.loadData("311_2010_2019_full.csv.");   // trailing dot is real
    double memAfter  = rssMemMB();

    std::cout << "RSS before load : " << std::fixed << std::setprecision(1)
              << memBefore << " MB" << std::endl;
    std::cout << "RSS after load  : " << std::fixed << std::setprecision(1)
              << memAfter  << " MB" << std::endl;
    std::cout << "Delta RSS       : " << std::fixed << std::setprecision(1)
              << (memAfter - memBefore) << " MB" << std::endl;

    // -------------------------------------------------------------------------
    // Print statistics — borough breakdown, top complaints, date range
    // -------------------------------------------------------------------------
    analyzer.printDataStatistics();

    std::cout << "\n=== SAMPLE QUERIES ===" << std::endl;

    // -------------------------------------------------------------------------
    // Query 1: Date range — all records created in calendar year 2015
    // DateTime::parse() converts the "MM/DD/YYYY HH:MM:SS AM" format from
    // the CSV into our compact DateTime struct for comparison.
    // -------------------------------------------------------------------------
    std::cout << "\n1. Getting records created in 2015:" << std::endl;
    DateTime y2015start = DateTime::parse("01/01/2015 12:00:00 AM");
    DateTime y2015end   = DateTime::parse("12/31/2015 11:59:59 PM");
    auto year2015 = analyzer.filterByDateRange(y2015start, y2015end);

    if (!year2015.empty()) {
        std::cout << "Sample records:" << std::endl;
        // Print up to 5 sample records so we can verify the results look right
        for (int i = 0; i < std::min(5, (int)year2015.size()); i++) {
            const auto& r = year2015[i];
            std::cout << "  [" << r.createdDate.toString() << "]  "
                      << r.borough << "  —  " << r.complaintType << std::endl;
        }
    }

    // -------------------------------------------------------------------------
    // Query 2: Borough — returns all records filed in Brooklyn
    // Uses case-insensitive equality so "BROOKLYN" matches "Brooklyn" etc.
    // -------------------------------------------------------------------------
    std::cout << "\n2. Getting records for Brooklyn:" << std::endl;
    auto brooklyn = analyzer.filterByBorough("BROOKLYN");

    if (!brooklyn.empty()) {
        std::cout << "Sample records:" << std::endl;
        for (int i = 0; i < std::min(5, (int)brooklyn.size()); i++) {
            const auto& r = brooklyn[i];
            std::cout << "  " << r.incidentAddress
                      << "  —  " << r.complaintType
                      << "  [" << r.status << "]" << std::endl;
        }
    }

    // -------------------------------------------------------------------------
    // Query 3: Agency — all complaints handled by NYPD
    // Agency codes are short (3-5 chars), stored as std::string in the struct.
    // -------------------------------------------------------------------------
    std::cout << "\n3. Getting records for agency NYPD:" << std::endl;
    auto nypd = analyzer.filterByAgency("NYPD");

    if (!nypd.empty()) {
        std::cout << "Sample records:" << std::endl;
        for (int i = 0; i < std::min(5, (int)nypd.size()); i++) {
            const auto& r = nypd[i];
            std::cout << "  " << r.complaintType
                      << "  —  " << r.borough
                      << "  [" << r.createdDate.toString() << "]" << std::endl;
        }
    }

    // -------------------------------------------------------------------------
    // Query 4: Complaint type substring — anything containing "Noise"
    // Substring match is slower than exact match (checks every position in
    // each string) — compare this time to the borough/agency queries above.
    // -------------------------------------------------------------------------
    std::cout << "\n4. Getting records with complaint type containing 'Noise':" << std::endl;
    auto noise = analyzer.filterByComplaintType("Noise");

    if (!noise.empty()) {
        std::cout << "Sample records:" << std::endl;
        for (int i = 0; i < std::min(5, (int)noise.size()); i++) {
            const auto& r = noise[i];
            std::cout << "  " << r.complaintType
                      << "  —  " << r.descriptor
                      << "  [" << r.borough << "]" << std::endl;
        }
    }

    // -------------------------------------------------------------------------
    // Query 5: Status — all records still marked Open
    // Expect far fewer matches than "Closed" since this is a 2010-2019 archive.
    // -------------------------------------------------------------------------
    std::cout << "\n5. Getting open records:" << std::endl;
    auto open = analyzer.filterByStatus("Open");

    if (!open.empty()) {
        std::cout << "Sample records:" << std::endl;
        for (int i = 0; i < std::min(5, (int)open.size()); i++) {
            const auto& r = open[i];
            std::cout << "  " << r.complaintType
                      << "  —  " << r.borough
                      << "  created: " << r.createdDate.toString() << std::endl;
        }
    }

    // -------------------------------------------------------------------------
    // Query 6: Zip code — incidentZip is stored as uint32_t, so this is a
    // plain integer compare (no string overhead at all).
    // -------------------------------------------------------------------------
    std::cout << "\n6. Getting records for zip 10001 (Midtown West):" << std::endl;
    auto zip10001 = analyzer.filterByZip(10001);

    if (!zip10001.empty()) {
        std::cout << "Sample records:" << std::endl;
        for (int i = 0; i < std::min(5, (int)zip10001.size()); i++) {
            const auto& r = zip10001[i];
            std::cout << "  " << r.incidentAddress
                      << "  —  " << r.complaintType << std::endl;
        }
    }

    // -------------------------------------------------------------------------
    // Query 7: Lat/lon bounding box — four double comparisons per record.
    // Box covers roughly downtown Manhattan (Brooklyn Bridge to Chambers St).
    // -------------------------------------------------------------------------
    std::cout << "\n7. Getting records in downtown Manhattan (lat/lon box):" << std::endl;
    auto downtown = analyzer.filterByLatLonBox(
        40.70, 40.75,    // latitude  south → north
       -74.02, -73.98);  // longitude west  → east

    if (!downtown.empty()) {
        std::cout << "Sample records:" << std::endl;
        for (int i = 0; i < std::min(5, (int)downtown.size()); i++) {
            const auto& r = downtown[i];
            std::cout << "  (" << r.latitude << ", " << r.longitude << ")"
                      << "  —  " << r.complaintType << std::endl;
        }
    }

    // -------------------------------------------------------------------------
    // Performance test — same pattern as FireDataAnalyzer:
    // run one query 10 times, sum the wall time, print average.
    // Borough query chosen because it returns a large result set (3M+ records)
    // giving a stable, repeatable measurement.
    // -------------------------------------------------------------------------
    std::cout << "\n=== PERFORMANCE TESTING ===" << std::endl;

    auto perfStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; i++) {
        analyzer.filterByBorough("BROOKLYN");
    }
    auto perfEnd      = std::chrono::high_resolution_clock::now();
    auto perfDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                            perfEnd - perfStart);

    std::cout << "10 borough queries took " << perfDuration.count()
              << " microseconds (avg: " << perfDuration.count() / 10
              << " μs per query)" << std::endl;

    return 0;
}
