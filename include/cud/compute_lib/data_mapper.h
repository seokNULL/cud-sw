#pragma once
#include <cstdint>

// Bit-serial layout: W-bit integers for N elements, stored as W rows × N columns.
//   Row (base_row + b) holds bit-plane b, where b=0 is LSB, b=bit_width-1 is MSB.
//   Column index corresponds directly to element index (element 0 → col 0).
//
// Constraint: all W rows must reside in the same bank and the same mat.
//   With bit_width ≤ 8 and mat size ≥ 1088, this is satisfied whenever
//   base_row and base_row+7 fall within the same mat boundary.
struct BitSerialLayout {
    uint32_t bank;
    uint32_t base_row;   // row address of bit-plane 0 (LSB)
    uint32_t bit_width;  // number of bit-planes: 1-8
    uint32_t num_elem;   // number of elements: 1-NUM_COL

    uint32_t plane_row(uint32_t bit) const { return base_row + bit; }
    uint32_t top_row()               const { return base_row + bit_width - 1; }

    // Physical address of the start of bit-plane `bit` (col defaults to 0).
    uint64_t plane_pa(uint32_t bit, uint32_t col = 0) const;
};

// True if all bit-planes of l are within the same mat.
bool layout_valid(const BitSerialLayout& l);

// True if a and b share the same bank and all their rows lie in the same mat.
// This is required for any ROWCOPY or MAJ3 that touches rows from both layouts.
bool layouts_compatible(const BitSerialLayout& a, const BitSerialLayout& b);
