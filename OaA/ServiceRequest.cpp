#include "ServiceRequest.h"
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <cstdio>
#include <cstdlib>


// Helper functions for parsing fields (minimal, can be expanded as needed)
static uint32_t parseZip(const std::string& s) {
   if (s.empty()) return 0;
   char* end = nullptr;
   unsigned long v = std::strtoul(s.c_str(), &end, 10);
   if (end == s.c_str()) return 0;
   return static_cast<uint32_t>(v);
}
static int16_t parseInt16(const std::string& s) {
   if (s.empty()) return -1;
   char* end = nullptr;
   long v = std::strtol(s.c_str(), &end, 10);
   if (end == s.c_str()) return -1;
   return static_cast<int16_t>(v);
}
static uint64_t parseU64(const std::string& s) {
   if (s.empty()) return 0;
   char* end = nullptr;
   unsigned long long v = std::strtoull(s.c_str(), &end, 10);
   if (end == s.c_str()) return 0;
   return static_cast<uint64_t>(v);
}
static int32_t parseInt32(const std::string& s) {
   if (s.empty()) return 0;
   char* end = nullptr;
   long v = std::strtol(s.c_str(), &end, 10);
   if (end == s.c_str()) return 0;
   return static_cast<int32_t>(v);
}
static double parseDouble(const std::string& s) {
   if (s.empty()) return 0.0;
   char* end = nullptr;
   double v = std::strtod(s.c_str(), &end);
   if (end == s.c_str()) return 0.0;
   return v;
}


// --- NEW helpers for fast queries ---
static uint8_t to24h(uint8_t h, bool isPM) {
   if (!isPM) return (h == 12) ? 0 : h;
   return (h == 12) ? 12 : (uint8_t)(h + 12);
}


static uint64_t parseDateKey(const std::string& s) {
   if (s.empty()) return 0;


   unsigned mm=0, dd=0, yyyy=0, hh=0, mi=0, ss=0;
   char ampm[3] = {};
   int n = std::sscanf(s.c_str(), "%u/%u/%u %u:%u:%u %2s",
                       &mm, &dd, &yyyy, &hh, &mi, &ss, ampm);
   if (n < 7) return 0;


   uint8_t hour24 = to24h((uint8_t)hh, (ampm[0] == 'P' || ampm[0] == 'p'));


   return ((uint64_t)yyyy << 40) |
          ((uint64_t)mm   << 32) |
          ((uint64_t)dd   << 24) |
          ((uint64_t)hour24 << 16) |
          ((uint64_t)mi   <<  8) |
          (uint64_t)ss;
}


static std::string upperCopy(const std::string& s) {
   std::string t = s;
   std::transform(t.begin(), t.end(), t.begin(),
                  [](unsigned char c) { return (unsigned char)std::toupper(c); });
   return t;
}


// Minimal CSV parser (reuse or expand as needed)
std::vector<std::string> parseCSVLine(const std::string& line) {
   std::vector<std::string> fields;
   std::string current;
   bool inQuotes = false;
   for (std::size_t i = 0; i < line.size(); ++i) {
       char c = line[i];
       if (inQuotes) {
           if (c == '"') {
               if (i + 1 < line.size() && line[i + 1] == '"') {
                   current += '"';
                   ++i;
               } else {
                   inQuotes = false;
               }
           } else {
               current += c;
           }
       } else {
           if (c == '"') {
               inQuotes = true;
           } else if (c == ',') {
               fields.push_back(current);
               current.clear();
           } else {
               current += c;
           }
       }
   }
   fields.push_back(current);
   return fields;
}


// Loader for OoA structure
bool loadServiceRequestOoA(const std::string& filename, ServiceRequestOoA& data, size_t maxRecords) {
   std::ifstream file(filename);
   if (!file.is_open()) {
       std::cerr << "Error opening file: " << filename << std::endl;
       return false;
   }


   std::string line;
   // Skip header
   if (std::getline(file, line)) { }


   while (std::getline(file, line)) {
       auto f = parseCSVLine(line);
       if (f.size() < 43) continue;


       data.uniqueKey.push_back(parseU64(f[0]));


       data.createdDate.push_back(f[1]);
       data.createdKey.push_back(parseDateKey(f[1]));          // NEW


       data.closedDate.push_back(f[2]);
       data.agency.push_back(f[3]);
       data.agencyName.push_back(f[4]);


       data.complaintType.push_back(f[5]);
       std::string lower = f[5];
       std::transform(lower.begin(), lower.end(), lower.begin(),
                      [](unsigned char c) { return (unsigned char)std::tolower(c); });
       data.complaintTypeLower.push_back(lower);


       data.descriptor.push_back(f[6]);
       data.additionalDetails.push_back(f[7]);
       data.locationType.push_back(f[8]);
       data.incidentZip.push_back(parseZip(f[9]));
       data.incidentAddress.push_back(f[10]);
       data.streetName.push_back(f[11]);
       data.crossStreet1.push_back(f[12]);
       data.crossStreet2.push_back(f[13]);
       data.intersectionStreet1.push_back(f[14]);
       data.intersectionStreet2.push_back(f[15]);
       data.addressType.push_back(f[16]);
       data.city.push_back(f[17]);
       data.landmark.push_back(f[18]);
       data.facilityType.push_back(f[19]);
       data.status.push_back(f[20]);
       data.dueDate.push_back(f[21]);
       data.resolutionDescription.push_back(f[22]);
       data.resolutionUpdatedDate.push_back(f[23]);
       data.communityBoard.push_back(f[24]);
       data.councilDistrict.push_back(parseInt16(f[25]));
       data.policePrecinct.push_back(f[26]);
       data.bbl.push_back(parseU64(f[27]));


       data.borough.push_back(f[28]);
       data.boroughUpper.push_back(upperCopy(f[28]));          // NEW


       data.xCoordinate.push_back(parseInt32(f[29]));
       data.yCoordinate.push_back(parseInt32(f[30]));
       data.channelType.push_back(f[31]);
       data.parkFacilityName.push_back(f[32]);
       data.parkBorough.push_back(f[33]);
       data.vehicleType.push_back(f[34]);
       data.taxiCompanyBorough.push_back(f[35]);
       data.taxiPickupLocation.push_back(f[36]);
       data.bridgeHighwayName.push_back(f[37]);
       data.bridgeHighwayDirection.push_back(f[38]);
       data.roadRamp.push_back(f[39]);
       data.bridgeHighwaySegment.push_back(f[40]);
       data.latitude.push_back(parseDouble(f[41]));
       data.longitude.push_back(parseDouble(f[42]));


       if (data.uniqueKey.size() >= maxRecords) break;
   }
   return true;
}