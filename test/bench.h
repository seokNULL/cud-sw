#pragma once
#include "cud/interface.h"

// ── CPU settings ──────────────────────────────────────────────────────────────

// Number of OpenMP threads for the CPU baseline.
#define BENCH_CPU_THREADS 4

// CPU processes (BENCH_CPU_SCALE x N_ELEM) elements; CUD processes N_ELEM.
// First N_ELEM elements are shared with CUD for correctness comparison.
#define BENCH_CPU_SCALE 16

// ── GEMV settings ────────────────────────────────────────────────────────────

// Dot-product length 'a' for the GEMV benchmark: out = sum_{i<a} A[i]*B[i],
// where A[i] is an 8-bit scalar (broadcast to every lane) and B[i] is an
// 8-bit vector (one value per lane). All 'a' pairs must be resident in DRAM
// at once for a single fused instruction stream, so this is capped by the
// row space bench.cpp reserves for GEMV (currently supports up to ~6).
#define BENCH_GEMV_LEN 4

// ─────────────────────────────────────────────────────────────────────────────

void run_benchmark(CxlMem& mem, CxlIo& io);
