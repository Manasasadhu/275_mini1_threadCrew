# multi_thread

This folder contains a multi-threaded C++ implementation and benchmarking harness for analyzing the NYC 311 Service Requests dataset. It leverages OpenMP for parallelism to accelerate data processing and query execution on multi-core systems.

## Folder Contents

- **main.cpp**  
  The main entry point. Implements:
  - Data loading from CSV (with memory usage reporting)
  - Core query functions (see below for details)
  - Unified benchmarking and timing logic for all queries
  - Sample output and result summaries for each query
  - Utility templates for generic benchmarking and sample printing
  - OpenMP-based parallelism for high performance

- **ServiceRequest.h / ServiceRequest.cpp**  
  Defines the core data structures and parsing logic:
  - `DateTime`: Compact, efficient date/time representation with parsing and comparison
  - `ServiceRequest`: Struct modeling a single NYC 311 record, with all relevant fields
  - Parsing helpers for robust CSV field conversion
  - `fromFields`: Populates a `ServiceRequest` from a vector of CSV strings

- **build/**  
  (Optional) Directory for build artifacts or out-of-source builds.

- **README.md**  
  This file. Explains the folder structure and usage.

## Query Functions (main.cpp)

Each query operates on the loaded NYC 311 dataset and demonstrates a different type of data access or aggregation, with OpenMP-based parallelism where applicable:

1. **Date Range Query**
   - `filterByCreatedDateRange(start, end, numberOfThreads)`
   - Returns all service requests created between two DateTime values (inclusive).
   - **Parallelism:** The dataset is divided among threads, each thread filters its chunk, and results are combined at the end. This accelerates range filtering on large datasets.

2. **Borough Filter**
   - `filterByBorough(borough, numberOfThreads)`
   - Returns all requests matching a given borough (case-insensitive).
   - **Parallelism:** Each thread processes a chunk of the data, performing case-insensitive comparison, and results are merged. This speeds up categorical filtering.

3. **Complaint Substring Search**
   - `searchByComplaint(keyword, numberOfThreads)`
   - Returns all requests whose complaint type contains the given substring (case-insensitive).
   - **Parallelism:** Threads search their assigned data chunks for the substring, and results are merged. This enables fast text search across millions of records.

4. **Latitude/Longitude Bounding Box**
   - `filterByLatLonBox(minLat, maxLat, minLon, maxLon, numberOfThreads)`
   - Returns pointers to all requests within a specified geographic bounding box.
   - **Parallelism:** Each thread checks its chunk for spatial inclusion, storing pointers to matching records. Results are merged for efficiency.

5. **Average Latitude**
   - `averageLatitude(numberOfThreads)`
   - Computes the average latitude of all loaded records.
   - **Parallelism:** Uses OpenMP reduction to sum latitudes in parallel, then divides by total count. This is a classic parallel reduction pattern.

6. **Borough Aggregation + Top Complaint (OMP Fast)**
   - `aggregateByBorough_omp_fast()`
   - Groups requests by borough, counts total requests per borough, and finds the most common complaint type in each borough.
   - **Parallelism:** Each thread builds its own local aggregation map, then all maps are merged in a final step. This avoids contention and is highly scalable for grouping and aggregation tasks.

## Benchmarking and Output

- All queries are benchmarked using a unified timing function that prints:
  - Query label
  - Result size or value
  - Total and average execution time
  - A sample of the result (where applicable)

- Utility templates detect container types and enable generic printing/benchmarking for vectors, maps, and scalars.

## How to Use

1. **Build:**  
   Compile with a C++17 compiler and OpenMP support:
   ```
   g++ -std=c++17 -fopenmp -O2 -o main main.cpp ServiceRequest.cpp
   ```

2. **Run:**  
   ```
   ./main [csv_file] [num_threads]
   ```
   - `csv_file` (optional): Path to the NYC 311 CSV file (default is hardcoded in main.cpp)
   - `num_threads` (optional): Number of threads to use (default: hardware concurrency or OMP_THREAD_COUNT env variable)

3. **Experiment:**  
   - Add or modify queries in `main.cpp`
   - Try new data structures or optimizations
   - Use the benchmarking harness to compare performance

## Notes

- This folder is self-contained and does not affect the main project build.
- Designed for clarity, performance, and experimentation—feel free to add new files or try alternative designs.
- OpenMP is used for parallelism; you can tune thread count for your hardware.
- The benchmarking harness and sample output logic are type-safe and generic, making it easy to add new queries or result types.
