#pragma once
#include <cstdint>
#include <cassert>

// Row map:
//   0 - 100   user data: inputs and results
//   101 - 900 compute zone: MAJ3 groups + scratch temporaries  (this file)
//
// All rows 0-900 fall within Mat 0 (boundary at row 1184), so ROWCOPY and
// MAJ3 instructions can freely operate across the data and compute zones.
//
// Layout within compute zone:
//   112-121  Mode-0 MAJ3 compute group used by inst_gen.
//              base=112 (0b1110000): bit0=0, bit3=0 ✓
//              group rows: {112, 113, 120, 121}
//   122-900  General-purpose scratch; bump-allocated per operation.

static constexpr uint32_t kScratchBase    = 101u;
static constexpr uint32_t kScratchEnd     = 901u;  // exclusive

// Mode-0 compute group base for instruction generators.
// Satisfies mode-0 constraint (bits 0 and 3 of base are 0):
//   112 = 0b1110000  →  group {112, 113, 120, 121}
static constexpr uint32_t kInstGenCmpBase = 112u;

// First scratch row available for temporaries (after the compute group).
static constexpr uint32_t kInstGenScratch = 122u;

// Bump allocator for temporary rows within [kInstGenScratch, kScratchEnd).
// Reset between operations. Not thread-safe.
struct ScratchAllocator {
    uint32_t bank;
    uint32_t next;

    explicit ScratchAllocator(uint32_t bank) : bank(bank), next(kInstGenScratch) {}

    // Returns base row of num_rows consecutive scratch rows.
    uint32_t alloc(uint32_t num_rows) {
        const uint32_t base = next;
        next += num_rows;
        assert(next <= kScratchEnd && "scratch zone exhausted");
        return base;
    }

    void reset()                   { next = kInstGenScratch; }
    uint32_t remaining()     const { return kScratchEnd - next; }
};
