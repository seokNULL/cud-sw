#pragma once
#include <cstdint>
#include <cassert>
#include "cxl/address_map.h"

// Row map (offsets within a mat):
//   0 - 100   user data: inputs and results
//   101 - 900 compute zone: MAJ3 groups + scratch temporaries  (this file)
//
// Within compute zone:
//   112-121  Mode-0 MAJ3 compute group used by inst_gen.
//              base=112 (0b1110000): bit0=0, bit3=0 ✓
//              group rows: {base+0, base+1, base+8, base+9}
//   122-900  General-purpose scratch; bump-allocated per operation.
//
// mat_to_row_start(mat) is always a multiple of 1184, so its low bits are 0.
// Adding offset 112 (= 0b01110000) keeps bit0=0 and bit3=0, satisfying mode-0.

static constexpr uint32_t kScratchBase    = 101u;
static constexpr uint32_t kScratchEnd     = 901u;  // exclusive

// Constant-row offsets within a mat.
static constexpr uint32_t kZeroRow        = 101u;  // all bits = 0
static constexpr uint32_t kOnesRow        = 102u;  // all bits = 1

// Mode-0 MAJ3 compute group base offset for instruction generators.
static constexpr uint32_t kInstGenCmpBase = 112u;

// General scratch starts here (offset 122, after compute group rows 112-121).
static constexpr uint32_t kInstGenScratch = 122u;

// Bump allocator for temporary rows within a mat's compute zone.
// All row offsets are relative to mat_to_row_start(mat).
// Call abs_row(offset) to get the absolute row address for CXL/CUD operations.
struct ScratchAllocator {
    uint32_t bank;
    uint32_t mat;
    uint32_t next;  // next free offset within the mat

    ScratchAllocator(uint32_t bank, uint32_t mat)
        : bank(bank), mat(mat), next(kInstGenScratch) {}

    // Absolute row address for a within-mat offset.
    uint32_t abs_row(uint32_t offset) const {
        return mat_to_row_start(mat) + offset;
    }

    // Allocate num_rows consecutive scratch rows; returns base offset.
    uint32_t alloc(uint32_t num_rows) {
        const uint32_t base = next;
        next += num_rows;
        assert(next <= kScratchEnd && "scratch zone exhausted");
        return base;
    }

    void reset()               { next = kInstGenScratch; }
    uint32_t remaining() const { return kScratchEnd - next; }
};
