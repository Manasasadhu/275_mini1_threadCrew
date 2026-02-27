#pragma once
#include "IDataReader.h"
#include <fstream>
#include <string>
#include <vector>
#include <cstddef>

// ---------------------------------------------------------------------------
// CsvReader  —  concrete IDataReader for RFC-4180 CSV files.
//
//   Design notes:
//   - Uses a 4 MB read buffer (pubsetbuf) to amortise syscall overhead
//     on large files.
//   - Parser is hand-rolled (no third-party libs) and handles:
//       * Quoted fields  (field content may contain commas)
//       * Doubled quotes inside quoted fields  ("" → ")
//       * Trailing carriage returns (\r\n line endings)
//   - Field strings are written directly into a reusable per-row buffer to
//     reduce allocation pressure.
// ---------------------------------------------------------------------------
class CsvReader : public IDataReader {
public:
    CsvReader() = default;
    ~CsvReader() override { close(); }

    bool        open(const std::string& path)                        override;
    std::size_t readAll(std::vector<ServiceRequest>& out)            override;
    std::size_t readChunk(std::function<void(ServiceRequest&&)> cb)  override;
    void        close()                                              override;

    std::size_t skippedRows() const override { return skipped_; }
    std::size_t totalRows()   const override { return total_;   }

private:
    // Parse one CSV line into individual field strings.
    // Returns true and populates 'fields' on success.
    bool parseLine(const std::string& line, std::vector<std::string>& fields);

    std::ifstream       file_;
    std::vector<char>   readBuf_;      // I/O buffer (4 MB)
    std::string         filePath_;
    std::size_t         skipped_ = 0;
    std::size_t         total_   = 0;
};
