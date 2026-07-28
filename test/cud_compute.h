#pragma once
#include "cud/interface.h"
#include "test_config.h"

// Instruction-level: ROWCOPY_SRC + ROWCOPY_DST + END via internal helpers
void run_rowcopy_test(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg);

// Instruction-level: single MAJ3 instruction across all 7 modes (AND + OR per mode)
void run_maj3_test(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg);

// Logical-level: AND and OR via the CudAnd / CudOr public API
void run_logical_tests(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg);

// Logical-level: XOR via gen_xor (2×AND + OR per bit-plane, CPU pre-computes ~a, ~b)
void run_xor_test(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg);

// Logical-level: W-bit addition via gen_add (Ripple Carry Adder, W=1..8)
void run_add_test(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg);
