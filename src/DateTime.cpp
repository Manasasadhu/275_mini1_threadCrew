#include "DateTime.h"
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Internal helper: convert 12-h hour + AM/PM to 24-h
// ---------------------------------------------------------------------------
static uint8_t to24h(uint8_t h, bool isPM) {
    if (!isPM)  return (h == 12) ? 0  : h;       // 12 AM → 0,  else same
    else        return (h == 12) ? 12 : h + 12;  // 12 PM → 12, else +12
}

// ---------------------------------------------------------------------------
// DateTime::parse
//   Accepts strings in the format:  "MM/DD/YYYY HH:MM:SS AM"
//   Returns an invalid DateTime for empty or malformed input.
// ---------------------------------------------------------------------------
DateTime DateTime::parse(const char* s, std::size_t len) {
    DateTime dt;
    if (!s || len < 11) return dt;  // too short to be valid

    unsigned mm = 0, dd = 0, yyyy = 0;
    unsigned hh = 0, mi = 0, ss = 0;
    char     ampm[3] = {};

    // sscanf is fast enough for our purposes here
    int n = std::sscanf(s, "%u/%u/%u %u:%u:%u %2s",
                        &mm, &dd, &yyyy, &hh, &mi, &ss, ampm);
    if (n < 7) return dt;  // could not parse all fields

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

// ---------------------------------------------------------------------------
// Packing: pack fields into a 64-bit key for O(1) comparison
// ---------------------------------------------------------------------------
uint64_t DateTime::toKey() const noexcept {
    return (static_cast<uint64_t>(year)   << 40) |
           (static_cast<uint64_t>(month)  << 32) |
           (static_cast<uint64_t>(day)    << 24) |
           (static_cast<uint64_t>(hour)   << 16) |
           (static_cast<uint64_t>(minute) <<  8) |
            static_cast<uint64_t>(second);
}

// ---------------------------------------------------------------------------
// Comparison operators
//   An invalid DateTime sorts before all valid ones.
// ---------------------------------------------------------------------------
bool DateTime::operator==(const DateTime& o) const noexcept {
    if (valid != o.valid) return false;
    return toKey() == o.toKey();
}
bool DateTime::operator!=(const DateTime& o) const noexcept { return !(*this == o); }
bool DateTime::operator< (const DateTime& o) const noexcept {
    if (!valid && !o.valid) return false;
    if (!valid)  return true;
    if (!o.valid) return false;
    return toKey() < o.toKey();
}
bool DateTime::operator<=(const DateTime& o) const noexcept { return !(o < *this); }
bool DateTime::operator> (const DateTime& o) const noexcept { return o < *this;   }
bool DateTime::operator>=(const DateTime& o) const noexcept { return !(*this < o); }

// ---------------------------------------------------------------------------
// toString: ISO-like "YYYY-MM-DD HH:MM:SS"
// ---------------------------------------------------------------------------
std::string DateTime::toString() const {
    if (!valid) return "(invalid)";
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
                  year, month, day, hour, minute, second);
    return buf;
}
