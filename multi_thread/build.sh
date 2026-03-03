#!/bin/bash
# Detect Homebrew OpenMP location
if [ -d "/opt/homebrew/opt/libomp" ]; then
    OMP_PREFIX="/opt/homebrew/opt/libomp"
elif [ -d "/usr/local/opt/libomp" ]; then
    OMP_PREFIX="/usr/local/opt/libomp"
else
    echo "Error: libomp not found. Install with: brew install libomp"
    exit 1
fi

g++ -std=c++17 -O2 -Xpreprocessor -fopenmp \
    -I"$OMP_PREFIX/include" -I../single_thread \
    -L"$OMP_PREFIX/lib" -lomp \
    main.cpp ../single_thread/ServiceRequest.cpp \
    -o build
