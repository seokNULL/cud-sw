#include "internal.h"
#include "../../include/cud/ops.h"
#include "../../include/cud/types.h"
#include <algorithm>
#include <iostream>

std::vector<uint32_t> maj3(uint64_t src1, uint64_t src2, uint64_t src3) {
    std::vector<uint32_t> inst_list;

    uint32_t src1_bank = extract_bank(src1);
    uint32_t src2_bank = extract_bank(src2);
    uint32_t src3_bank = extract_bank(src3);

    if (src1_bank != src2_bank || src2_bank != src3_bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within maj3" << std::endl;
        return inst_list;
    }

    uint32_t src1_row = extract_row(src1);
    uint32_t src2_row = extract_row(src2);
    uint32_t src3_row = extract_row(src3);

    uint32_t mat1 = mat_id_from_row(src1_row);
    uint32_t mat2 = mat_id_from_row(src2_row);
    uint32_t mat3 = mat_id_from_row(src3_row);

    if (mat1 != mat2 || mat2 != mat3) {
        std::cout << "[ERROR]: maj3 rows cross MAT boundary" << std::endl;
        return inst_list;
    }

    uint32_t lrow1 = local_row_from_global(src1_row);
    uint32_t lrow2 = local_row_from_global(src2_row);
    uint32_t lrow3 = local_row_from_global(src3_row);

    RowAnalysisResult result = analyze_rows({lrow1, lrow2, lrow3});
    if (!result.valid) {
        std::cout << "[ERROR]: NO_RA_PAIR within maj3" << std::endl;
        return inst_list;
    }

    uint32_t local_base_row = result.base_row;
    uint32_t global_base_row = global_row_from_mat(mat1, local_base_row);
    uint32_t ra_group_index = result.ra_group_index;

    if (ra_group_index > 6) {
        std::cout << "[ERROR]: maj3 matched non-2RA group" << std::endl;
        return inst_list;
    }

    uint32_t dont_care = dont_care_pos_maj3(ra_group_index);
    const auto& group_offsets = SUPPORTED_GROUPS[ra_group_index].offsets;
    uint32_t max_offset = group_offsets.back();

    if (local_base_row + max_offset >= ROWS_PER_MAT) {
        std::cout << "[ERROR]: maj3 group spills outside MAT boundary" << std::endl;
        return inst_list;
    }

    std::vector<uint32_t> free_offsets;
    for (auto off : group_offsets) {
        if (std::find(result.frac_rows.begin(), result.frac_rows.end(), off) == result.frac_rows.end()) {
            free_offsets.push_back(off);
        }
    }

    if (free_offsets.size() != 1) {
        std::cout << "[ERROR]: maj3 expects exactly one free row" << std::endl;
        return inst_list;
    }

    uint32_t out_offset = free_offsets[0];
    uint32_t frac_pos = static_cast<uint32_t>(std::distance(
        group_offsets.begin(),
        std::find(group_offsets.begin(), group_offsets.end(), out_offset)
    ));

    inst_list.push_back(
        encode_uop(AddrMode::MAJ3, src1_bank, global_base_row, 0, frac_pos, 0, dont_care)
    );
    return inst_list;
}

