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

// AND: out[i] = a[i] & b[i]  for each bit-plane i in [0, bit_width)
//
// Uses MAJ3(a, b, 0) per bit-plane.  11 insts/plane + 1 END.
// Total: 11*W + 1  (W=1: 12  W=4: 45  W=8: 89)
// No complement inputs required.
std::vector<CudInst> gen_and(
    const BitSerialLayout& a,
    const BitSerialLayout& b,
    const BitSerialLayout& out,
    ScratchAllocator& scratch);

// OR: out[i] = a[i] | b[i]  for each bit-plane i in [0, bit_width)
//
// Uses MAJ3(a, b, 1) per bit-plane.  11 insts/plane + 1 END.
// Total: 11*W + 1  (W=1: 12  W=4: 45  W=8: 89)
// No complement inputs required.
std::vector<CudInst> gen_or(
    const BitSerialLayout& a,
    const BitSerialLayout& b,
    const BitSerialLayout& out,
    ScratchAllocator& scratch);

// XOR: out[i] = a[i] ^ b[i]  for each bit-plane i in [0, bit_width)
//
// Algorithm: XOR = OR(AND(a, ~b), AND(~a, b))
//   per bit-plane: t1 = AND(a[i], not_b[i])
//                  t2 = AND(not_a[i], b[i])
//                  out[i] = OR(t1, t2)
//
// not_a and not_b must already be written to DRAM by the CPU.
// Uses 2 scratch rows (t1, t2) reused across bit-planes.
// Total: 33*W + 1  (W=1: 34  W=4: 133  W=8: 265)
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
//   W=1: AND(a,b) fast path — 12 insts.
//   W>1: partial products W²×22 insts + Wallace FAs×47 + CPA with trivial-case
//        optimisation (FA(a,0,0) and FA(0,0,c) replaced by rowcopy).
//   Measured: W=2: 195  W=4: 937  W=8: 4077
// CPU must pre-compute not_a and not_b and write all four to DRAM.
// out.bit_width must equal 2*W (or ≥1 for W=1).
//
// If not_out is non-null, the product's complement is also written to it
// (out.bit_width each) — every CPA output bit already has its complement
// available internally (gen_fa6 always produces sum and ~sum together), so
// this costs one extra rowcopy pair per bit and no new arithmetic. Lets a
// caller (e.g. gen_gemv) chain this product straight into gen_add without an
// extra NOT step.
std::vector<CudInst> gen_mul(
    const BitSerialLayout& a,
    const BitSerialLayout& not_a,
    const BitSerialLayout& b,
    const BitSerialLayout& not_b,
    const BitSerialLayout& out,
    uint8_t W,
    ScratchAllocator& scratch,
    const BitSerialLayout* not_out = nullptr);

// GEMV: out = sum_{i=0}^{len-1} a[i] * b[i]   (dot product / MAC reduction)
//
// a[i]: W-bit scalar, broadcast to the same value in every lane (element).
// b[i]: W-bit vector, one value per lane (a row of the matrix).
// out:  running accumulator and final result, fixed at 2*W bits — bits beyond
//       2*W are dropped each add, so pick len so the true sum fits in 2*W
//       bits (or accept truncation as a benchmark approximation).
//
// i=0 is a single gen_mul directly into out (with not_out); each subsequent
// i is gen_mul into a scratch product, folded into out with a ripple-carry
// accumulate (same per-bit full-adder as gen_mul's own CPA — sum and ~sum
// together, so no NOT round-trip is ever needed). No new arithmetic
// primitive, just gen_mul chained into itself. Scratch is rewound between
// iterations, so cost does not grow with len.
std::vector<CudInst> gen_gemv(
    const std::vector<BitSerialLayout>& a,
    const std::vector<BitSerialLayout>& not_a,
    const std::vector<BitSerialLayout>& b,
    const std::vector<BitSerialLayout>& not_b,
    const BitSerialLayout& prod,
    const BitSerialLayout& not_prod,
    const BitSerialLayout& out,
    const BitSerialLayout& not_out,
    uint8_t W,
    ScratchAllocator& scratch);
