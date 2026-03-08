# Requirements Document

## Introduction

This feature adds multi-threaded parallelization to the existing single-threaded NYC 311 service request processing system (Phase 2 of SJSU 275 EAD Mini Project 1). The existing system loads ~14 million records from a ~12GB CSV file and performs six query operations (date range filter, borough filter, complaint search, lat/lon bounding box, sort by date, average latitude). The multi-threaded version reuses the same `ServiceRequest` and `DateTime` data classes and parallelizes both the CSV loading stage and the query execution stage using C++ standard threading primitives and/or OpenMP, targeting measurable speedup over the Phase 1 single-threaded baseline.

## Glossary

- **Loader**: The component responsible for reading the CSV file and constructing `ServiceRequest` objects in memory
- **Query_Engine**: The component that executes filter, search, sort, and aggregation operations over the loaded dataset
- **Benchmark_Harness**: The component that measures and reports execution time for data loading and each query operation across multiple runs
- **Thread_Pool**: A set of pre-created worker threads that execute parallelized work chunks
- **Record_Set**: The in-memory collection of `ServiceRequest` objects (global `g_records` vector)
- **Chunk**: A contiguous sub-range of the Record_Set assigned to a single thread for parallel processing
- **Speedup**: The ratio of single-threaded execution time to multi-threaded execution time for the same operation

## Requirements

### Requirement 1: Parallel CSV Data Loading

**User Story:** As a researcher, I want the CSV loading phase to use multiple threads, so that the 14+ million records load faster than the single-threaded baseline.

#### Acceptance Criteria

1. WHEN the CSV file path is provided, THE Loader SHALL read the file and divide the raw data into Chunks for parallel parsing across multiple threads
2. WHEN all Chunks have been parsed, THE Loader SHALL merge the per-thread results into a single Record_Set that contains the same records as the single-threaded Loader would produce
3. THE Loader SHALL report the total wall-clock time for the parallel load operation
4. THE Loader SHALL report the resident memory usage before and after loading
5. IF the CSV file cannot be opened, THEN THE Loader SHALL print an error message to stderr and return an empty Record_Set
6. WHEN loading completes, THE Loader SHALL print the total number of valid records loaded and the number of lines processed

### Requirement 2: Parallel Date Range Filter

**User Story:** As a researcher, I want the date range filter query to execute in parallel across threads, so that filtering by created date is faster than the single-threaded version.

#### Acceptance Criteria

1. WHEN a start DateTime and end DateTime are provided, THE Query_Engine SHALL partition the Record_Set into Chunks and filter each Chunk concurrently using separate threads
2. WHEN all threads complete, THE Query_Engine SHALL merge per-thread results into a single output vector containing all records where createdDate falls within the specified range (inclusive)
3. THE Query_Engine SHALL produce the same result set as the single-threaded `filterByCreatedDateRange` function for any given date range

### Requirement 3: Parallel Borough Filter

**User Story:** As a researcher, I want the borough filter query to run in parallel, so that exact-match filtering by borough is faster than the single-threaded version.

#### Acceptance Criteria

1. WHEN a borough name is provided, THE Query_Engine SHALL partition the Record_Set into Chunks and filter each Chunk concurrently using separate threads
2. THE Query_Engine SHALL perform case-insensitive comparison of the borough field
3. WHEN all threads complete, THE Query_Engine SHALL merge per-thread results into a single output vector containing all matching records
4. THE Query_Engine SHALL produce the same result set as the single-threaded `filterByBorough` function for any given borough name

### Requirement 4: Parallel Complaint Substring Search

**User Story:** As a researcher, I want the complaint type substring search to run in parallel, so that searching complaint descriptions is faster than the single-threaded version.

#### Acceptance Criteria

1. WHEN a keyword string is provided, THE Query_Engine SHALL partition the Record_Set into Chunks and search each Chunk concurrently using separate threads
2. THE Query_Engine SHALL perform case-insensitive substring matching on the complaintType field
3. WHEN all threads complete, THE Query_Engine SHALL merge per-thread results into a single output vector containing all matching records
4. THE Query_Engine SHALL produce the same result set as the single-threaded `searchByComplaint` function for any given keyword

### Requirement 5: Parallel Latitude/Longitude Bounding Box Filter

**User Story:** As a researcher, I want the lat/lon bounding box filter to run in parallel, so that spatial filtering is faster than the single-threaded version.

#### Acceptance Criteria

1. WHEN minimum and maximum latitude and longitude values are provided, THE Query_Engine SHALL partition the Record_Set into Chunks and filter each Chunk concurrently using separate threads
2. WHEN all threads complete, THE Query_Engine SHALL merge per-thread results into a single output vector of pointers to matching records
3. THE Query_Engine SHALL produce the same result set as the single-threaded `filterByLatLonBox` function for any given bounding box

### Requirement 6: Parallel Sort by Created Date

**User Story:** As a researcher, I want the sort-by-date operation to use parallel sorting, so that sorting 14+ million records is faster than the single-threaded version.

#### Acceptance Criteria

1. WHEN sort is invoked, THE Query_Engine SHALL create a copy of the Record_Set and sort the copy by createdDate in ascending order using a parallel sorting algorithm
2. THE Query_Engine SHALL produce the same ordering as the single-threaded `sortByCreatedDate` function
3. THE Query_Engine SHALL use a parallel sort strategy such as parallel merge sort, parallel partitioning, or OpenMP parallel sort

### Requirement 7: Parallel Average Latitude Aggregation

**User Story:** As a researcher, I want the average latitude computation to run in parallel, so that aggregation over 14+ million records is faster than the single-threaded version.

#### Acceptance Criteria

1. WHEN average latitude is invoked, THE Query_Engine SHALL partition the Record_Set into Chunks, compute partial sums concurrently, and combine the partial sums into a final average
2. THE Query_Engine SHALL produce the same numerical result (within floating-point tolerance of 1e-9) as the single-threaded `averageLatitude` function
3. IF the Record_Set is empty, THEN THE Query_Engine SHALL return 0.0

### Requirement 8: Configurable Thread Count

**User Story:** As a researcher, I want to control the number of threads used, so that I can benchmark performance at different thread counts for my report.

#### Acceptance Criteria

1. THE Query_Engine SHALL default the thread count to the number of hardware threads available on the system
2. WHERE a custom thread count is specified, THE Query_Engine SHALL use that thread count for all parallel operations
3. THE Benchmark_Harness SHALL print the thread count being used at the start of execution

### Requirement 9: Benchmark Harness with Comparison Metrics

**User Story:** As a researcher, I want the multi-threaded version to include the same benchmarking harness as Phase 1, so that I can directly compare single-threaded and multi-threaded performance.

#### Acceptance Criteria

1. THE Benchmark_Harness SHALL execute each query operation a configurable number of times (default 15) and report total time and average time per run
2. THE Benchmark_Harness SHALL measure and report wall-clock time for the data loading phase
3. THE Benchmark_Harness SHALL measure and report resident memory usage before and after data loading
4. THE Benchmark_Harness SHALL print sample records after loading for verification against Phase 1 output

### Requirement 10: Build System Integration

**User Story:** As a researcher, I want the multi-threaded code to build with CMake, so that it meets the assignment's tooling requirements.

#### Acceptance Criteria

1. THE multi-threaded build SHALL use CMake as the build system
2. THE multi-threaded build SHALL compile with g++ v13+ or Clang v16+ using C++17 or later
3. WHERE OpenMP is used, THE CMake configuration SHALL find and link OpenMP automatically
4. THE CMake configuration SHALL produce a separate executable for the multi-threaded version
