#pragma once
#include "cud/interface.h"

// ── CPU settings ──────────────────────────────────────────────────────────────

// Number of OpenMP threads for the CPU baseline.
#define BENCH_CPU_THREADS 4

// CPU processes (BENCH_CPU_SCALE x N_ELEM) elements; CUD processes N_ELEM.
// First N_ELEM elements are shared with CUD for correctness comparison.
#define BENCH_CPU_SCALE 16

// ─────────────────────────────────────────────────────────────────────────────

void run_benchmark(CxlMem& mem, CxlIo& io);
