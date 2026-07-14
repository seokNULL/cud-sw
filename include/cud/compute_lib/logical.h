#pragma once
#include <cstdint>
#include <vector>
#include "../instruction.h"

// Bitwise AND via MAJ3: dst = a AND b = MAJ3(a, b, 0)
//
// Inputs:
//   a_pa, b_pa  — source operand rows (must be in the same bank)
//   zero_pa     — a row pre-filled with all zeros (same bank)
//   dst_pa      — result destination row (same bank)
//
// Internally allocates three fixed compute rows within the bank.
// Returns the complete instruction list for CudExecute().
std::vector<CudInst> cud_and(uint64_t a_pa, uint64_t b_pa,
                              uint64_t zero_pa, uint64_t dst_pa);

// Bitwise OR via MAJ3: dst = a OR b = MAJ3(a, b, 1)
//
// Inputs:
//   a_pa, b_pa  — source operand rows (must be in the same bank)
//   one_pa      — a row pre-filled with all ones (0xFF...FF) (same bank)
//   dst_pa      — result destination row (same bank)
std::vector<CudInst> cud_or(uint64_t a_pa, uint64_t b_pa,
                             uint64_t one_pa, uint64_t dst_pa);
