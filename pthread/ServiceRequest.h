#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// DateTime — compact date/time for NYC 311 timestamps
// Format: "MM/DD/YYYY HH:MM:SS AM" or empty string
// ---------------------------------------------------------------------------
struct DateTime {
    uint16_t year   = 0;
    uint8_t  month  = 0;
    uint8_t  day    = 0;
    uint8_t  hour   = 0;   // 0-23 (24-h)
    uint8_t  minute = 0;
    uint8_t  second = 0;
    bool     valid  = false;

    static DateTime parse(const std::string& s);
    static DateTime parse(const char* s, std::size_t len);
    uint64_t toKey() const noexcept;

    bool operator==(const DateTime& o) const noexcept;
    bool operator!=(const DateTime& o) const noexcept;
    bool operator< (const DateTime& o) const noexcept;
    bool operator<=(const DateTime& o) const noexcept;
    bool operator> (const DateTime& o) const noexcept;
    bool operator>=(const DateTime& o) const noexcept;

    std::string toString() const;
};

// ---------------------------------------------------------------------------
// ServiceRequest — one row of the NYC 311 dataset (44 columns)
// ---------------------------------------------------------------------------
struct ServiceRequest {
    uint64_t    uniqueKey        = 0;
    DateTime    createdDate;
    DateTime    closedDate;
    DateTime    dueDate;
    DateTime    resolutionUpdatedDate;
    std::string agency;
    std::string agencyName;
    std::string complaintType;
    std::string descriptor;
    std::string additionalDetails;
    std::string locationType;
    uint32_t    incidentZip     = 0;
    std::string incidentAddress;
    std::string streetName;
    std::string crossStreet1;
    std::string crossStreet2;
    std::string intersectionStreet1;
    std::string intersectionStreet2;
    std::string addressType;
    std::string city;
    std::string landmark;
    std::string facilityType;
    std::string status;
    std::string resolutionDescription;
    std::string communityBoard;
    int16_t     councilDistrict = -1;
    std::string policePrecinct;
    uint64_t    bbl             = 0;
    std::string borough;
    int32_t     xCoordinate     = 0;
    int32_t     yCoordinate     = 0;
    double      latitude        = 0.0;
    double      longitude       = 0.0;
    std::string channelType;
    std::string parkFacilityName;
    std::string parkBorough;
    std::string vehicleType;
    std::string taxiCompanyBorough;
    std::string taxiPickupLocation;
    std::string bridgeHighwayName;
    std::string bridgeHighwayDirection;
    std::string roadRamp;
    std::string bridgeHighwaySegment;

    bool fromFields(const std::vector<std::string>& fields);
};
