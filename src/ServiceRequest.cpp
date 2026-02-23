#include "ServiceRequest.h"
#include <cstdlib>   // strtoul / strtod
#include <cstring>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helper: parse a uint32_t zip code; returns 0 on empty or non-numeric
// ---------------------------------------------------------------------------
static uint32_t parseZip(const std::string& s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    unsigned long v = std::strtoul(s.c_str(), &end, 10);
    if (end == s.c_str()) return 0;  // no digits consumed
    return static_cast<uint32_t>(v);
}

// ---------------------------------------------------------------------------
// Helper: parse a small signed integer (council district); -1 on empty
// ---------------------------------------------------------------------------
static int16_t parseInt16(const std::string& s) {
    if (s.empty()) return -1;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return -1;
    return static_cast<int16_t>(v);
}

// ---------------------------------------------------------------------------
// Helper: parse a 64-bit unsigned integer (BBL, uniqueKey)
// ---------------------------------------------------------------------------
static uint64_t parseU64(const std::string& s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (end == s.c_str()) return 0;
    return static_cast<uint64_t>(v);
}

// ---------------------------------------------------------------------------
// Helper: parse a 32-bit signed integer (state-plane coordinates)
// ---------------------------------------------------------------------------
static int32_t parseInt32(const std::string& s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return 0;
    return static_cast<int32_t>(v);
}

// ---------------------------------------------------------------------------
// Helper: parse a double (lat/lon)
// ---------------------------------------------------------------------------
static double parseDouble(const std::string& s) {
    if (s.empty()) return 0.0;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return 0.0;
    return v;
}

// ---------------------------------------------------------------------------
// ServiceRequest::fromFields
//   Populates all fields from the split CSV row.
//   The expected column order matches the NYC 311 2010-2019 header.
//   Returns false if there are fewer than 43 fields (last "Location" col
//   is optional since we get position from lat/lon).
// ---------------------------------------------------------------------------
bool ServiceRequest::fromFields(const std::vector<std::string>& f) {
    if (f.size() < 43) return false;

    uniqueKey                = parseU64(f[0]);
    createdDate              = DateTime::parse(f[1]);
    closedDate               = DateTime::parse(f[2]);
    agency                   = f[3];
    agencyName               = f[4];
    complaintType            = f[5];
    descriptor               = f[6];
    additionalDetails        = f[7];
    locationType             = f[8];
    incidentZip              = parseZip(f[9]);
    incidentAddress          = f[10];
    streetName               = f[11];
    crossStreet1             = f[12];
    crossStreet2             = f[13];
    intersectionStreet1      = f[14];
    intersectionStreet2      = f[15];
    addressType              = f[16];
    city                     = f[17];
    landmark                 = f[18];
    facilityType             = f[19];
    status                   = f[20];
    dueDate                  = DateTime::parse(f[21]);
    resolutionDescription    = f[22];
    resolutionUpdatedDate    = DateTime::parse(f[23]);
    communityBoard           = f[24];
    councilDistrict          = parseInt16(f[25]);
    policePrecinct           = f[26];
    bbl                      = parseU64(f[27]);
    borough                  = f[28];
    xCoordinate              = parseInt32(f[29]);
    yCoordinate              = parseInt32(f[30]);
    channelType              = f[31];
    parkFacilityName         = f[32];
    parkBorough              = f[33];
    vehicleType              = f[34];
    taxiCompanyBorough       = f[35];
    taxiPickupLocation       = f[36];
    bridgeHighwayName        = f[37];
    bridgeHighwayDirection   = f[38];
    roadRamp                 = f[39];
    bridgeHighwaySegment     = f[40];
    latitude                 = parseDouble(f[41]);
    longitude                = parseDouble(f[42]);
    // f[43] is the "Location" WKT string — skipped (redundant)

    return true;
}
