# NYC 311 Service Request Analysis: Serial vs. Parallel Processing

## 1. Dataset Information

- **Dataset:** NYC 311 Service Requests (2010–2023)
- **Source:** [NYC Open Data - 311 Service Requests](https://data.cityofnewyork.us/Social-Services/311-Service-Requests-from-2010-to-2019/76ig-c548/about_data) and [2020 to Present](https://data.cityofnewyork.us/Social-Services/311-Service-Requests-from-2020-to-Present/erm2-nwe9/about_data)
- **Combined File:** `311_combined.csv` (~12.34 GB, 20.3 million records)
- **Records Loaded:** 14,000,000 (capped for memory stability)
- **Fields/Record:** 44 columns (complaint type, borough, agency, lat/lon, dates, status, zip code, etc.)
- **Data Structure:** Each record is a `ServiceRequest` struct (primitives, custom `DateTime`, and `std::string` fields), loaded into a global vector or columnar layout depending on implementation.

## 2. Folder Structure & Navigation

- [`single_thread/`](single_thread/README.md): Baseline serial implementation. Loads and queries the dataset using a single thread. All queries and benchmarking logic are implemented here.
- [`multi_thread/`](multi_thread/README.md): Adds OpenMP-based parallelism to all queries and data loading. Thread count is configurable. Highlights both the benefits and pitfalls of naive parallelization.
- [`optimized/`](optimized/README.md): Implements advanced optimizations, including an Object-of-Arrays (OoA) memory layout, pointer-based result sets, and precomputed fields. Achieves the best performance by addressing memory and cache bottlenecks.

Each folder contains its own `README.md` with detailed explanations, build/run instructions, and query logic.

## 3. Project Overview & Key Learnings

This project benchmarks serial and parallel processing strategies on the NYC 311 Service Request dataset—a large, real-world dataset with over 14 million records. We implemented and compared three approaches:

- **Single-threaded (serial):** Establishes a baseline for all queries. Predictable, stable timings, but limited by CPU and memory bandwidth.
- **Multi-threaded (OpenMP):** Parallelizes all queries and data loading. Some compute-bound queries (e.g., average latitude, complaint keyword search) saw modest speedups. However, queries with large result sets or requiring synchronization (e.g., borough filter, aggregation) were often slower due to memory allocation and lock contention.
- **Optimized (OoA, pointer results):** Redesigns memory layout for cache efficiency, returns pointers instead of copies, and precomputes fields for fast comparison. This approach delivered the largest speedups—up to 99% faster for some queries—by eliminating bottlenecks unrelated to threading.

### What We Learned
- **Parallelism is not a silver bullet:** Adding threads does not guarantee speedup. For this dataset, naive parallelism often made things worse, especially for queries that copy large, string-heavy structs or require fine-grained locking.
- **Memory layout matters:** Switching from an Array-of-Structs to an Object-of-Arrays layout improved cache utilization and enabled compiler auto-vectorization, delivering speedups that threading alone could not achieve.
- **Result size and synchronization are critical:** Queries with small outputs and no locks scale well; those with large outputs or critical sections do not.
- **Know your data before parallelizing:** The NYC 311 dataset’s wide, string-heavy records and large result sets made it a poor fit for naive multi-threading. Data design and workload analysis are more important than simply increasing thread count.
- **C++ learning curve:** Building this project deepened our understanding of C++ (memory management, move semantics, OpenMP scoping, etc.) and the importance of profiling and careful design.

## 4. References
- NYC Open Data: [2010–2019](https://data.cityofnewyork.us/Social-Services/311-Service-Requests-from-2010-to-2019/76ig-c548/about_data), [2020–Present](https://data.cityofnewyork.us/Social-Services/311-Service-Requests-from-2020-to-Present/erm2-nwe9/about_data)
- [OpenMP Reference Guide v6.0](https://www.openmp.org/wp-content/uploads/OpenMP-RefGuide-6.0-OMP60SC24-web.pdf)
- [GeeksforGeeks: C++ Parallel for Loop in OpenMP](https://www.geeksforgeeks.org/c/c-parallel-for-loop-in-openmp/)
- [UVA CS OpenMP Tutorial](https://www.cs.virginia.edu/~cr4bd/3130/S2024/labhw/openmp.html)
- [cppreference.com: std::chrono](https://en.cppreference.com/w/cpp/chrono)
