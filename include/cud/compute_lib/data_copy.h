#pragma once
#include <cstdint>
#include <vector>
#include "../instruction.h"

// Single-row ROWCOPY primitive: returns [ROWCOPY_SRC(src_pa), ROWCOPY_DST(dst_pa)].
// No validation, no END. Use CudDataCopy() for the validated multi-row variant.
std::vector<CudInst> cud_data_copy(uint64_t src_pa, uint64_t dst_pa);
