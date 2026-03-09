#pragma once
#include <cstddef>
#include <vector>
#include <string>
#include <cstdint>


// Object-of-Arrays (OoA) structure for all NYC 311 fields
struct ServiceRequestOoA {
   std::vector<uint64_t> uniqueKey;
   std::vector<std::string> createdDate;
   std::vector<std::string> closedDate;
   std::vector<std::string> agency;
   std::vector<std::string> agencyName;
   std::vector<std::string> complaintType;
   std::vector<std::string> complaintTypeLower;
   std::vector<std::string> descriptor;
   std::vector<std::string> additionalDetails;
   std::vector<std::string> locationType;
   std::vector<uint32_t> incidentZip;
   std::vector<std::string> incidentAddress;
   std::vector<std::string> streetName;
   std::vector<std::string> crossStreet1;
   std::vector<std::string> crossStreet2;
   std::vector<std::string> intersectionStreet1;
   std::vector<std::string> intersectionStreet2;
   std::vector<std::string> addressType;
   std::vector<std::string> city;
   std::vector<std::string> landmark;
   std::vector<std::string> facilityType;
   std::vector<std::string> status;
   std::vector<std::string> dueDate;
   std::vector<std::string> resolutionDescription;
   std::vector<std::string> resolutionUpdatedDate;
   std::vector<std::string> communityBoard;
   std::vector<int16_t> councilDistrict;
   std::vector<std::string> policePrecinct;
   std::vector<uint64_t> bbl;
   std::vector<std::string> borough;
   std::vector<int32_t> xCoordinate;
   std::vector<int32_t> yCoordinate;
   std::vector<std::string> channelType;
   std::vector<std::string> parkFacilityName;
   std::vector<std::string> parkBorough;
   std::vector<std::string> vehicleType;
   std::vector<std::string> taxiCompanyBorough;
   std::vector<std::string> taxiPickupLocation;
   std::vector<std::string> bridgeHighwayName;
   std::vector<std::string> bridgeHighwayDirection;
   std::vector<std::string> roadRamp;
   std::vector<std::string> bridgeHighwaySegment;
   std::vector<double> latitude;
   std::vector<double> longitude;
   std::vector<uint64_t> createdKey;
   std::vector<std::string> boroughUpper;
};

// Loader function declaration (must come after struct definition)
bool loadServiceRequestOoA(const std::string& filename, ServiceRequestOoA& data, size_t maxRecords = 14000000);
