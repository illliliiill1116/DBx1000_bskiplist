#!/bin/bash

# compile all workloads
python3 compile_macrobenchmark.py

# Ensure output directory exists
mkdir -p out

# Loop over executables matching run_*
for exe in run_*; do
    # Skip if not executable
    [ -x "$exe" ] || continue

    workload="${exe#run_}"        # strip "run_" prefix
    outfile="./out/${workload}.txt"

    # Clear the output file if it exists
    > "$outfile"

    for i in {1..6}; do
        echo "=== Running $exe (workload=$workload), iteration $i ==="
        echo "=== Iteration $i ===" >> "$outfile"
        numactl --interleave=all "./$exe" >> "$outfile" 2>&1
        echo "" >> "$outfile"
    done
done