std::vector<uint32_t> maj5(uint64_t src1, uint64_t src2, uint64_t src3,
                            uint64_t src4, uint64_t src5) {
    std::vector<uint32_t> inst_list;

    uint32_t src1_bank = extract_bank(src1);

    if (extract_bank(src2) != src1_bank || extract_bank(src3) != src1_bank ||
        extract_bank(src4) != src1_bank || extract_bank(src5) != src1_bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within maj5" << std::endl;
        return inst_list;
    }

    uint32_t src1_row = extract_row(src1);
    uint32_t src2_row = extract_row(src2);
    uint32_t src3_row = extract_row(src3);
    uint32_t src4_row = extract_row(src4);
    uint32_t src5_row = extract_row(src5);

    uint32_t mat1 = mat_id_from_row(src1_row);

    if (mat_id_from_row(src2_row) != mat1 || mat_id_from_row(src3_row) != mat1 ||
        mat_id_from_row(src4_row) != mat1 || mat_id_from_row(src5_row) != mat1) {
        std::cout << "[ERROR]: maj5 rows cross MAT boundary" << std::endl;
        return inst_list;
    }

    uint32_t lrow1 = local_row_from_global(src1_row);
    uint32_t lrow2 = local_row_from_global(src2_row);
    uint32_t lrow3 = local_row_from_global(src3_row);
    uint32_t lrow4 = local_row_from_global(src4_row);
    uint32_t lrow5 = local_row_from_global(src5_row);

    RowAnalysisResult result = analyze_rows({lrow1, lrow2, lrow3, lrow4, lrow5});
    if (!result.valid) {
        std::cout << "[ERROR]: NO_RA_PAIR within maj5" << std::endl;
        return inst_list;
    }

    uint32_t local_base_row = result.base_row;
    uint32_t global_base_row = global_row_from_mat(mat1, local_base_row);
    uint32_t ra_group_index = result.ra_group_index;

    if (ra_group_index < 7) {
        std::cout << "[ERROR]: maj5 matched non-3RA group" << std::endl;
        return inst_list;
    }

    uint32_t dont_care = dont_care_pos_maj5(ra_group_index);
    const auto& group_offsets = SUPPORTED_GROUPS[ra_group_index].offsets;
    uint32_t max_offset = group_offsets.back();

    if (local_base_row + max_offset >= ROWS_PER_MAT) {
        std::cout << "[ERROR]: maj5 group spills outside MAT boundary" << std::endl;
        return inst_list;
    }

    std::vector<uint32_t> free_offsets;
    for (auto off : group_offsets) {
        if (std::find(result.frac_rows.begin(), result.frac_rows.end(), off) == result.frac_rows.end()) {
            free_offsets.push_back(off);
        }
    }

    if (free_offsets.size() != 3) {
        std::cout << "[ERROR]: maj5 expects exactly three free rows" << std::endl;
        return inst_list;
    }

    std::vector<uint32_t> frac_pos;
    for (auto off : free_offsets) {
        frac_pos.push_back(static_cast<uint32_t>(std::distance(
            group_offsets.begin(),
            std::find(group_offsets.begin(), group_offsets.end(), off)
        )));
    }

    inst_list.push_back(encode_uop(AddrMode::MAJ5_1, src1_bank, global_base_row, 0, 0, 0, dont_care));
    inst_list.push_back(encode_uop(AddrMode::MAJ5_2, 0, 0, 0, frac_pos[0], frac_pos[1], frac_pos[2]));
    return inst_list;
}

std::vector<uint32_t> op_or(uint64_t src1, uint64_t src2) {
    std::vector<uint32_t> inst_list;

    uint32_t src1_bank = extract_bank(src1);
    uint32_t src2_bank = extract_bank(src2);

    if (src1_bank != src2_bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within op_or" << std::endl;
        return inst_list;
    }

    uint32_t src1_row = extract_row(src1);
    uint32_t src2_row = extract_row(src2);

    uint32_t mat1 = mat_id_from_row(src1_row);
    uint32_t mat2 = mat_id_from_row(src2_row);

    if (mat1 != mat2) {
        std::cout << "[ERROR]: op_or rows cross MAT boundary" << std::endl;
        return inst_list;
    }

    uint32_t lrow1 = local_row_from_global(src1_row);
    uint32_t lrow2 = local_row_from_global(src2_row);

    RowAnalysisResult result = analyze_rows({lrow1, lrow2});
    if (!result.valid) {
        std::cout << "[ERROR]: NO_RA_PAIR within op_or" << std::endl;
        return inst_list;
    }

    uint32_t local_base_row = result.base_row;
    uint32_t global_base_row = global_row_from_mat(mat1, local_base_row);
    uint32_t ra_group_index = result.ra_group_index;

    if (ra_group_index > 6) {
        std::cout << "[ERROR]: op_or matched non-2RA group" << std::endl;
        return inst_list;
    }

    uint32_t dont_care = dont_care_pos_maj3(ra_group_index);
    const auto& group_offsets = SUPPORTED_GROUPS[ra_group_index].offsets;
    uint32_t max_offset = group_offsets.back();

    if (local_base_row + max_offset >= ROWS_PER_MAT) {
        std::cout << "[ERROR]: op_or group spills outside MAT boundary" << std::endl;
        return inst_list;
    }

    std::vector<uint32_t> free_offsets;
    for (auto off : group_offsets) {
        if (std::find(result.frac_rows.begin(), result.frac_rows.end(), off) == result.frac_rows.end()) {
            free_offsets.push_back(off);
        }
    }

    if (free_offsets.size() < 2) {
        std::cout << "[ERROR]: Not enough free rows for op_or operation" << std::endl;
        return inst_list;
    }

    uint32_t const_offset = free_offsets[0];
    uint32_t const_row_global = global_base_row + const_offset;
    uint32_t const_src_global = global_row_from_mat(mat1, g_const_one_row);

    auto tmp = row_copy(make_row_addr(src1_bank, const_src_global),
                        make_row_addr(src1_bank, const_row_global));
    inst_list.insert(inst_list.end(), tmp.begin(), tmp.end());

    uint32_t out_offset = free_offsets[1];
    uint32_t frac_pos = static_cast<uint32_t>(std::distance(
        group_offsets.begin(),
        std::find(group_offsets.begin(), group_offsets.end(), out_offset)
    ));

    inst_list.push_back(
        encode_uop(AddrMode::MAJ3, src1_bank, global_base_row, 0, frac_pos, 0, dont_care)
    );
    return inst_list;
}

