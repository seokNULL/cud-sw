#pragma once
#include "cud/interface.h"

// ── CPU settings ──────────────────────────────────────────────────────────────

// Number of OpenMP threads for the CPU baseline.
#define BENCH_CPU_THREADS 4

// CPU processes (BENCH_CPU_SCALE x N_ELEM) elements; CUD processes N_ELEM per tile.
// First N_ELEM elements are shared with CUD for correctness comparison.
#define BENCH_CPU_SCALE 16

// ── Tile settings ─────────────────────────────────────────────────────────────

// Maximum tile count to test; benchmark iterates {1, 2, 4, ..., BENCH_N_TILES}.
// CUD instruction buffer is 1024; bulk mode is skipped for cases that exceed it.
#define BENCH_N_TILES 8

// ── Execution mode ────────────────────────────────────────────────────────────

// Set to 1 to enable sequential mode (per-tile: write → exec → read).
#define BENCH_RUN_SEQ  1

// Set to 1 to enable bulk mode (all write → one exec → all read).
#define BENCH_RUN_BULK 1

// ─────────────────────────────────────────────────────────────────────────────

void run_benchmark(CxlMem& mem, CxlIo& io);
