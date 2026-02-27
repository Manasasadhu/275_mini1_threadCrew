#pragma once
#include <cstdint>
#include <string>

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
