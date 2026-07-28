#pragma once
#include "cud/instruction.h"
#include "cud/compute_lib/data_mapper.h"
#include "cud/compute_lib/scratch.h"
#include "cud/compute_lib/add_table.h"
#include <cstdint>
#include <vector>

// ── Bit-serial instruction generators ────────────────────────────────────────
//
// Each function returns a std::vector<CudInst> (including the final END).
// Callers must:
//   1. Pre-initialize kZeroRow and kOnesRow in the target bank before any call.
//   2. Ensure all layouts and scratch rows are in the same bank and same mat.
//   3. Reset or advance the ScratchAllocator between operations as needed.
//
// NOT is not supported in hardware; the CPU must pre-compute complements and
// write them to DRAM before calling any generator that requires them.

// XOR: out[i] = a[i] ^ b[i]  for each bit-plane i in [0, bit_width)
//
// Algorithm: XOR = OR(AND(a, ~b), AND(~a, b))
//   per bit-plane: t1 = AND(a[i], not_b[i])
//                  t2 = AND(not_a[i], b[i])
//                  out[i] = OR(t1, t2)
//
// not_a and not_b must already be written to DRAM by the CPU.
// Uses 2 scratch rows (t1, t2) reused across bit-planes.
std::vector<CudInst> gen_xor(
    const BitSerialLayout& a,
    const BitSerialLayout& not_a,
    const BitSerialLayout& b,
    const BitSerialLayout& not_b,
    const BitSerialLayout& out,
    ScratchAllocator& scratch);

// ADD: out[k] = bit k of (a + b)  for k in [0, W]  (W+1 output bit-planes)
//
// Algorithm: two-level SOP from kAdderSop[W-1].  W in [1..8].
// CPU must pre-compute not_a and not_b and write all four to DRAM.
// Uses 2 scratch rows (acc + tmp) reused across all output bits.
// out.bit_width must equal W+1.
std::vector<CudInst> gen_add(
    const BitSerialLayout& a,
    const BitSerialLayout& not_a,
    const BitSerialLayout& b,
    const BitSerialLayout& not_b,
    const BitSerialLayout& out,
    uint8_t W,
    ScratchAllocator& scratch);