std::vector<uint32_t> op_and(uint64_t src1, uint64_t src2) {
    std::vector<uint32_t> inst_list;

    uint32_t src1_bank = extract_bank(src1);
    uint32_t src2_bank = extract_bank(src2);

    if (src1_bank != src2_bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within op_and" << std::endl;
        return inst_list;
    }

    uint32_t src1_row = extract_row(src1);
    uint32_t src2_row = extract_row(src2);

    uint32_t mat1 = mat_id_from_row(src1_row);
    uint32_t mat2 = mat_id_from_row(src2_row);

    if (mat1 != mat2) {
        std::cout << "[ERROR]: op_and rows cross MAT boundary" << std::endl;
        return inst_list;
    }

    uint32_t lrow1 = local_row_from_global(src1_row);
    uint32_t lrow2 = local_row_from_global(src2_row);

    RowAnalysisResult result = analyze_rows({lrow1, lrow2});
    if (!result.valid) {
        std::cout << "[ERROR]: NO_RA_PAIR within op_and" << std::endl;
        return inst_list;
    }

    uint32_t local_base_row = result.base_row;
    uint32_t global_base_row = global_row_from_mat(mat1, local_base_row);
    uint32_t ra_group_index = result.ra_group_index;

    if (ra_group_index > 6) {
        std::cout << "[ERROR]: op_and matched non-2RA group" << std::endl;
        return inst_list;
    }

    uint32_t dont_care = dont_care_pos_maj3(ra_group_index);
    const auto& group_offsets = SUPPORTED_GROUPS[ra_group_index].offsets;
    uint32_t max_offset = group_offsets.back();

    if (local_base_row + max_offset >= ROWS_PER_MAT) {
        std::cout << "[ERROR]: op_and group spills outside MAT boundary" << std::endl;
        return inst_list;
    }

    std::vector<uint32_t> free_offsets;
    for (auto off : group_offsets) {
        if (std::find(result.frac_rows.begin(), result.frac_rows.end(), off) == result.frac_rows.end()) {
            free_offsets.push_back(off);
        }
    }

    if (free_offsets.size() < 2) {
        std::cout << "[ERROR]: Not enough free rows for op_and operation" << std::endl;
        return inst_list;
    }

    uint32_t const_offset = free_offsets[0];
    uint32_t const_row_global = global_base_row + const_offset;
    uint32_t const_src_global = global_row_from_mat(mat1, g_const_zero_row);

    auto tmp = row_copy(make_row_addr(src1_bank, const_src_global),
                        make_row_addr(src1_bank, const_row_global));
    inst_list.insert(inst_list.end(), tmp.begin(), tmp.end());

    uint32_t out_offset = free_offsets[1];
    uint32_t frac_pos = static_cast<uint32_t>(std::distance(
        group_offsets.begin(),
        std::find(group_offsets.begin(), group_offsets.end(), out_offset)
    ));

    inst_list.push_back(
        encode_uop(AddrMode::MAJ3, src1_bank, global_base_row, 0, frac_pos, 0, dont_care)
    );
    return inst_list;
}

