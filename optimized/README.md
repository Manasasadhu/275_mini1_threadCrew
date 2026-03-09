# Optimized C++ Implementation
This folder contains a high-performance, parallel C++ implementation for analyzing the NYC 311 Service Requests dataset using an **Object-of-Arrays (OoA)** memory layout. This approach is designed to maximize cache efficiency, enable vectorized operations, and accelerate parallel queries on large datasets.

## What is Object-of-Arrays (OoA)?

- **Traditional (AoS):** Each record is a struct/class with all fields together in memory.
- **OoA:** Each field is stored as a separate contiguous array (vector), so all values for a field are adjacent in memory.

**Benefits:**
- Improved cache locality for columnar access patterns (e.g., filtering by a single field).
- Enables SIMD/vectorization and efficient parallelization.
- Reduces memory overhead for large datasets.

## Folder Contents

- **ServiceRequest.h / ServiceRequest.cpp**  
  - Defines the `ServiceRequestOoA` struct: a set of vectors, one for each field in the NYC 311 dataset.
  - Includes loader logic to efficiently populate the OoA structure from CSV.

- **queries.h / queries.cpp**  
  - Implements all core queries using the OoA layout and OpenMP for parallelism.
  - Each query returns indices (std::vector<size_t>) into the arrays, not copies of records, for efficiency.
  - Includes aggregation and statistical queries.

- **main.cpp**  
  - Entry point for benchmarking and running queries.
  - Unified, type-safe benchmarking and sample output logic.
  - Demonstrates all queries and prints timing, result size, and sample output.

- **build/**  
  - (Optional) Directory for build artifacts.

## Query Functions

Each query is implemented to take advantage of the OoA layout and OpenMP parallelism:

1. **Date Range Query**  
   - `filterByCreatedDateRangeOoA_omp(data, startKey, endKey, threads)`
   - Returns indices of requests whose createdDate is in the given range.
   - **OoA/Parallelism:** Only the createdDate array is scanned in parallel, maximizing cache efficiency.

2. **Borough Filter**  
   - `filterByBoroughOoA_omp(data, boroughUpper, threads)`
   - Returns indices of requests matching a given borough (case-insensitive).
   - **OoA/Parallelism:** Only the borough array is scanned in parallel.

3. **Complaint Substring Search**  
   - `searchByComplaintOoA(data, keyword, threads)`
   - Returns indices of requests whose complaintType contains the keyword (case-insensitive).
   - **OoA/Parallelism:** Only the complaintType array is scanned in parallel.

4. **Latitude/Longitude Bounding Box**  
   - `filterByLatLonBoxOoA(data, minLat, maxLat, minLon, maxLon, threads)`
   - Returns indices of requests within a geographic bounding box.
   - **OoA/Parallelism:** Only the latitude and longitude arrays are scanned in parallel.

5. **Average Latitude**  
   - `averageLatitudeOoA_omp(data, threads)`
   - Computes the average latitude of all records.
   - **OoA/Parallelism:** The latitude array is summed in parallel using OpenMP reduction.

6. **Borough Aggregation + Top Complaint**  
   - `aggregateByBoroughOoA_omp_fast(data, threads)`
   - Groups requests by borough, counts totals, and finds the most common complaint type per borough.
   - **OoA/Parallelism:** Each thread builds a local aggregation map, then all maps are merged. This is highly scalable and avoids contention.

## Improvements Over AoS

- **Performance:**  
  - Faster queries due to better cache usage and memory access patterns.
  - Parallel queries scale efficiently with thread count.
  - Lower memory overhead for large datasets.

- **Flexibility:**  
  - Easy to add new queries that operate on specific fields.
  - Efficient for both analytical and filtering workloads.

- **Sample Output:**  
  - All queries print timing, result size, and a sample of the results for easy validation.

## How to Use

1. **Build:**  
   ```
   g++ -std=c++17 -fopenmp -O2 -o main main.cpp ServiceRequest.cpp queries.cpp
   ```

2. **Run:**  
   ```
   ./main [csv_file] [num_threads]
   ```
   - `csv_file` (optional): Path to the NYC 311 CSV file (default is hardcoded in main.cpp)
   - `num_threads` (optional): Number of threads to use (default: hardware concurrency)


