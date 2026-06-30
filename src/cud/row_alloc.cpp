#include "internal.h"
#include "../../include/cud/types.h"
#include <algorithm>

uint32_t g_const_one_row = 1183;
uint32_t g_const_zero_row = 1182;

namespace cud_internal {

const std::set<uint32_t>* g_extra_reserved = nullptr;

const std::vector<size_t>& two_ra_groups() {
    static const std::vector<size_t> kIndices = {0, 1, 2, 3, 4, 5, 6};
    return kIndices;
}

const std::vector<size_t>& three_ra_groups() {
    static const std::vector<size_t> kIndices = {7, 8, 9, 10, 11, 12, 13, 14};
    return kIndices;
}

void mark_used_row(std::set<uint32_t>& used, uint32_t global_row) {
    if (global_row <= kMaxRowAddr) {
        used.insert(global_row);
    }
}

void mark_used_addr(std::set<uint32_t>& used, uint64_t addr) {
    mark_used_row(used, extract_row(addr));
}

void mark_used_addr_and_mirror(std::set<uint32_t>& used, uint64_t addr) {
    uint32_t global_row = extract_row(addr);
    mark_used_row(used, global_row);
    if (global_row + ROWS_PER_MAT <= kMaxRowAddr) {
        mark_used_row(used, global_row + ROWS_PER_MAT);
    }
}

std::optional<std::vector<uint64_t>> alloc_group_rows(
    uint32_t bank,
    uint32_t mat_id,
    const std::vector<size_t>& group_candidates,
    std::set<uint32_t>& used_rows
) {
    for (size_t group_index : group_candidates) {
        if (group_index >= SUPPORTED_GROUPS.size()) continue;
        const auto& offsets = SUPPORTED_GROUPS[group_index].offsets;
        if (offsets.empty()) continue;
        uint32_t max_offset = offsets.back();
        if (max_offset >= ROWS_PER_MAT) continue;

        for (uint32_t base_local = 0; base_local + max_offset < ROWS_PER_MAT; ++base_local) {
            bool free = true;
            std::vector<uint32_t> candidate_rows;
            candidate_rows.reserve(offsets.size());

            for (uint32_t offset : offsets) {
                uint32_t global_row = global_row_from_mat(mat_id, base_local + offset);
                if (used_rows.count(global_row)) { free = false; break; }
                candidate_rows.push_back(global_row);
            }

            if (!free) continue;

            std::vector<uint64_t> addrs;
            addrs.reserve(candidate_rows.size());
            for (uint32_t global_row : candidate_rows) {
                used_rows.insert(global_row);
                addrs.push_back(make_row_addr(bank, global_row));
            }
            return addrs;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<uint64_t>> alloc_group_rows_anchored(
    uint32_t bank,
    uint32_t mat_id,
    const std::vector<size_t>& group_candidates,
    uint32_t anchor_global_row,
    std::set<uint32_t>& used_rows
) {
    if (mat_id_from_row(anchor_global_row) != mat_id) return std::nullopt;

    for (size_t group_index : group_candidates) {
        if (group_index >= SUPPORTED_GROUPS.size()) continue;
        const auto& offsets = SUPPORTED_GROUPS[group_index].offsets;
        if (offsets.empty()) continue;
        uint32_t max_offset = offsets.back();
        if (max_offset >= ROWS_PER_MAT) continue;

        for (uint32_t base_local = 0; base_local + max_offset < ROWS_PER_MAT; ++base_local) {
            bool contains_anchor = false;
            bool free = true;
            std::vector<uint32_t> candidate_rows;
            candidate_rows.reserve(offsets.size());

            for (uint32_t offset : offsets) {
                uint32_t global_row = global_row_from_mat(mat_id, base_local + offset);
                if (global_row == anchor_global_row) {
                    contains_anchor = true;
                } else if (used_rows.count(global_row)) {
                    free = false;
                    break;
                }
                candidate_rows.push_back(global_row);
            }

            if (!free || !contains_anchor) continue;

            std::vector<uint64_t> addrs;
            addrs.reserve(candidate_rows.size());
            for (uint32_t global_row : candidate_rows) {
                if (global_row != anchor_global_row) {
                    used_rows.insert(global_row);
                }
                addrs.push_back(make_row_addr(bank, global_row));
            }
            return addrs;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<uint32_t>> alloc_mirrored_rows(
    uint32_t mat0_id,
    uint32_t mat1_id,
    size_t needed_count,
    std::set<uint32_t>& used_rows
) {
    std::vector<uint32_t> locals;
    locals.reserve(needed_count);

    while (locals.size() < needed_count) {
        bool found_group = false;

        for (size_t group_index : three_ra_groups()) {
            if (group_index >= SUPPORTED_GROUPS.size()) continue;
            const auto& offsets = SUPPORTED_GROUPS[group_index].offsets;
            if (offsets.empty()) continue;
            uint32_t max_offset = offsets.back();
            if (max_offset >= ROWS_PER_MAT) continue;

            for (uint32_t base_local = 0; base_local + max_offset < ROWS_PER_MAT; ++base_local) {
                bool free = true;
                std::vector<uint32_t> candidate_locals;
                candidate_locals.reserve(offsets.size());

                for (uint32_t offset : offsets) {
                    uint32_t local_row = base_local + offset;
                    uint32_t row_m0 = global_row_from_mat(mat0_id, local_row);
                    uint32_t row_m1 = global_row_from_mat(mat1_id, local_row);
                    if (used_rows.count(row_m0) || used_rows.count(row_m1)) {
                        free = false;
                        break;
                    }
                    candidate_locals.push_back(local_row);
                }

                if (!free) continue;

                for (uint32_t local_row : candidate_locals) {
                    used_rows.insert(global_row_from_mat(mat0_id, local_row));
                    used_rows.insert(global_row_from_mat(mat1_id, local_row));
                    locals.push_back(local_row);
                }
                found_group = true;
                break;
            }
            if (found_group) break;
        }
        if (!found_group) return std::nullopt;
    }

    locals.resize(needed_count);
    return locals;
}

} // namespace cud_internal
