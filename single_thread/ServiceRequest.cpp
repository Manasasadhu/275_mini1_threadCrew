#include "ServiceRequest.h"
#include <cstdlib>  
#include <cstring>
#include <stdexcept>
#include <cstdio>


// Internal helper: convert 12-h hour + AM/PM to 24-h
static uint8_t to24h(uint8_t h, bool isPM) {
    if (!isPM)  return (h == 12) ? 0  : h;      
    else        return (h == 12) ? 12 : h + 12; 
}


// DateTime::parse - Accepts strings in the format:  "MM/DD/YYYY HH:MM:SS AM", Returns an invalid DateTime for empty or malformed input.
DateTime DateTime::parse(const char* s, std::size_t len) {
    DateTime dt;
    if (!s || len < 11) return dt;  

    unsigned mm = 0, dd = 0, yyyy = 0;
    unsigned hh = 0, mi = 0, ss = 0;
    char     ampm[3] = {};

    // sscanf is fast enough for our purposes here
    int n = std::sscanf(s, "%u/%u/%u %u:%u:%u %2s",
                        &mm, &dd, &yyyy, &hh, &mi, &ss, ampm);
    if (n < 7) return dt;  

    dt.month  = static_cast<uint8_t>(mm);
    dt.day    = static_cast<uint8_t>(dd);
    dt.year   = static_cast<uint16_t>(yyyy);
    dt.minute = static_cast<uint8_t>(mi);
    dt.second = static_cast<uint8_t>(ss);
    dt.hour   = to24h(static_cast<uint8_t>(hh),
                      (ampm[0] == 'P' || ampm[0] == 'p'));
    dt.valid  = true;
    return dt;
}

DateTime DateTime::parse(const std::string& s) {
    return parse(s.c_str(), s.size());
}

// Packing: pack fields into a 64-bit key for O(1) comparison
uint64_t DateTime::toKey() const noexcept {
    return (static_cast<uint64_t>(year)   << 40) |
           (static_cast<uint64_t>(month)  << 32) |
           (static_cast<uint64_t>(day)    << 24) |
           (static_cast<uint64_t>(hour)   << 16) |
           (static_cast<uint64_t>(minute) <<  8) |
            static_cast<uint64_t>(second);
}

// Comparison operators - An invalid DateTime sorts before all valid ones and Normal field-by-field comparison (not using packed key format).
bool DateTime::operator==(const DateTime& o) const noexcept {
    if (valid != o.valid) return false;
    return year == o.year && month == o.month && day == o.day &&
           hour == o.hour && minute == o.minute && second == o.second;
}

bool DateTime::operator!=(const DateTime& o) const noexcept { 
    return !(*this == o); 
}

bool DateTime::operator< (const DateTime& o) const noexcept {
    if (!valid && !o.valid) return false;
    if (!valid)  return true;
    if (!o.valid) return false;
    
    // Normal field-by-field comparison
    if (year != o.year) return year < o.year;
    if (month != o.month) return month < o.month;
    if (day != o.day) return day < o.day;
    if (hour != o.hour) return hour < o.hour;
    if (minute != o.minute) return minute < o.minute;
    return second < o.second;
}

bool DateTime::operator<=(const DateTime& o) const noexcept { 
    return !(o < *this); 
}

bool DateTime::operator> (const DateTime& o) const noexcept { 
    return o < *this; 
}

bool DateTime::operator>=(const DateTime& o) const noexcept { 
    return !(*this < o); 
}

// toString: ISO-like "YYYY-MM-DD HH:MM:SS"
std::string DateTime::toString() const {
    if (!valid) return "(invalid)";
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
                  year, month, day, hour, minute, second);
    return buf;
}

// Helper: parse a uint32_t zip code; returns 0 on empty or non-numeric
static uint32_t parseZip(const std::string& s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    unsigned long v = std::strtoul(s.c_str(), &end, 10);
    if (end == s.c_str()) return 0; 
    return static_cast<uint32_t>(v);
}

// Helper: parse a small signed integer (council district); -1 on empty
static int16_t parseInt16(const std::string& s) {
    if (s.empty()) return -1;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return -1;
    return static_cast<int16_t>(v);
}

// Helper: parse a 64-bit unsigned integer (BBL, uniqueKey)
static uint64_t parseU64(const std::string& s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if (end == s.c_str()) return 0;
    return static_cast<uint64_t>(v);
}

// Helper: parse a 32-bit signed integer (state-plane coordinates)
static int32_t parseInt32(const std::string& s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return 0;
    return static_cast<int32_t>(v);
}

// Helper: parse a double (lat/lon)
static double parseDouble(const std::string& s) {
    if (s.empty()) return 0.0;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return 0.0;
    return v;
}

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

    return true;
}