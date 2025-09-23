#!/usr/bin/env python3
import os
import re
import csv
from statistics import mean
from pathlib import Path

OUTDIR = Path("out")
PER_THREAD_FILE = "per_thread_throughput.csv"
IDX_TIME_FILE = "idx_time_per_100K_txns.csv"

# Regex to parse filenames: IDX_<option>_<workload>.txt
fname_re = re.compile(r"^IDX_([^_]+)_(.+)\.txt$")

# Data structure: results[option][workload] = {"throughput": [...], "idx_time": [...]}
results = {}

for file in OUTDIR.glob("IDX_*.txt"):
    m = fname_re.match(file.name)
    if not m:
        continue
    option, workload = m.groups()
    results.setdefault(option, {}).setdefault(workload, {"throughput": [], "idx_time": []})

    with open(file) as f:
        for line in f:
            line = line.strip()
            if not line.startswith("[summary]"):
                continue  # skip non-summary lines

            fields = dict(re.findall(r"(\w+)=(\d+)", line))
            txn_cnt = float(fields["txn_cnt"])
            run_time = float(fields["run_time"])
            time_index = float(fields["time_index"])

            throughput = txn_cnt / run_time if run_time > 0 else 0
            idx_time = 100000 * time_index / txn_cnt if txn_cnt > 0 else 0

            results[option][workload]["throughput"].append(throughput)
            results[option][workload]["idx_time"].append(idx_time)

# Collect all workloads and options
all_options = sorted(results.keys())
all_workloads = sorted({w for opt in results.values() for w in opt})

def write_csv(filename, metric):
    with open(filename, "w", newline="") as f:
        writer = csv.writer(f)
        # Header row
        writer.writerow(["Option"] + all_workloads)
        # Data rows
        for opt in all_options:
            row = [opt]
            for wl in all_workloads:
                if wl in results[opt]:
                    values = results[opt][wl][metric]
                    row.append(f"{mean(values):.6f}" if values else "")
                else:
                    row.append("")
            writer.writerow(row)

write_csv(PER_THREAD_FILE, "throughput")
write_csv(IDX_TIME_FILE, "idx_time")

print(f"Generated {PER_THREAD_FILE} and {IDX_TIME_FILE}")
