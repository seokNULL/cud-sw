#pragma once
// Internal header: shared only among src/cud/*.cpp files.

#include "../../include/cud/types.h"
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace cud_internal {

constexpr uint32_t kMaxRowAddr = (1u << 17) - 1;

// Pointer set by ScopedExtraReserved to prevent ADD_new scratch from
// clobbering outer caller state rows.
extern const std::set<uint32_t>* g_extra_reserved;

const std::vector<size_t>& two_ra_groups();
const std::vector<size_t>& three_ra_groups();

void mark_used_row(std::set<uint32_t>& used, uint32_t global_row);
void mark_used_addr(std::set<uint32_t>& used, uint64_t addr);
void mark_used_addr_and_mirror(std::set<uint32_t>& used, uint64_t addr);

std::optional<std::vector<uint64_t>> alloc_group_rows(
    uint32_t bank,
    uint32_t mat_id,
    const std::vector<size_t>& group_candidates,
    std::set<uint32_t>& used_rows
);

std::optional<std::vector<uint64_t>> alloc_group_rows_anchored(
    uint32_t bank,
    uint32_t mat_id,
    const std::vector<size_t>& group_candidates,
    uint32_t anchor_global_row,
    std::set<uint32_t>& used_rows
);

std::optional<std::vector<uint32_t>> alloc_mirrored_rows(
    uint32_t mat0_id,
    uint32_t mat1_id,
    size_t needed_count,
    std::set<uint32_t>& used_rows
);

// RAII guard: installs extra_rows pointer so nested allocators avoid those rows.
class ScopedExtraReserved {
public:
    explicit ScopedExtraReserved(const std::set<uint32_t>* extra_rows)
        : prev_(g_extra_reserved) {
        g_extra_reserved = extra_rows;
    }
    ~ScopedExtraReserved() { g_extra_reserved = prev_; }

private:
    const std::set<uint32_t>* prev_;
};

} // namespace cud_internal

// Declared here so both ops_logic.cpp (definition) and ops_mult.cpp (caller) can see it.
std::vector<uint32_t> maj5_via_maj3_sum_inplace(
    uint64_t src1, uint64_t src2, uint64_t src3, uint64_t src4, uint64_t src5);
