#pragma once
#include <cstdint>

struct CudTestConfig {
    // CXL.mem row assignments
    uint32_t bank        = 0;
    uint32_t src_row     = 0;   // DataCopy source
    uint32_t dst_row     = 1;   // DataCopy destination
    uint32_t row_a       = 0;   // Logical operand a
    uint32_t row_b       = 1;   // Logical operand b
    uint32_t row_bias    = 2;   // Logical bias (zero for AND, one for OR)
    uint32_t row_dst     = 3;   // Logical result
    uint32_t row_zero    = 4;   // Dedicated zero row for OR frac-row init

    // Input patterns
    uint64_t copy_pattern = 0xDEADBEEFCAFEBABEULL;
    uint64_t pattern_a    = 0xAAAAAAAAAAAAAAAAULL;
    uint64_t pattern_b    = 0xCCCCCCCCCCCCCCCCULL;

    // CXL.io register offsets
    uint64_t inst_base   = 0x0000;
    uint64_t status_reg  = 0x0000;
    uint32_t done_mask   = 0x1;
};
