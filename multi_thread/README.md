# Multi-Threaded NYC 311 Data Analyzer

This application loads and analyzes NYC 311 service request data using parallel processing (OpenMP and std::thread).

## Prerequisites

*   C++ Compiler (g++ or clang++) supporting C++17
*   OpenMP library (`libomp`)
    *   macOS (Homebrew): `brew install libomp`
    *   Linux: `sudo apt-get install libomp-dev`

## Building

To compile the application, run the `build.sh` script:

```bash
cd multi_thread
bash build.sh
```

This will create an executable named `build`.

## Running

You can run the application by providing the path to your CSV data file as an argument:

```bash
./build <path_to_csv_file>
```

### Examples

Run with a specific dataset:
```bash
./build /path/to/311_Service_Requests.csv
```

Run with the default sample data (if no argument is provided):
```bash
./build
```

## Features

The application performs the following parallel operations:
1.  **Parallel Load**: Reads and parses the CSV file using multiple threads.
2.  **Date Range Filter**: Finds requests within a specific year (2013).
3.  **Borough Filter**: Filters requests by borough (e.g., BROOKLYN).
4.  **Substring Search**: Searches for "rodent" in complaint types.
5.  **Sorting**: Sorts records by creation date.
6.  **Bounding Box**: Filters by latitude/longitude coordinates.
7.  **Aggregation**: Calculates average latitude.
