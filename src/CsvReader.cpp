#include "CsvReader.h"
#include <stdexcept>
#include <iostream>

// ---------------------------------------------------------------------------
// Buffer size for file I/O.  4 MB reduces the number of read() syscalls
// significantly on a large file like this 6.7 GB dataset.
// ---------------------------------------------------------------------------
static constexpr std::size_t IO_BUF_SIZE = 4 * 1024 * 1024; // 4 MB

// ---------------------------------------------------------------------------
// CsvReader::open
// ---------------------------------------------------------------------------
bool CsvReader::open(const std::string& path) {
    filePath_ = path;
    skipped_  = 0;
    total_    = 0;

    readBuf_.resize(IO_BUF_SIZE);
    file_.rdbuf()->pubsetbuf(readBuf_.data(), IO_BUF_SIZE);
    file_.open(path, std::ios::in);

    if (!file_.is_open()) {
        std::cerr << "[CsvReader] Cannot open: " << path << "\n";
        return false;
    }

    // Consume and discard the header line
    std::string header;
    std::getline(file_, header);
    return true;
}

// ---------------------------------------------------------------------------
// CsvReader::close
// ---------------------------------------------------------------------------
void CsvReader::close() {
    if (file_.is_open()) file_.close();
}

// ---------------------------------------------------------------------------
// CsvReader::parseLine
//   Hand-rolled RFC-4180 parser.
//   State machine: normal field  |  inside quoted field
//   Handles:
//     ,field,               → plain field
//     ,"text, with comma",  → quoted field
//     ,"text ""quoted""",   → doubled-quote escape
// ---------------------------------------------------------------------------
bool CsvReader::parseLine(const std::string& line,
                          std::vector<std::string>& fields) {
    fields.clear();
    std::string current;
    current.reserve(64);
    bool inQuotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (inQuotes) {
            if (c == '"') {
                // Doubled quote?
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current += '"';
                    ++i;         // skip the second quote
                } else {
                    inQuotes = false;  // closing quote
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
            } else if (c == '\r') {
                // Ignore carriage return (Windows line endings)
            } else {
                current += c;
            }
        }
    }
    // Push the last field (no trailing comma on last field)
    fields.push_back(current);

    return true;
}

// ---------------------------------------------------------------------------
// CsvReader::readAll
//   Reads the entire file into the output vector.
//   Pre-reserves capacity to avoid repeated reallocation.
// ---------------------------------------------------------------------------
std::size_t CsvReader::readAll(std::vector<ServiceRequest>& out) {
    if (!file_.is_open()) return 0;

    // Rough estimate: 11 M records — reserve to avoid realloc mid-load
    out.reserve(12'000'000);

    std::string         line;
    std::vector<std::string> fields;
    fields.reserve(44);
    ServiceRequest      rec;

    while (std::getline(file_, line)) {
        if (line.empty()) continue;
        ++total_;

        parseLine(line, fields);

        if (!rec.fromFields(fields)) {
            ++skipped_;
            continue;
        }
        out.push_back(rec);
    }
    return out.size();
}

// ---------------------------------------------------------------------------
// CsvReader::readChunk
//   Streaming variant: does not store records; calls cb() for each one.
//   Useful for filtered loads or when memory is tight.
// ---------------------------------------------------------------------------
std::size_t CsvReader::readChunk(std::function<void(ServiceRequest&&)> cb) {
    if (!file_.is_open()) return 0;

    std::string              line;
    std::vector<std::string> fields;
    fields.reserve(44);

    while (std::getline(file_, line)) {
        if (line.empty()) continue;
        ++total_;

        parseLine(line, fields);

        ServiceRequest rec;
        if (!rec.fromFields(fields)) {
            ++skipped_;
            continue;
        }
        cb(std::move(rec));
    }
    return total_;
}
