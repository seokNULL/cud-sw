#pragma once
#include "cud/instruction.h"
#include "cud/compute_lib/data_mapper.h"
#include "cud/compute_lib/scratch.h"
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
// Algorithm: Ripple Carry Adder via MAJ3 decomposition.  W in [1..8].
//   sum = MAJ3(a, MAJ3(b, cin, ~carry), ~carry);  carry = MAJ3(a, b, cin)
//   Uses 4 fixed mode-0 compute groups at mat offsets [112, 176).
//   Each bit step: 34 insts.
//   Total: 34*W + 3  (W=1: 37  W=4: 139  W=8: 275)
// CPU must pre-compute not_a and not_b and write all four to DRAM.
// Uses 2 scratch rows (carry, ~carry) reused across all output bits.
// out.bit_width must equal W+1.
std::vector<CudInst> gen_add(
    const BitSerialLayout& a,
    const BitSerialLayout& not_a,
    const BitSerialLayout& b,
    const BitSerialLayout& not_b,
    const BitSerialLayout& out,
    uint8_t W,
    ScratchAllocator& scratch);

// MUL: out[k] = bit k of (a * b)  for k in [0, 2W)  (2W output bit-planes)
//
// Algorithm: Wallace tree (greedy left-to-right column reduction) + final CPA.
//   Partial products: W² AND gates, each 22 insts.
//   Reduction FAs: 6-group FA producing (sum, ~sum, carry, ~carry), 47 insts each.
//   Final CPA: 2W × 6-group FA, 47 insts per bit.
//   Instruction count: W²×22 + (Wallace FAs)×47 + 2W×47 + 2W×2 + 1
//   Approx: W=4: ~1700 insts   W=8: ~6100 insts
// CPU must pre-compute not_a and not_b and write all four to DRAM.
// out.bit_width must equal 2*W.
std::vector<CudInst> gen_mul(
    const BitSerialLayout& a,
    const BitSerialLayout& not_a,
    const BitSerialLayout& b,
    const BitSerialLayout& not_b,
    const BitSerialLayout& out,
    uint8_t W,
    ScratchAllocator& scratch);
