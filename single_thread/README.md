# single_thread

This folder contains a single-threaded C++ implementation and benchmarking harness for analyzing the NYC 311 Service Requests dataset. It is designed for clarity, correctness, and ease of experimentation with data structures and query logic.

## Folder Contents

- **main.cpp**  
  The main entry point. Implements:
  - Data loading from CSV (with memory usage reporting)
  - Core query functions (see below for details)
  - Unified benchmarking and timing logic for all queries
  - Sample output and result summaries for each query
  - Utility templates for generic benchmarking and sample printing

- **ServiceRequest.h / ServiceRequest.cpp**  
  Defines the core data structures and parsing logic:
  - `DateTime`: Compact, efficient date/time representation with parsing and comparison
  - `ServiceRequest`: Struct modeling a single NYC 311 record, with all relevant fields
  - Parsing helpers for robust CSV field conversion
  - `fromFields`: Populates a `ServiceRequest` from a vector of CSV strings

- **build/**  
  (Optional) Directory for build artifacts or out-of-source builds.

- **mainc.cpp**  
  (Optional/experimental) May contain alternative or experimental code not wired into the main build.

- **README.md**  
  This file. Explains the folder structure and usage.

## Query Functions (main.cpp)

Each query operates on the loaded NYC 311 dataset and demonstrates a different type of data access or aggregation:

1. **Date Range Query**
   - `filterByCreatedDateRange(start, end)`
   - Returns all service requests created between two DateTime values (inclusive).
   - Demonstrates range filtering on a timestamp field.

2. **Borough Filter**
   - `filterByBorough(borough)`
   - Returns all requests matching a given borough (case-insensitive).
   - Demonstrates exact match filtering on a categorical string field.

3. **Complaint Substring Search**
   - `searchByComplaint(keyword)`
   - Returns all requests whose complaint type contains the given substring (case-insensitive).
   - Demonstrates substring search on a text field.

4. **Latitude/Longitude Bounding Box**
   - `filterByLatLonBox(minLat, maxLat, minLon, maxLon)`
   - Returns pointers to all requests within a specified geographic bounding box.
   - Demonstrates spatial filtering and pointer-based result sets for efficiency.

5. **Average Latitude**
   - `averageLatitude()`
   - Computes the average latitude of all loaded records.
   - Demonstrates a simple aggregation (reduce) operation.

6. **Borough Aggregation + Top Complaint**
   - `aggregateByBorough()`
   - Groups requests by borough, counts total requests per borough, and finds the most common complaint type in each borough.
   - Demonstrates grouping, aggregation, and finding top-N within groups.

## Benchmarking and Output

- All queries are benchmarked using a unified timing function that prints:
  - Query label
  - Result size or value
  - Total and average execution time
  - A sample of the result (where applicable)

- Utility templates detect container types and enable generic printing/benchmarking for vectors, maps, and scalars.

## How to Use

1. **Build:**  
   Compile with a C++17 compiler:
   ```
   g++ -std=c++17 -O2 -o main main.cpp ServiceRequest.cpp
   ```

2. **Run:**  
   ```
   ./main
   ```
   (Edit the CSV file path in `main.cpp` as needed.)

3. **Experiment:**  
   - Add or modify queries in `main.cpp`
   - Try new data structures or optimizations
   - Use the benchmarking harness to compare performance

## Notes

- This folder is self-contained and does not affect the main project build.
- Designed for clarity and experimentation—feel free to add new files or try alternative designs.

