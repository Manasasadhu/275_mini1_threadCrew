# multi_thread

This folder contains a **multi-threaded C++ implementation** and benchmarking harness for analyzing the **NYC 311 Service Requests dataset**.

It leverages **OpenMP** to enable parallel data processing and accelerate query execution on multi-core systems.

---

# 📁 Folder Structure

## `main.cpp`

The main entry point of the program. It includes:

* CSV data loading (with memory usage reporting)
* Core query implementations
* Unified benchmarking and timing logic
* Sample result output and summaries
* Utility templates for generic benchmarking and printing
* OpenMP-based parallel execution

---

## `ServiceRequest.h` / `ServiceRequest.cpp`

Defines the core data model and parsing utilities:

* `DateTime`

  * Compact and efficient date/time representation
  * Parsing and comparison support

* `ServiceRequest`

  * Represents a single NYC 311 record
  * Contains all relevant fields

* CSV parsing helpers

* `fromFields()` function to populate a `ServiceRequest` from parsed CSV fields

---

## `build/`

(Optional) Directory for build artifacts or out-of-source builds.

---

## `README.md`

Project documentation (this file).

---

# 🔎 Query Functions (main.cpp)

Each query operates on the loaded dataset and demonstrates a different type of filtering or aggregation using **OpenMP parallelism**.

---

## 1️⃣ Date Range Query

```cpp
filterByCreatedDateRange(start, end, numberOfThreads)
```

* Returns all service requests created between two `DateTime` values (inclusive).
* **Parallel Strategy:**
  The dataset is divided into chunks. Each thread filters its portion independently, and results are merged at the end.
* Efficient for large-scale range filtering.

---

## 2️⃣ Borough Filter

```cpp
filterByBorough(borough, numberOfThreads)
```

* Returns all requests matching a given borough (case-insensitive).
* **Parallel Strategy:**
  Threads process chunks independently and merge results.
* Optimized for categorical filtering.

---

## 3️⃣ Complaint Substring Search

```cpp
searchByComplaint(keyword, numberOfThreads)
```

* Returns all requests whose complaint type contains the given substring (case-insensitive).
* **Parallel Strategy:**
  Each thread performs substring search on its assigned data chunk.
* Enables fast large-scale text searching.

---

## 4️⃣ Latitude/Longitude Bounding Box

```cpp
filterByLatLonBox(minLat, maxLat, minLon, maxLon, numberOfThreads)
```

* Returns pointers to all requests within a specified geographic bounding box.
* **Parallel Strategy:**
  Threads independently check spatial inclusion and merge matching results.
* Efficient for spatial filtering.

---

## 5️⃣ Average Latitude

```cpp
averageLatitude(numberOfThreads)
```

* Computes the average latitude of all records.
* **Parallel Strategy:**
  Uses OpenMP reduction to sum latitudes in parallel.
* Demonstrates classic parallel reduction.

---

## 6️⃣ Borough Aggregation + Top Complaint (OMP Fast)

```cpp
aggregateByBorough_omp_fast()
```

* Groups requests by borough
* Counts total requests per borough
* Determines the most common complaint type per borough

**Parallel Strategy:**

* Each thread builds its own local aggregation map
* Local maps are merged at the end
* Minimizes contention and improves scalability

---

# ⏱ Benchmarking & Output

All queries use a unified benchmarking function that reports:

* Query label
* Result size or computed value
* Total execution time
* Average execution time
* Sample results (when applicable)

Utility templates automatically detect container types (vectors, maps, scalars) to support generic printing and benchmarking.

---

# 🚀 How to Use

## 1️⃣ Build

Compile using a C++17 compiler with OpenMP support:

```bash
g++ -std=c++17 -fopenmp -O2 -o main main.cpp ServiceRequest.cpp
```

---

## 2️⃣ Run

```bash
./main [csv_file] [num_threads]
```

### Arguments:

* `csv_file` (optional)
  Path to the NYC 311 CSV file
  (Default path may be hardcoded in `main.cpp`)

* `num_threads` (optional)
  Number of threads to use
  (Default: hardware concurrency or `OMP_NUM_THREADS` environment variable)

---

## 3️⃣ Experiment

You can:

* Add new queries in `main.cpp`
* Modify existing data structures
* Experiment with different OpenMP strategies
* Compare performance using the built-in benchmarking harness

---

# 📝 Notes

* This folder is self-contained and does not affect the main project build.
* Designed for clarity, performance, and experimentation.
* OpenMP is used for parallelism; tune thread count according to your hardware.
* The benchmarking system is generic and type-safe, making it easy to extend with new query types.

---

**Purpose:**
This implementation demonstrates how multi-threading can significantly accelerate large-scale data processing and analytical queries in C++.
