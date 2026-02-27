#pragma once
#include "ServiceRequest.h"
#include <cstddef>
#include <functional>
#include <string>

// ---------------------------------------------------------------------------
// IDataReader  —  abstract interface for any record data source.
//
//   Using a pure-virtual base class lets DataStore remain decoupled from
//   the concrete file format.  In future phases you could add:
//     - BinaryReader  (packed binary format, faster I/O)
//     - ParallelCsvReader  (splits file into chunks for Phase 2)
//   without changing the DataStore or any query code.
//
//   Pattern: Template Method + Strategy (reader is injected into DataStore).
// ---------------------------------------------------------------------------
class IDataReader {
public:
    virtual ~IDataReader() = default;

    // ------------------------------------------------------------------
    // open: prepare the data source (open file, validate header, etc.)
    // Returns true on success.
    // ------------------------------------------------------------------
    virtual bool open(const std::string& path) = 0;

    // ------------------------------------------------------------------
    // readAll: load every record into the provided vector.
    // Returns the number of records successfully read.
    // ------------------------------------------------------------------
    virtual std::size_t readAll(std::vector<ServiceRequest>& out) = 0;

    // ------------------------------------------------------------------
    // readChunk: streaming variant — calls cb for every parsed record.
    // Useful for filtering without building a full in-memory copy.
    // Returns total records seen.
    // ------------------------------------------------------------------
    virtual std::size_t readChunk(
        std::function<void(ServiceRequest&&)> cb) = 0;

    // ------------------------------------------------------------------
    // close: release any file handles / mapped memory.
    // ------------------------------------------------------------------
    virtual void close() = 0;

    // ------------------------------------------------------------------
    // Accessors filled after open()
    // ------------------------------------------------------------------
    virtual std::size_t skippedRows()  const = 0;  // malformed rows
    virtual std::size_t totalRows()    const = 0;  // rows seen (excl. header)
};
