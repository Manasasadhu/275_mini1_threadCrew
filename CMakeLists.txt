cmake_minimum_required(VERSION 3.16)
project(mini1_311 VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Build type defaults to Release for benchmarking ---
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Compiler flags ---
# -O2 for Release, add -march=native to use local CPU features
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -march=native")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -Wall -Wextra -Wpedantic")

# --- Library: all reusable source files ---
add_library(nyc311
    src/DateTime.cpp
    src/ServiceRequest.cpp
    src/CsvReader.cpp
    src/DataStore.cpp
    src/NYC311Analyzer.cpp
)

target_include_directories(nyc311 PUBLIC include)

# --- Main benchmark executable ---
add_executable(benchmark main.cpp)
target_link_libraries(benchmark PRIVATE nyc311)

# --- Optional: print build info ---
message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
