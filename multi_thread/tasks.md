# Implementation Plan: Multi-Threaded Service Requests

## Overview

Parallelize the existing single-threaded NYC 311 service request system. Copy shared data classes, create a CMake build with OpenMP, implement parallel CSV loading with std::thread, implement 6 parallel query functions with OpenMP, and wire everything together in a benchmark harness matching the single-threaded output format.

## Tasks

- [x] 1. Set up multi_thread project files and build system
  - [x] 1.1 Copy ServiceRequest.h and ServiceRequest.cpp into multi_thread/
    - Copy `single_thread/ServiceRequest.h` to `multi_thread/ServiceRequest.h`
    - Copy `single_thread/ServiceRequest.cpp` to `multi_thread/ServiceRequest.cpp`
    - No modifications needed — these files are thread-safe as-is
    - _Requirements: 10.1, 10.4_

  - [x] 1.2 Create CMakeLists.txt for multi_thread/
    - Set `cmake_minimum_required` and `project()`
    - Set C++17 standard (`CMAKE_CXX_STANDARD 17`)
    - Use `find_package(OpenMP REQUIRED)`
    - Add executable target from `main.cpp` and `ServiceRequest.cpp`
    - Link OpenMP via `target_link_libraries`
    - Add `-O2` optimization flag
    - _Requirements: 10.1, 10.2, 10.3, 10.4_

- [x] 2. Implement utility functions and global state in multi_thread/main.cpp
  - [x] 2.1 Add includes, global g_records vector, and utility functions
    - Add all needed `#include` directives (iostream, fstream, vector, string, chrono, thread, algorithm, cstring, omp.h, mach/mach.h)
    - Declare `static vector<ServiceRequest> g_records`
    - Copy `cleanString()`, `parseCSVLine()`, and `rssMemMB()` from single_thread/main.cpp unchanged
    - _Requirements: 9.3, 1.4_

- [x] 3. Implement parallel CSV loading with std::thread
  - [x] 3.1 Implement loadDataParallel()
    - Open the file with `ifstream`, check if it opened (print error to stderr and return empty vector if not)
    - Seek to end to get file size, then read the entire file into a single `string` buffer
    - Divide the buffer into `numberOfThreads` roughly equal byte chunks
    - For each chunk boundary, scan forward to the next `\n` so no line gets split
    - Skip the header line in the first chunk
    - Spawn one `std::thread` per chunk — each thread loops through its portion line by line, calls `parseCSVLine` and `ServiceRequest::fromFields`, pushes valid records into its own local `vector<ServiceRequest>`
    - After all threads join, merge all local vectors into one result vector
    - Time the whole operation with `chrono::high_resolution_clock` and print duration
    - Print total lines processed and total valid records loaded
    - Print per-thread chunk sizes in bytes
    - If thread count is 0, treat as 1
    - _Requirements: 1.1, 1.2, 1.3, 1.5, 1.6, 8.1, 8.2_

- [x] 4. Checkpoint - Verify build and loading
  - Ensure the project builds with CMake and the parallel loader compiles. Ask the user if questions arise.

- [x] 5. Implement parallel query functions with OpenMP
  - [x] 5.1 Implement filterByCreatedDateRange (parallel)
    - Accept `DateTime start`, `DateTime end`, `int numberOfThreads`
    - Call `omp_set_num_threads(numberOfThreads)`
    - Create a `vector<vector<ServiceRequest>> localResults(numberOfThreads)`
    - Use `#pragma omp parallel for schedule(static)` to loop over g_records
    - Each thread gets its thread number with `omp_get_thread_num()` and pushes matching records into `localResults[threadNum]`
    - After the parallel loop, merge all localResults into one output vector with simple for loops
    - _Requirements: 2.1, 2.2, 2.3_

  - [x] 5.2 Implement filterByBorough (parallel, case-insensitive)
    - Same parallel pattern as 5.1
    - Convert both the record's borough and the input borough to uppercase before comparing (simple for loop over chars with `toupper`)
    - _Requirements: 3.1, 3.2, 3.3, 3.4_

  - [x] 5.3 Implement searchByComplaint (parallel, case-insensitive)
    - Same parallel pattern as 5.1
    - Convert both the record's complaintType and the keyword to lowercase before doing `string::find`
    - _Requirements: 4.1, 4.2, 4.3, 4.4_

  - [x] 5.4 Implement filterByLatLonBox (parallel, returns pointers)
    - Same parallel pattern but use `vector<vector<const ServiceRequest*>>` for localResults
    - Check if latitude and longitude fall within the min/max bounds
    - Merge into a single `vector<const ServiceRequest*>`
    - _Requirements: 5.1, 5.2, 5.3_

  - [x] 5.5 Implement sortByCreatedDate (parallel sort)
    - Accept `int numberOfThreads`, call `omp_set_num_threads(numberOfThreads)`
    - Copy g_records into a new vector
    - Use `__gnu_parallel::sort` if available (GCC), otherwise fall back to `std::sort` with OpenMP parallel sections for a simple parallel merge sort
    - Sort by `createdDate` ascending using the same comparator as single-threaded version
    - _Requirements: 6.1, 6.2, 6.3_

  - [x] 5.6 Implement averageLatitude (OpenMP reduction)
    - Accept `int numberOfThreads`, call `omp_set_num_threads(numberOfThreads)`
    - Use `#pragma omp parallel for reduction(+:sum)` to sum all latitudes
    - Divide by `g_records.size()`, return 0.0 if empty
    - _Requirements: 7.1, 7.2, 7.3_

- [x] 6. Checkpoint - Verify queries compile
  - Ensure all query functions compile cleanly with OpenMP. Ask the user if questions arise.

- [x] 7. Implement main() with benchmark harness
  - [x] 7.1 Implement main() function
    - Get thread count from `thread::hardware_concurrency()` (default)
    - Print thread count at start
    - Print memory before load with `rssMemMB()`
    - Call `loadDataParallel()` and assign to `g_records`
    - Print memory after load and memory delta
    - If g_records is empty, print error and exit
    - Print first 5 sample records in same format as single-threaded: `#N: uniqueKey | createdDate | borough | complaintType`
    - _Requirements: 8.1, 8.3, 9.2, 9.3, 9.4_

  - [x] 7.2 Implement benchmark timing loop
    - Create `measureVectorQuery` and `measureScalarQuery` helper lambdas matching the single-threaded version's output format
    - Run each of the 6 queries 15 times (configurable `runs` variable)
    - For each query, print: label, result size (or value), total time, average time
    - Pass `numberOfThreads` to each query call
    - Query parameters match single-threaded: date range 2013, borough BROOKLYN, complaint "rodent", NYC lat/lon box (40.5–40.9, -74.25– -73.7), average latitude
    - _Requirements: 9.1, 9.2_

- [x] 8. Final checkpoint - Full build and run verification
  - Ensure the full project builds with CMake, all functions are wired together, and the output format matches the single-threaded version. Ask the user if questions arise.

## Notes

- C++17, compiled with g++ v13+ or Clang v16+
- OpenMP for queries, std::thread for CSV loading
- macOS memory tracking via mach/mach.h
- Simple coding style — basic for loops, straightforward variable names, no advanced patterns
- No tests per user request
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