std::vector<uint32_t> op_not(uint64_t src, uint64_t dst) {
    std::vector<uint32_t> inst_list;

    uint32_t src_bank = extract_bank(src);
    uint32_t dst_bank = extract_bank(dst);

    if (src_bank != dst_bank) {
        std::cout << "[ERROR]: op_not requires same bank" << std::endl;
        return inst_list;
    }

    uint32_t src_row = extract_row(src);
    uint32_t dst_row = extract_row(dst);

    uint32_t src_mat = mat_id_from_row(src_row);
    uint32_t dst_mat = mat_id_from_row(dst_row);
    uint32_t hops = (src_mat > dst_mat) ? (src_mat - dst_mat) : (dst_mat - src_mat);

    if ((hops % 2) == 0) {
        std::cout << "[ERROR]: op_not requires odd MAT distance" << std::endl;
        return inst_list;
    }

    return row_copy(src, dst);
}

std::vector<uint32_t> op_xor(uint64_t src1, uint64_t src2) {
    std::vector<uint32_t> inst_list;

    uint32_t src1_bank = extract_bank(src1);
    uint32_t src2_bank = extract_bank(src2);

    if (src1_bank != src2_bank) {
        std::cout << "[ERROR]: Invalid Addr Pair within op_xor" << std::endl;
        return inst_list;
    }

    uint32_t src1_row = extract_row(src1);
    uint32_t src2_row = extract_row(src2);

    uint32_t src_mat0 = mat_id_from_row(src1_row);
    uint32_t src_mat1 = src_mat0 + 1;

    if (mat_id_from_row(src2_row) != src_mat0) {
        std::cout << "[ERROR]: op_xor expects src1/src2 in same MAT" << std::endl;
        return inst_list;
    }

    std::set<uint32_t> used_rows;
    cud_internal::mark_used_addr_and_mirror(used_rows, src1);
    cud_internal::mark_used_addr_and_mirror(used_rows, src2);

    auto row_group_1_opt = cud_internal::alloc_group_rows(src1_bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
    auto row_group_2_opt = cud_internal::alloc_group_rows(src1_bank, src_mat1, cud_internal::two_ra_groups(), used_rows);

    if (!row_group_1_opt || !row_group_2_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for op_xor" << std::endl;
        return inst_list;
    }

    const auto& row_group_1 = *row_group_1_opt;
    const auto& row_group_2 = *row_group_2_opt;

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    append(row_copy(src1, row_group_1[0]));
    append(row_copy(src1 + ROWS_PER_MAT, row_group_2[0]));
    append(row_copy(src2, row_group_1[1]));
    append(row_copy(src2 + ROWS_PER_MAT, row_group_2[1]));
    append(op_or(row_group_1[0], row_group_1[1]));
    append(op_and(row_group_2[0], row_group_2[1]));
    append(op_not(row_group_2[0], row_group_1[1]));
    append(op_and(row_group_1[0], row_group_1[1]));

    return inst_list;
}

std::vector<uint32_t> maj5_via_maj3(uint64_t src1, uint64_t src2, uint64_t src3,
                                     uint64_t src4, uint64_t src5, uint64_t out) {
    std::vector<uint32_t> inst_list;

    uint32_t bank = extract_bank(src1);
    if (extract_bank(src2) != bank || extract_bank(src3) != bank ||
        extract_bank(src4) != bank || extract_bank(src5) != bank ||
        extract_bank(out)  != bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within maj5_via_maj3" << std::endl;
        return {};
    }

    uint32_t row1    = extract_row(src1);
    uint32_t row2    = extract_row(src2);
    uint32_t row3    = extract_row(src3);
    uint32_t row4    = extract_row(src4);
    uint32_t row5    = extract_row(src5);
    uint32_t out_row = extract_row(out);

    uint32_t mat = mat_id_from_row(row1);
    if (mat_id_from_row(row2) != mat || mat_id_from_row(row3) != mat ||
        mat_id_from_row(row4) != mat || mat_id_from_row(row5) != mat ||
        mat_id_from_row(out_row) != mat) {
        std::cout << "[ERROR]: maj5_via_maj3 rows must stay in same MAT" << std::endl;
        return {};
    }

    std::set<uint32_t> used_rows;
    cud_internal::mark_used_addr(used_rows, src1);
    cud_internal::mark_used_addr(used_rows, src2);
    cud_internal::mark_used_addr(used_rows, src3);
    cud_internal::mark_used_addr(used_rows, src4);
    cud_internal::mark_used_addr(used_rows, src5);
    cud_internal::mark_used_addr(used_rows, out);
    if (cud_internal::g_extra_reserved != nullptr) {
        used_rows.insert(cud_internal::g_extra_reserved->begin(),
                         cud_internal::g_extra_reserved->end());
    }

    auto g1_opt = cud_internal::alloc_group_rows(bank, mat, cud_internal::two_ra_groups(), used_rows);
    auto g2_opt = cud_internal::alloc_group_rows(bank, mat, cud_internal::two_ra_groups(), used_rows);
    auto g3_opt = cud_internal::alloc_group_rows(bank, mat, cud_internal::two_ra_groups(), used_rows);

    bool final_stage_anchored = false;
    std::optional<std::vector<uint64_t>> g4_opt;
    if (out_row == row1) {
        g4_opt = cud_internal::alloc_group_rows_anchored(
            bank, mat, cud_internal::two_ra_groups(), out_row, used_rows);
        final_stage_anchored = g4_opt.has_value();
    }
    if (!g4_opt) {
        g4_opt = cud_internal::alloc_group_rows(bank, mat, cud_internal::two_ra_groups(), used_rows);
    }

    if (!g1_opt || !g2_opt || !g3_opt || !g4_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for maj5_via_maj3" << std::endl;
        return {};
    }

    const auto& g1 = *g1_opt;
    const auto& g2 = *g2_opt;
    const auto& g3 = *g3_opt;
    const auto& g4 = *g4_opt;

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    // t1 = maj3(src2, src3, src4)
    append(row_copy(src2, g1[0]));
    append(row_copy(src3, g1[1]));
    append(row_copy(src4, g1[2]));
    append(maj3(g1[0], g1[1], g1[2]));

    // t2 = maj3(src1, src3, src4)
    append(row_copy(src1, g2[0]));
    append(row_copy(src3, g2[1]));
    append(row_copy(src4, g2[2]));
    append(maj3(g2[0], g2[1], g2[2]));

    // t3 = maj3(src2, src5, t2)
    append(row_copy(src2, g3[0]));
    append(row_copy(src5, g3[1]));
    append(row_copy(g2[0], g3[2]));
    append(maj3(g3[0], g3[1], g3[2]));

    // out = maj3(src1, t1, t3)
    if (final_stage_anchored) {
        std::vector<uint64_t> helper_rows;
        for (uint64_t addr : g4) {
            if (extract_row(addr) != out_row && helper_rows.size() < 2) {
                helper_rows.push_back(addr);
            }
        }
        if (helper_rows.size() != 2) {
            std::cout << "[ERROR]: Failed to anchor final maj3 in maj5_via_maj3" << std::endl;
            return {};
        }
        append(row_copy(g1[0], helper_rows[0]));
        append(row_copy(g3[0], helper_rows[1]));
        append(maj3(out, helper_rows[0], helper_rows[1]));
    } else {
        append(row_copy(src1, g4[0]));
        append(row_copy(g1[0], g4[1]));
        append(row_copy(g3[0], g4[2]));
        append(maj3(g4[0], g4[1], g4[2]));
        append(row_copy(g4[0], out));
    }

    return inst_list;
}

// In-place MAJ5 via MAJ3, result overwrites src1.
std::vector<uint32_t> maj5_via_maj3_sum_inplace(
    uint64_t src1, uint64_t src2, uint64_t src3, uint64_t src4, uint64_t src5)
{
    std::vector<uint32_t> inst_list;

    uint32_t bank = extract_bank(src1);
    if (extract_bank(src2) != bank || extract_bank(src3) != bank ||
        extract_bank(src4) != bank || extract_bank(src5) != bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within maj5_via_maj3_sum_inplace" << std::endl;
        return {};
    }

    uint32_t row1 = extract_row(src1);
    uint32_t row2 = extract_row(src2);
    uint32_t row3 = extract_row(src3);
    uint32_t row4 = extract_row(src4);
    uint32_t row5 = extract_row(src5);

    uint32_t mat = mat_id_from_row(row1);
    if (mat_id_from_row(row2) != mat || mat_id_from_row(row3) != mat ||
        mat_id_from_row(row4) != mat || mat_id_from_row(row5) != mat) {
        std::cout << "[ERROR]: maj5_via_maj3_sum_inplace rows must stay in same MAT" << std::endl;
        return {};
    }

    if (row1 == row3) {
        return maj5_via_maj3(src1, src2, src3, src4, src5, src1);
    }

    std::set<uint32_t> used_rows;
    cud_internal::mark_used_addr(used_rows, src1);
    cud_internal::mark_used_addr(used_rows, src2);
    cud_internal::mark_used_addr(used_rows, src3);
    cud_internal::mark_used_addr(used_rows, src4);
    cud_internal::mark_used_addr(used_rows, src5);
    if (cud_internal::g_extra_reserved != nullptr) {
        used_rows.insert(cud_internal::g_extra_reserved->begin(),
                         cud_internal::g_extra_reserved->end());
    }

    auto g1_opt = cud_internal::alloc_group_rows(bank, mat, cud_internal::two_ra_groups(), used_rows);
    auto g23_opt = cud_internal::alloc_group_rows_anchored(
        bank, mat, cud_internal::two_ra_groups(), row3, used_rows);

    std::optional<std::vector<uint64_t>> g4_opt =
        cud_internal::alloc_group_rows_anchored(bank, mat, cud_internal::two_ra_groups(), row1, used_rows);
    const bool final_stage_anchored = g4_opt.has_value();
    if (!g4_opt) {
        g4_opt = cud_internal::alloc_group_rows(bank, mat, cud_internal::two_ra_groups(), used_rows);
    }

    if (!g1_opt || !g23_opt || !g4_opt) {
        return maj5_via_maj3(src1, src2, src3, src4, src5, src1);
    }

    const auto& g1  = *g1_opt;
    const auto& g23 = *g23_opt;
    const auto& g4  = *g4_opt;

    std::vector<uint64_t> g23_helpers;
    for (uint64_t addr : g23) {
        if (extract_row(addr) != row3 && g23_helpers.size() < 2) {
            g23_helpers.push_back(addr);
        }
    }
    if (g23_helpers.size() != 2) {
        return maj5_via_maj3(src1, src2, src3, src4, src5, src1);
    }

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    // t1 = maj3(src2, src3, src4)
    append(row_copy(src2, g1[0]));
    append(row_copy(src3, g1[1]));
    append(row_copy(src4, g1[2]));
    append(maj3(g1[0], g1[1], g1[2]));

    // Reuse src3 as temp: t2 = maj3(src1, src3, src4)
    append(row_copy(src1, g23_helpers[0]));
    append(row_copy(src4, g23_helpers[1]));
    append(maj3(src3, g23_helpers[0], g23_helpers[1]));

    // Reuse anchored group: t3 = maj3(src2, src5, t2)
    append(row_copy(src2, g23_helpers[0]));
    append(row_copy(src5, g23_helpers[1]));
    append(maj3(src3, g23_helpers[0], g23_helpers[1]));

    // Final SUM written into src1 when possible
    if (final_stage_anchored) {
        std::vector<uint64_t> g4_helpers;
        for (uint64_t addr : g4) {
            if (extract_row(addr) != row1 && g4_helpers.size() < 2) {
                g4_helpers.push_back(addr);
            }
        }
        if (g4_helpers.size() != 2) return {};
        append(row_copy(g1[0], g4_helpers[0]));
        append(row_copy(src3,  g4_helpers[1]));
        append(maj3(src1, g4_helpers[0], g4_helpers[1]));
    } else {
        append(row_copy(src1,  g4[0]));
        append(row_copy(g1[0], g4[1]));
        append(row_copy(src3,  g4[2]));
        append(maj3(g4[0], g4[1], g4[2]));
        append(row_copy(g4[0], src1));
    }

    return inst_list;
}
