#pragma once
#include "cud/interface.h"

// Number of OpenMP threads used for the CPU benchmark.
#define BENCH_CPU_THREADS 4

// CPU processes (BENCH_CPU_SCALE x N_ELEM) elements; CUD processes N_ELEM.
// The first N_ELEM elements are shared between both for result comparison.
#define BENCH_CPU_SCALE 16

// CPU vs CUD performance benchmark for XOR / ADD / MUL (W = 1, 2, 4, 8).
// Breakdown: CXL.mem write | inst-gen | CXL.io exec | CXL.mem read
void run_benchmark(CxlMem& mem, CxlIo& io);
