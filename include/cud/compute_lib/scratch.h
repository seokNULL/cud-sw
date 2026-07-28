#pragma once
#include <cstdint>
#include <cassert>

// Scratch zone: rows 300-500 (inclusive) within a bank.
//
// All rows in this range fall within Mat 0 (rows 0-1183), so ROWCOPY and MAJ3
// instructions can freely interoperate with user data rows that also live in
// Mat 0 (e.g. rows 0-299).
//
// Layout:
//   304-313  Mode-0 MAJ3 compute group used by inst_gen.
//              base=304 (0b100110000): bit0=0, bit3=0 ✓
//              group rows: {304, 305, 312, 313}
//   314-500  General-purpose scratch; bump-allocated per operation.

static constexpr uint32_t kScratchBase    = 300u;
static constexpr uint32_t kScratchEnd     = 501u;  // exclusive

// Mode-0 compute group base for instruction generators.
// Satisfies mode-0 constraint: bits 0 and 3 of base are 0.
//   304 = 0b100110000  →  group {304, 305, 312, 313}
static constexpr uint32_t kInstGenCmpBase = 304u;

// First scratch row available for temporaries (after the compute group).
static constexpr uint32_t kInstGenScratch = 314u;

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
