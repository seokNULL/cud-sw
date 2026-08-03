#pragma once
#include "../../../include/cud/compute_lib/inst_gen.h"
#include "../../../include/cud/compute_lib/scratch.h"
#include <cstdint>
#include <vector>

// ── Shared 6-group full-adder primitive ──────────────────────────────────────
//
// Used by mul.cpp (Wallace-tree CPA) and gemv.cpp (accumulate) — both need a
// ripple-carry step that produces sum AND its complement (so the result can
// feed straight into another gen_fa6/gen_mul call without a NOT round-trip).

// Bit-plane wire: absolute DRAM rows for a value and its complement.
struct Wire { uint32_t row, nrow; };

uint64_t abs_pa(uint32_t bank, uint32_t abs_row);

// Allocate two consecutive scratch rows and return as a Wire.
Wire alloc_wire(ScratchAllocator& sc);

// Wire representing the constant 0 / 1 (pre-initialised constant rows).
Wire zero_wire(const ScratchAllocator& sc);

// End of the fixed 6-group FA compute area (6 groups x stride 16, from
// kInstGenCmpBase=112); callers reserve scratch up to here before using
// gen_fa6, since the groups live inside the normal scratch address range.
static constexpr uint32_t kMGroupEnd = kInstGenCmpBase + 6u * 16u; // 208

// Produces sum = XOR(a,b,cin) and carry = MAJ3(a,b,cin), plus their
// complements, written into the caller-supplied c_wire / s_wire rows.
// s_wire (or c_wire) may alias an input wire for an in-place update: the
// pre-load phase copies inputs into the compute-group rows before the
// execute phase writes the outputs, so overwriting an input's rows with the
// result afterward is safe.  47 instructions.
void gen_fa6(std::vector<CudInst>& v, const ScratchAllocator& sc,
             Wire a, Wire b, Wire cin, Wire c_wire, Wire s_wire);
