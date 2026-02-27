#pragma once
#include "DateTime.h"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// ServiceRequest
//   Models one row of the NYC 311 dataset (2010-2019).
//   Fields are typed as the most compact primitive that fits the data:
//     - Integer IDs / coordinates  → fixed-width ints
//     - Lat/Lon                    → double
//     - Dates                      → DateTime (8 bytes each)
//     - Categorical / free text    → std::string
//   The Location (WKT POINT) column is intentionally skipped; it is
//   redundant given Latitude and Longitude.
// ---------------------------------------------------------------------------
struct ServiceRequest {
    // ---- Identifiers --------------------------------------------------------
    uint64_t    uniqueKey        = 0;    // col  0

    // ---- Dates (4 × 8 bytes) ------------------------------------------------
    DateTime    createdDate;             // col  1
    DateTime    closedDate;             // col  2
    DateTime    dueDate;                // col 21
    DateTime    resolutionUpdatedDate;  // col 23

    // ---- Agency -------------------------------------------------------------
    std::string agency;                 // col  3  e.g. "DOHMH"
    std::string agencyName;             // col  4  full name

    // ---- Complaint ----------------------------------------------------------
    std::string complaintType;          // col  5  "Problem" field
    std::string descriptor;             // col  6  "Problem Detail"
    std::string additionalDetails;      // col  7

    // ---- Location text ------------------------------------------------------
    std::string locationType;           // col  8
    uint32_t    incidentZip     = 0;   // col  9  0 if empty/invalid
    std::string incidentAddress;        // col 10
    std::string streetName;             // col 11
    std::string crossStreet1;           // col 12
    std::string crossStreet2;           // col 13
    std::string intersectionStreet1;    // col 14
    std::string intersectionStreet2;    // col 15
    std::string addressType;            // col 16
    std::string city;                   // col 17
    std::string landmark;               // col 18
    std::string facilityType;           // col 19

    // ---- Status / resolution ------------------------------------------------
    std::string status;                 // col 20
    std::string resolutionDescription;  // col 22

    // ---- Administrative -----------------------------------------------------
    std::string communityBoard;         // col 24
    int16_t     councilDistrict = -1;  // col 25  -1 if empty
    std::string policePrecinct;         // col 26
    uint64_t    bbl             = 0;   // col 27  10-digit Borough-Block-Lot
    std::string borough;                // col 28

    // ---- Coordinates --------------------------------------------------------
    int32_t     xCoordinate     = 0;   // col 29  State Plane (ft), 0=absent
    int32_t     yCoordinate     = 0;   // col 30
    double      latitude        = 0.0; // col 41
    double      longitude       = 0.0; // col 42

    // ---- Channel ------------------------------------------------------------
    std::string channelType;            // col 31  e.g. "PHONE", "ONLINE"

    // ---- Park / vehicle / bridge fields (often empty) -----------------------
    std::string parkFacilityName;       // col 32
    std::string parkBorough;            // col 33
    std::string vehicleType;            // col 34
    std::string taxiCompanyBorough;     // col 35
    std::string taxiPickupLocation;     // col 36
    std::string bridgeHighwayName;      // col 37
    std::string bridgeHighwayDirection; // col 38
    std::string roadRamp;               // col 39
    std::string bridgeHighwaySegment;   // col 40

    // ------------------------------------------------------------------
    // Construct from an already-split vector of field strings.
    // Returns false if the record is malformed (wrong field count, etc.)
    // ------------------------------------------------------------------
    bool fromFields(const std::vector<std::string>& fields);
};
