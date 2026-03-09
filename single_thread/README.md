# single_thread

This folder contains a single-threaded C++ implementation for analyzing the NYC 311 Service Requests dataset. It is designed for clarity, correctness, and ease of experimentation with data structures and query logic.

---

# Folder Structure

## main.cpp

The main entry point of the program. It includes:

* Data loading from CSV (with memory usage reporting)
* Core query implementations

---

## ServiceRequest.h / ServiceRequest.cpp

Defines the core data structures and parsing logic:

### DateTime

* Compact and efficient date/time representation
* Parsing and comparison support

### ServiceRequest

* Struct modeling a single NYC 311 record
* Contains all relevant fields

Additional components:

* Parsing helpers for robust CSV field conversion
* `fromFields()` function to populate a `ServiceRequest` from a vector of CSV strings

---

## build/

(Optional) Directory for build artifacts or out-of-source builds.

---

## mainc.cpp

(Optional/experimental)
May contain alternative or experimental code not wired into the main build.

---

## README.md

Project documentation (this file).

---

# Query Functions (main.cpp)

Each query operates on the loaded NYC 311 dataset and demonstrates a different type of data access or aggregation.

---

## 1. Date Range Query

```cpp id="0j8t3m"
filterByCreatedDateRange(start, end)
```

* Returns all service requests created between two `DateTime` values (inclusive).
* Demonstrates range filtering on a timestamp field.

---

## 2. Borough Filter

```cpp id="j5lq9c"
filterByBorough(borough)
```

* Returns all requests matching a given borough (case-insensitive).
* Demonstrates exact match filtering on a categorical string field.

---

## 3. Complaint Substring Search

```cpp id="k9s2px"
searchByComplaint(keyword)
```

* Returns all requests whose complaint type contains the given substring (case-insensitive).
* Demonstrates substring search on a text field.

---

## 4. Latitude/Longitude Bounding Box

```cpp id="z3mv7d"
filterByLatLonBox(minLat, maxLat, minLon, maxLon)
```

* Returns pointers to all requests within a specified geographic bounding box.
* Demonstrates spatial filtering and pointer-based result sets for efficiency.

---

## 5. Average Latitude

```cpp id="x6qn2v"
averageLatitude()
```

* Computes the average latitude of all loaded records.
* Demonstrates a simple aggregation (reduce) operation.

---

## 6. Borough Aggregation + Top Complaint

```cpp id="t8uy4r"
aggregateByBorough()
```

* Groups requests by borough
* Counts total requests per borough
* Finds the most common complaint type in each borough
* Demonstrates grouping, aggregation, and top-frequency computation within groups.

---

# Commands

## Build

Compile with a C++17 compiler:

```bash id="b6h2nm"
g++ -std=c++17 -O2 -o main main.cpp ServiceRequest.cpp
```

---

## Run

```bash id="c4rz8p"
./main
```

Edit the CSV file path in `main.cpp` as needed before running.
