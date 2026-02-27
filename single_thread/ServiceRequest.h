#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// DateTime
//   Represents a date/time value parsed from NYC 311 CSV timestamps.
//   Format in the file: "MM/DD/YYYY HH:MM:SS AM" or empty string.
//
//   Fields are stored as the smallest suitable primitive types to minimise
//   memory footprint across millions of records.
//
//   Comparison operators allow direct use in range predicates.
// ---------------------------------------------------------------------------
struct DateTime {
    uint16_t year   = 0;   // e.g. 2013
    uint8_t  month  = 0;   // 1-12
    uint8_t  day    = 0;   // 1-31
    uint8_t  hour   = 0;   // 0-23 (24-h, converted from AM/PM)
    uint8_t  minute = 0;   // 0-59
    uint8_t  second = 0;   // 0-59
    bool     valid  = false;  // false when field is empty / unparseable

    // ------------------------------------------------------------------
    // Factory: parse from a raw string field.
    // Returns an invalid DateTime if the string is empty or malformed.
    // ------------------------------------------------------------------
    static DateTime parse(const std::string& s);
    static DateTime parse(const char* s, std::size_t len);

    // ------------------------------------------------------------------
    // Pack all fields into a single uint64 for fast ordering.
    // Bit layout (MSB→LSB): year(16) month(8) day(8) hour(8) min(8) sec(8)
    // ------------------------------------------------------------------
    uint64_t toKey() const noexcept;

    // Comparison operators  (invalid < any valid)
    bool operator==(const DateTime& o) const noexcept;
    bool operator!=(const DateTime& o) const noexcept;
    bool operator< (const DateTime& o) const noexcept;
    bool operator<=(const DateTime& o) const noexcept;
    bool operator> (const DateTime& o) const noexcept;
    bool operator>=(const DateTime& o) const noexcept;

    // Human-readable output  e.g. "2013-09-29 12:00:00"
    std::string toString() const;
};

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
