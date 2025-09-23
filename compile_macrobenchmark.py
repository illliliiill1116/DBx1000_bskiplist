import os, sys, re, os.path
import platform
import subprocess, datetime, time, signal

def replace(filename, pattern, replacement):
	f = open(filename)
	s = f.read()
	f.close()
	s = re.sub(pattern,replacement,s)
	f = open(filename,'w')
	f.write(s)
	f.close()

jobs = {}
dbms_cfg = ["config-std.h", "config.h"]
algs = ['IDX_SKIPLIST', 'IDX_SKIPLISTxFS', 'IDX_SKIPLISTxFSxSIMD']
workloads = ['YCSB', 'TPCC']
threads = ['128']
YCSB_workloads = ['A', 'B', 'C']
YCSB_sizes = ['10485760', '33554432']

def insert_job(alg, workload, threads, YCSB_wl, YCSB_size):
	if workload == 'TPCC':
		jobs[alg + '_' + workload + '_' + threads + 't'] = {
			"WORKLOAD"			: workload,
			"THREAD_CNT"		: threads,
			"NUM_WH"			: threads,
			"INDEX_STRUCT"		: alg
		}
	else: # workload == 'YCSB'
		jobs[alg + '_' + workload + '_' + YCSB_wl + '_' + YCSB_size + '_' + threads + 't'] = {
			"WORKLOAD"			: workload,
			"THREAD_CNT"		: threads,
			"INDEX_STRUCT"		: alg,
			"SYNTH_TABLE_SIZE" 	: YCSB_size
		}
		if YCSB_wl == 'A':
			jobs[alg + '_' + workload + '_' + YCSB_wl + '_' + YCSB_size + '_' + threads + 't']["READ_PERC"] = '0.5'
			jobs[alg + '_' + workload + '_' + YCSB_wl + '_' + YCSB_size + '_' + threads + 't']["WRITE_PERC"] = '0.5'
		if YCSB_wl == 'B':
			jobs[alg + '_' + workload + '_' + YCSB_wl + '_' + YCSB_size + '_' + threads + 't']["READ_PERC"] = '0.95'
			jobs[alg + '_' + workload + '_' + YCSB_wl + '_' + YCSB_size + '_' + threads + 't']["WRITE_PERC"] = '0.05'
		if YCSB_wl == 'C':
			jobs[alg + '_' + workload + '_' + YCSB_wl + '_' + YCSB_size + '_' + threads + 't']["READ_PERC"] = '1'
			jobs[alg + '_' + workload + '_' + YCSB_wl + '_' + YCSB_size + '_' + threads + 't']["WRITE_PERC"] = '0'



def test_compile(jobname, job):
	os.system("cp "+ dbms_cfg[0] +' ' + dbms_cfg[1])
	for param, value in job.items():
		pattern = r"\#define\s*" + re.escape(param) + r'.*'
		replacement = "#define " + param + ' ' + str(value)
		replace(dbms_cfg[1], pattern, replacement)
	os.system("make clean > temp.out 2>&1")
	ret = os.system("make -j > temp.out 2>&1")
	if ret != 0:
		print("ERROR in compiling job=")
		print(job)
		exit(0)
	os.system(f"cp rundb run_{jobname}")
	print(f"PASS Compile {jobname}")


def compile_all_test(jobs) :
	for jobname, job in jobs.items():
		test_compile(jobname, job)

# run YCSB tests
jobs = {}
for alg in algs: 
	for work in workloads:
		for t_num in threads:
			for sub_wl in YCSB_workloads:
				for size in YCSB_sizes:
					insert_job(alg, work, t_num, sub_wl, size)
compile_all_test(jobs)


os.system('cp config-std.h config.h')
os.system('make clean > temp.out 2>&1')
os.system('rm temp.out')
