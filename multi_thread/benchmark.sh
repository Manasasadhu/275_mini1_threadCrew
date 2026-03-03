#!/bin/bash
# benchmark.sh — Runs the multi-thread build with 1, 2, 4, 8, and max threads.
# Usage: bash benchmark.sh [csv_file]
# Results are saved to benchmark_results.txt

CSV="${1:-sample_data.csv}"
BINARY="./build"
RESULTS="benchmark_results.txt"
THREAD_COUNTS=(4)

# Deduplicate thread counts (in case hw.logicalcpu == 8)
THREAD_COUNTS=($(echo "${THREAD_COUNTS[@]}" | tr ' ' '\n' | sort -nu))

echo "========================================================"
echo "  Multi-Thread Benchmark"
echo "  Dataset : $CSV"
echo "  Logical CPUs : $(sysctl -n hw.logicalcpu)"
echo "  Thread counts: ${THREAD_COUNTS[*]}"
echo "========================================================"
echo ""

# Clear previous results
echo "Multi-Thread Benchmark — $(date)" > "$RESULTS"
echo "Dataset: $CSV" >> "$RESULTS"
echo "Logical CPUs: $(sysctl -n hw.logicalcpu)" >> "$RESULTS"
echo "========================================================" >> "$RESULTS"

for T in "${THREAD_COUNTS[@]}"; do
    echo "------------------------------------------------------------"
    echo "Running with $T thread(s)..."
    echo "------------------------------------------------------------"

    echo "" >> "$RESULTS"
    echo "=== Threads: $T ===" >> "$RESULTS"

    # Run and capture output; tee to terminal and results file
    "$BINARY" "$CSV" "$T" 2>&1 | grep -E "(Thread count|Loaded|Memory|date range|borough|complaint|sorted|lat\/lon|average)" | tee -a "$RESULTS"

    echo ""
done

echo ""
echo "========================================================"
echo "Benchmark complete. Full results saved to: multi_thread/$RESULTS"
echo "========================================================"
