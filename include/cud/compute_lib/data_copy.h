#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "../instruction.h"

// Row-to-row data copy kernel.
//
// Constraints:
//   - size_bytes must be a non-zero multiple of CUD_ROW_SIZE_BYTES (8 KiB)
//   - src_pa and dst_pa must be in the same DRAM bank
//
// Returns the complete instruction list (including END) for CudExecute().
std::vector<CudInst> cud_data_copy(uint64_t src_pa, uint64_t dst_pa,
                                    size_t   size_bytes);
