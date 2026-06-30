#include "internal.h"
#include "../../include/cud/ops.h"
#include "../../include/cud/types.h"
#include <array>
#include <iostream>
#include <optional>
#include <set>

static std::vector<uint32_t> add_new_fast_for_mult(
    uint64_t src1, uint64_t src2, uint64_t src3,
    uint64_t sum_m0, uint64_t sum_m1,
    uint64_t carry_m0, uint64_t carry_m1
) {
    std::vector<uint32_t> inst_list;

    uint32_t bank = extract_bank(src1);
    if (extract_bank(src2) != bank || extract_bank(src3) != bank ||
        extract_bank(sum_m0) != bank || extract_bank(sum_m1) != bank ||
        extract_bank(carry_m0) != bank || extract_bank(carry_m1) != bank) {
        std::cout << "[ERROR]: Invalid Addr Pair\n";
        return {};
    }

    uint32_t src_mat0 = mat_id_from_row(extract_row(src1));
    uint32_t src_mat1 = src_mat0 + 1;
    if (mat_id_from_row(extract_row(src2)) != src_mat0 ||
        mat_id_from_row(extract_row(src3)) != src_mat0) {
        std::cout << "[ERROR]: add_new_fast_for_mult expects src1/src2/src3 in same MAT\n";
        return {};
    }

    std::set<uint32_t> used_rows;
    cud_internal::mark_used_addr_and_mirror(used_rows, src1);
    cud_internal::mark_used_addr_and_mirror(used_rows, src2);
    cud_internal::mark_used_addr_and_mirror(used_rows, src3);
    cud_internal::mark_used_addr(used_rows, sum_m0);
    cud_internal::mark_used_addr(used_rows, sum_m1);
    cud_internal::mark_used_addr(used_rows, carry_m0);
    cud_internal::mark_used_addr(used_rows, carry_m1);

    auto cg_m0_opt = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
    auto cg_m1_opt = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);
    auto sg_m0_opt = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::three_ra_groups(), used_rows);
    auto sg_m1_opt = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::three_ra_groups(), used_rows);
    if (!cg_m0_opt || !cg_m1_opt || !sg_m0_opt || !sg_m1_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for add_new_fast_for_mult\n";
        return {};
    }

    std::vector<uint64_t> cg_m0 = *cg_m0_opt;
    std::vector<uint64_t> cg_m1 = *cg_m1_opt;
    std::vector<uint64_t> sg_m0(sg_m0_opt->begin(), sg_m0_opt->begin() + 5);
    std::vector<uint64_t> sg_m1(sg_m1_opt->begin(), sg_m1_opt->begin() + 5);

    cud_internal::ScopedExtraReserved scope(&used_rows);

    auto emit = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    emit(row_copy_fan(src1, cg_m0[0], sg_m0[0]));
    emit(row_copy_fan(src2, cg_m0[1], sg_m0[1]));
    emit(row_copy_fan(src3, cg_m0[2], sg_m0[2]));
    emit(row_copy_fan(src1 + ROWS_PER_MAT, cg_m1[0], sg_m1[0]));
    emit(row_copy_fan(src2 + ROWS_PER_MAT, cg_m1[1], sg_m1[1]));
    emit(row_copy_fan(src3 + ROWS_PER_MAT, cg_m1[2], sg_m1[2]));

    emit(maj3(cg_m0[0], cg_m0[1], cg_m0[2]));
    emit(row_copy(cg_m0[0], carry_m0));

    emit(maj3(cg_m1[0], cg_m1[1], cg_m1[2]));
    emit(row_copy(cg_m1[0], carry_m1));

    emit(op_not(cg_m1[0], sg_m0[3]));
    emit(row_copy(sg_m0[3], sg_m0[4]));
    emit(maj5(sg_m0[0], sg_m0[1], sg_m0[2], sg_m0[3], sg_m0[4]));
    emit(row_copy(sg_m0[0], sum_m0));

    emit(op_not(cg_m0[0], sg_m1[3]));
    emit(row_copy(sg_m1[3], sg_m1[4]));
    emit(maj5(sg_m1[0], sg_m1[1], sg_m1[2], sg_m1[3], sg_m1[4]));
    emit(row_copy(sg_m1[0], sum_m1));

    return inst_list;
}

std::vector<uint32_t> mult_4bit_dual(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0,
    std::vector<uint64_t> prod_m1
) {
    std::vector<uint32_t> inst_list;

    if (srcA.size() != 4 || srcB.size() != 4 || prod_m0.size() != 8 || prod_m1.size() != 8) {
        std::cout << "[ERROR]: mult_4bit_dual expects A/B=4 bits and product=8 bits\n";
        return {};
    }

    uint32_t bank = extract_bank(srcA[0]);
    for (int i = 0; i < 4; i++) {
        if (extract_bank(srcA[i]) != bank || extract_bank(srcB[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }
    for (int i = 0; i < 8; i++) {
        if (extract_bank(prod_m0[i]) != bank || extract_bank(prod_m1[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    uint32_t src_mat0 = mat_id_from_row(extract_row(srcA[0]));
    uint32_t src_mat1 = src_mat0 + 1;
    for (int i = 0; i < 4; i++) {
        if (mat_id_from_row(extract_row(srcA[i])) != src_mat0 ||
            mat_id_from_row(extract_row(srcB[i])) != src_mat0) {
            std::cout << "[ERROR]: mult_4bit_dual expects srcA/srcB in same MAT\n";
            return {};
        }
    }
    for (int i = 0; i < 8; i++) {
        if (mat_id_from_row(extract_row(prod_m0[i])) != src_mat0 ||
            mat_id_from_row(extract_row(prod_m1[i])) != src_mat1) {
            std::cout << "[ERROR]: mult_4bit_dual expects product outputs in adjacent MATs\n";
            return {};
        }
    }

    uint64_t zero_m0 = make_row_addr(bank, global_row_from_mat(src_mat0, g_const_zero_row));

    std::set<uint32_t> used_rows;
    for (auto x : srcA)    cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : srcB)    cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : prod_m0) cud_internal::mark_used_addr(used_rows, x);
    for (auto x : prod_m1) cud_internal::mark_used_addr(used_rows, x);
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat0, g_const_zero_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat0, g_const_one_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat1, g_const_zero_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat1, g_const_one_row));

    auto ag_m0_opt = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
    auto ag_m1_opt = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);
    if (!ag_m0_opt || !ag_m1_opt) {
        std::cout << "[ERROR]: No free 2RA scratch groups for AND stage in mult_4bit_dual\n";
        return {};
    }

    std::vector<uint64_t> ag_m0 = *ag_m0_opt;
    std::vector<uint64_t> ag_m1 = *ag_m1_opt;
    uint64_t and_a_m0 = ag_m0[0], and_b_m0 = ag_m0[1];
    uint64_t and_a_m1 = ag_m1[0], and_b_m1 = ag_m1[1];

    constexpr size_t kMirroredScratchRows = 39;
    auto ls_opt = cud_internal::alloc_mirrored_rows(src_mat0, src_mat1, kMirroredScratchRows, used_rows);
    if (!ls_opt) {
        std::cout << "[ERROR]: No free mirrored scratch rows for mult_4bit_dual\n";
        return {};
    }

    const auto& ls = *ls_opt;
    size_t si = 0;
    auto next_pair = [&](uint64_t& r0, uint64_t& r1) {
        uint32_t lr = ls[si++];
        r0 = make_row_addr(bank, global_row_from_mat(src_mat0, lr));
        r1 = make_row_addr(bank, global_row_from_mat(src_mat1, lr));
    };

    std::vector<std::vector<uint64_t>> pp_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_m1(4, std::vector<uint64_t>(4));
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
            next_pair(pp_m0[j][i], pp_m1[j][i]);

    uint64_t s2a_m0=0, s2a_m1=0, c3a_m0=0, c3a_m1=0, c3b_m0=0, c3b_m1=0;
    next_pair(s2a_m0,s2a_m1); next_pair(c3a_m0,c3a_m1); next_pair(c3b_m0,c3b_m1);

    uint64_t s3a_m0=0, s3a_m1=0, c4a_m0=0, c4a_m1=0;
    uint64_t s3b_m0=0, s3b_m1=0, c4b_m0=0, c4b_m1=0, c4c_m0=0, c4c_m1=0;
    next_pair(s3a_m0,s3a_m1); next_pair(c4a_m0,c4a_m1);
    next_pair(s3b_m0,s3b_m1); next_pair(c4b_m0,c4b_m1); next_pair(c4c_m0,c4c_m1);

    uint64_t s4a_m0=0, s4a_m1=0, c5a_m0=0, c5a_m1=0;
    uint64_t s4b_m0=0, s4b_m1=0, c5b_m0=0, c5b_m1=0, c5c_m0=0, c5c_m1=0;
    next_pair(s4a_m0,s4a_m1); next_pair(c5a_m0,c5a_m1);
    next_pair(s4b_m0,s4b_m1); next_pair(c5b_m0,c5b_m1); next_pair(c5c_m0,c5c_m1);

    uint64_t s5a_m0=0, s5a_m1=0, c6a_m0=0, c6a_m1=0;
    uint64_t s5b_m0=0, s5b_m1=0, c6b_m0=0, c6b_m1=0, c6c_m0=0, c6c_m1=0;
    next_pair(s5a_m0,s5a_m1); next_pair(c6a_m0,c6a_m1);
    next_pair(s5b_m0,s5b_m1); next_pair(c6b_m0,c6b_m1); next_pair(c6c_m0,c6c_m1);

    uint64_t s6a_m0=0, s6a_m1=0, c7a_m0=0, c7a_m1=0, c7b_m0=0, c7b_m1=0;
    uint64_t overflow_m0=0, overflow_m1=0, final_carry_m0=0, final_carry_m1=0;
    next_pair(s6a_m0,s6a_m1); next_pair(c7a_m0,c7a_m1); next_pair(c7b_m0,c7b_m1);
    next_pair(overflow_m0,overflow_m1); next_pair(final_carry_m0,final_carry_m1);

    if (si != kMirroredScratchRows) {
        std::cout << "[ERROR]: Internal scratch allocation mismatch in mult_4bit_dual\n";
        return {};
    }

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    auto gen_pp_dual = [&](uint64_t a_m0, uint64_t b_m0, uint64_t dst_m0, uint64_t dst_m1) {
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };
        e(row_copy(a_m0, and_a_m0));
        e(row_copy(b_m0, and_b_m0));
        e(op_and(and_a_m0, and_b_m0));
        e(row_copy(and_a_m0, dst_m0));
        e(row_copy(a_m0 + ROWS_PER_MAT, and_a_m1));
        e(row_copy(b_m0 + ROWS_PER_MAT, and_b_m1));
        e(op_and(and_a_m1, and_b_m1));
        e(row_copy(and_a_m1, dst_m1));
        return local;
    };

    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
            append(gen_pp_dual(srcA[i], srcB[j], pp_m0[j][i], pp_m1[j][i]));

    append(row_copy(pp_m0[0][0], prod_m0[0]));
    append(row_copy(pp_m1[0][0], prod_m1[0]));

    cud_internal::ScopedExtraReserved guard(&used_rows);

    append(add_new(pp_m0[0][1], pp_m0[1][0], zero_m0,       prod_m0[1], prod_m1[1], c3a_m0, c3a_m1));
    append(add_new(pp_m0[0][2], pp_m0[1][1], c3a_m0,        s2a_m0, s2a_m1, c3b_m0, c3b_m1));
    append(add_new(s2a_m0,      pp_m0[2][0], zero_m0,        prod_m0[2], prod_m1[2], c4a_m0, c4a_m1));
    append(add_new(pp_m0[0][3], pp_m0[1][2], pp_m0[2][1],   s3a_m0, s3a_m1, c4b_m0, c4b_m1));
    append(add_new(pp_m0[3][0], c3b_m0,      c4a_m0,         s3b_m0, s3b_m1, c4c_m0, c4c_m1));
    append(add_new(s3a_m0,      s3b_m0,       zero_m0,        prod_m0[3], prod_m1[3], c5a_m0, c5a_m1));
    append(add_new(pp_m0[1][3], pp_m0[2][2], pp_m0[3][1],   s4a_m0, s4a_m1, c5b_m0, c5b_m1));
    append(add_new(c4b_m0,      c4c_m0,       c5a_m0,         s4b_m0, s4b_m1, c5c_m0, c5c_m1));
    append(add_new(s4a_m0,      s4b_m0,       zero_m0,        prod_m0[4], prod_m1[4], c6a_m0, c6a_m1));
    append(add_new(pp_m0[2][3], pp_m0[3][2], c5b_m0,         s5a_m0, s5a_m1, c6b_m0, c6b_m1));
    append(add_new(c5c_m0,      c6a_m0,       zero_m0,        s5b_m0, s5b_m1, c6c_m0, c6c_m1));
    append(add_new(s5a_m0,      s5b_m0,       zero_m0,        prod_m0[5], prod_m1[5], c7a_m0, c7a_m1));
    append(add_new(pp_m0[3][3], c6b_m0,       c6c_m0,         s6a_m0, s6a_m1, c7b_m0, c7b_m1));
    append(add_new(s6a_m0,      c7a_m0,       zero_m0,        prod_m0[6], prod_m1[6], overflow_m0, overflow_m1));
    append(add_new(c7b_m0,      overflow_m0,  zero_m0,        prod_m0[7], prod_m1[7], final_carry_m0, final_carry_m1));

    return inst_list;
}

std::vector<uint32_t> mult_4bit_opt_dual(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0,
    std::vector<uint64_t> prod_m1
) {
    std::vector<uint32_t> inst_list;

    if (srcA.size() != 4 || srcB.size() != 4 || prod_m0.size() != 8 || prod_m1.size() != 8) {
        std::cout << "[ERROR]: mult_4bit_opt_dual expects A/B=4 bits and product=8 bits\n";
        return {};
    }

    uint32_t bank = extract_bank(srcA[0]);
    for (int i = 0; i < 4; i++) {
        if (extract_bank(srcA[i]) != bank || extract_bank(srcB[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }
    for (int i = 0; i < 8; i++) {
        if (extract_bank(prod_m0[i]) != bank || extract_bank(prod_m1[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    uint32_t src_mat0 = mat_id_from_row(extract_row(srcA[0]));
    uint32_t src_mat1 = src_mat0 + 1;
    for (int i = 0; i < 4; i++) {
        if (mat_id_from_row(extract_row(srcA[i])) != src_mat0 ||
            mat_id_from_row(extract_row(srcB[i])) != src_mat0) {
            std::cout << "[ERROR]: mult_4bit_opt_dual expects srcA/srcB in same MAT\n";
            return {};
        }
    }
    for (int i = 0; i < 8; i++) {
        if (mat_id_from_row(extract_row(prod_m0[i])) != src_mat0 ||
            mat_id_from_row(extract_row(prod_m1[i])) != src_mat1) {
            std::cout << "[ERROR]: mult_4bit_opt_dual expects product outputs in adjacent MATs\n";
            return {};
        }
    }

    uint64_t zero_m0 = make_row_addr(bank, global_row_from_mat(src_mat0, g_const_zero_row));

    std::set<uint32_t> used_rows;
    for (auto x : srcA)    cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : srcB)    cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : prod_m0) cud_internal::mark_used_addr(used_rows, x);
    for (auto x : prod_m1) cud_internal::mark_used_addr(used_rows, x);
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat0, g_const_zero_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat0, g_const_one_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat1, g_const_zero_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat1, g_const_one_row));

    std::vector<std::vector<uint64_t>> pp_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_m1(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_rhs_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_rhs_m1(4, std::vector<uint64_t>(4));

    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            auto g0 = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
            auto g1 = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);
            if (!g0 || !g1) {
                std::cout << "[ERROR]: No free 2RA scratch groups for partial products in mult_4bit_opt_dual\n";
                return {};
            }
            pp_m0[j][i]     = (*g0)[0];
            pp_rhs_m0[j][i] = (*g0)[1];
            pp_m1[j][i]     = (*g1)[0];
            pp_rhs_m1[j][i] = (*g1)[1];
        }
    }

    constexpr size_t kMirroredScratchRows = 23;
    auto ls_opt = cud_internal::alloc_mirrored_rows(src_mat0, src_mat1, kMirroredScratchRows, used_rows);
    if (!ls_opt) {
        std::cout << "[ERROR]: No free mirrored scratch rows for mult_4bit_opt_dual\n";
        return {};
    }

    const auto& ls = *ls_opt;
    size_t si = 0;
    auto next_pair = [&](uint64_t& r0, uint64_t& r1) {
        uint32_t lr = ls[si++];
        r0 = make_row_addr(bank, global_row_from_mat(src_mat0, lr));
        r1 = make_row_addr(bank, global_row_from_mat(src_mat1, lr));
    };

    uint64_t s2a_m0=0, s2a_m1=0, c3a_m0=0, c3a_m1=0, c3b_m0=0, c3b_m1=0;
    next_pair(s2a_m0,s2a_m1); next_pair(c3a_m0,c3a_m1); next_pair(c3b_m0,c3b_m1);

    uint64_t s3a_m0=0, s3a_m1=0, c4a_m0=0, c4a_m1=0;
    uint64_t s3b_m0=0, s3b_m1=0, c4b_m0=0, c4b_m1=0, c4c_m0=0, c4c_m1=0;
    next_pair(s3a_m0,s3a_m1); next_pair(c4a_m0,c4a_m1);
    next_pair(s3b_m0,s3b_m1); next_pair(c4b_m0,c4b_m1); next_pair(c4c_m0,c4c_m1);

    uint64_t s4a_m0=0, s4a_m1=0, c5a_m0=0, c5a_m1=0;
    uint64_t s4b_m0=0, s4b_m1=0, c5b_m0=0, c5b_m1=0, c5c_m0=0, c5c_m1=0;
    next_pair(s4a_m0,s4a_m1); next_pair(c5a_m0,c5a_m1);
    next_pair(s4b_m0,s4b_m1); next_pair(c5b_m0,c5b_m1); next_pair(c5c_m0,c5c_m1);

    uint64_t s5a_m0=0, s5a_m1=0, c6a_m0=0, c6a_m1=0;
    uint64_t s5b_m0=0, s5b_m1=0, c6b_m0=0, c6b_m1=0, c6c_m0=0, c6c_m1=0;
    next_pair(s5a_m0,s5a_m1); next_pair(c6a_m0,c6a_m1);
    next_pair(s5b_m0,s5b_m1); next_pair(c6b_m0,c6b_m1); next_pair(c6c_m0,c6c_m1);

    uint64_t s6a_m0=0, s6a_m1=0, c7a_m0=0, c7a_m1=0, c7b_m0=0, c7b_m1=0;
    uint64_t overflow_m0=0, overflow_m1=0, final_carry_m0=0, final_carry_m1=0;
    next_pair(s6a_m0,s6a_m1); next_pair(c7a_m0,c7a_m1); next_pair(c7b_m0,c7b_m1);
    next_pair(overflow_m0,overflow_m1); next_pair(final_carry_m0,final_carry_m1);

    if (si != kMirroredScratchRows) {
        std::cout << "[ERROR]: Internal scratch allocation mismatch in mult_4bit_opt_dual\n";
        return {};
    }

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    auto gen_pp_dual = [&](uint64_t a_m0, uint64_t b_m0,
                           uint64_t ppr_m0, uint64_t ppb_m0,
                           uint64_t ppr_m1, uint64_t ppb_m1) {
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };
        e(row_copy(a_m0, ppr_m0));
        e(row_copy(b_m0, ppb_m0));
        e(op_and(ppr_m0, ppb_m0));
        e(row_copy(a_m0 + ROWS_PER_MAT, ppr_m1));
        e(row_copy(b_m0 + ROWS_PER_MAT, ppb_m1));
        e(op_and(ppr_m1, ppb_m1));
        return local;
    };

    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
            append(gen_pp_dual(srcA[i], srcB[j],
                               pp_m0[j][i], pp_rhs_m0[j][i],
                               pp_m1[j][i], pp_rhs_m1[j][i]));

    append(row_copy(pp_m0[0][0], prod_m0[0]));
    append(row_copy(pp_m1[0][0], prod_m1[0]));

    cud_internal::ScopedExtraReserved guard(&used_rows);

    append(add_new_fast_for_mult(pp_m0[0][1], pp_m0[1][0], zero_m0,     prod_m0[1], prod_m1[1], c3a_m0, c3a_m1));
    append(add_new_fast_for_mult(pp_m0[0][2], pp_m0[1][1], c3a_m0,      s2a_m0, s2a_m1, c3b_m0, c3b_m1));
    append(add_new_fast_for_mult(s2a_m0,      pp_m0[2][0], zero_m0,     prod_m0[2], prod_m1[2], c4a_m0, c4a_m1));
    append(add_new_fast_for_mult(pp_m0[0][3], pp_m0[1][2], pp_m0[2][1], s3a_m0, s3a_m1, c4b_m0, c4b_m1));
    append(add_new_fast_for_mult(pp_m0[3][0], c3b_m0,      c4a_m0,      s3b_m0, s3b_m1, c4c_m0, c4c_m1));
    append(add_new_fast_for_mult(s3a_m0,      s3b_m0,      zero_m0,     prod_m0[3], prod_m1[3], c5a_m0, c5a_m1));
    append(add_new_fast_for_mult(pp_m0[1][3], pp_m0[2][2], pp_m0[3][1], s4a_m0, s4a_m1, c5b_m0, c5b_m1));
    append(add_new_fast_for_mult(c4b_m0,      c4c_m0,      c5a_m0,      s4b_m0, s4b_m1, c5c_m0, c5c_m1));
    append(add_new_fast_for_mult(s4a_m0,      s4b_m0,      zero_m0,     prod_m0[4], prod_m1[4], c6a_m0, c6a_m1));
    append(add_new_fast_for_mult(pp_m0[2][3], pp_m0[3][2], c5b_m0,      s5a_m0, s5a_m1, c6b_m0, c6b_m1));
    append(add_new_fast_for_mult(c5c_m0,      c6a_m0,      zero_m0,     s5b_m0, s5b_m1, c6c_m0, c6c_m1));
    append(add_new_fast_for_mult(s5a_m0,      s5b_m0,      zero_m0,     prod_m0[5], prod_m1[5], c7a_m0, c7a_m1));
    append(add_new_fast_for_mult(pp_m0[3][3], c6b_m0,      c6c_m0,      s6a_m0, s6a_m1, c7b_m0, c7b_m1));
    append(add_new_fast_for_mult(s6a_m0,      c7a_m0,      zero_m0,     prod_m0[6], prod_m1[6], overflow_m0, overflow_m1));
    append(add_new_fast_for_mult(c7b_m0,      overflow_m0, zero_m0,     prod_m0[7], prod_m1[7], final_carry_m0, final_carry_m1));

    return inst_list;
}

std::vector<uint32_t> mult_4bit_opt(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0
) {
    std::vector<uint32_t> inst_list;

    if (srcA.size() != 4 || srcB.size() != 4 || prod_m0.size() != 8) {
        std::cout << "[ERROR]: mult_4bit_opt expects A/B=4 bits and product=8 bits\n";
        return {};
    }

    uint32_t bank = extract_bank(srcA[0]);
    for (int i = 0; i < 4; i++) {
        if (extract_bank(srcA[i]) != bank || extract_bank(srcB[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }
    for (int i = 0; i < 8; i++) {
        if (extract_bank(prod_m0[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    uint32_t src_mat0 = mat_id_from_row(extract_row(srcA[0]));
    uint32_t src_mat1 = src_mat0 + 1;
    for (int i = 0; i < 4; i++) {
        if (mat_id_from_row(extract_row(srcA[i])) != src_mat0 ||
            mat_id_from_row(extract_row(srcB[i])) != src_mat0) {
            std::cout << "[ERROR]: mult_4bit_opt expects srcA/srcB in same MAT\n";
            return {};
        }
    }
    for (int i = 0; i < 8; i++) {
        if (mat_id_from_row(extract_row(prod_m0[i])) != src_mat0) {
            std::cout << "[ERROR]: mult_4bit_opt expects outputs in source MAT\n";
            return {};
        }
    }

    const uint64_t zero_m0 = make_row_addr(bank, global_row_from_mat(src_mat0, g_const_zero_row));
    const uint64_t zero_m1 = make_row_addr(bank, global_row_from_mat(src_mat1, g_const_zero_row));

    std::set<uint32_t> used_rows;
    for (auto x : srcA)    cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : srcB)    cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : prod_m0) cud_internal::mark_used_addr(used_rows, x);
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat0, g_const_zero_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat0, g_const_one_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat1, g_const_zero_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat1, g_const_one_row));

    std::vector<std::vector<uint64_t>> pp_m0_arr(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_m1_arr(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_rhs_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_rhs_m1(4, std::vector<uint64_t>(4));

    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            auto g0 = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
            auto g1 = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);
            if (!g0 || !g1) {
                std::cout << "[ERROR]: No free 2RA scratch groups for partial products in mult_4bit_opt\n";
                return {};
            }
            pp_m0_arr[j][i] = (*g0)[0];
            pp_rhs_m0[j][i] = (*g0)[1];
            pp_m1_arr[j][i] = (*g1)[0];
            pp_rhs_m1[j][i] = (*g1)[1];
        }
    }

    constexpr size_t kMirroredScratchRows = 22;
    auto ls_opt = cud_internal::alloc_mirrored_rows(src_mat0, src_mat1, kMirroredScratchRows, used_rows);
    if (!ls_opt) {
        std::cout << "[ERROR]: No free mirrored scratch rows for mult_4bit_opt\n";
        return {};
    }

    const auto& ls = *ls_opt;
    size_t si = 0;
    auto next_pair = [&](uint64_t& r0, uint64_t& r1) {
        uint32_t lr = ls[si++];
        r0 = make_row_addr(bank, global_row_from_mat(src_mat0, lr));
        r1 = make_row_addr(bank, global_row_from_mat(src_mat1, lr));
    };

    uint64_t s2a_m0=0, s2a_m1=0, c3a_m0=0, c3a_m1=0, c3b_m0=0, c3b_m1=0;
    next_pair(s2a_m0,s2a_m1); next_pair(c3a_m0,c3a_m1); next_pair(c3b_m0,c3b_m1);

    uint64_t s3a_m0=0, s3a_m1=0, c4a_m0=0, c4a_m1=0;
    uint64_t s3b_m0=0, s3b_m1=0, c4b_m0=0, c4b_m1=0, c4c_m0=0, c4c_m1=0;
    next_pair(s3a_m0,s3a_m1); next_pair(c4a_m0,c4a_m1);
    next_pair(s3b_m0,s3b_m1); next_pair(c4b_m0,c4b_m1); next_pair(c4c_m0,c4c_m1);

    uint64_t s4a_m0=0, s4a_m1=0, c5a_m0=0, c5a_m1=0;
    uint64_t s4b_m0=0, s4b_m1=0, c5b_m0=0, c5b_m1=0, c5c_m0=0, c5c_m1=0;
    next_pair(s4a_m0,s4a_m1); next_pair(c5a_m0,c5a_m1);
    next_pair(s4b_m0,s4b_m1); next_pair(c5b_m0,c5b_m1); next_pair(c5c_m0,c5c_m1);

    uint64_t s5a_m0=0, s5a_m1=0, c6a_m0=0, c6a_m1=0;
    uint64_t s5b_m0=0, s5b_m1=0, c6b_m0=0, c6b_m1=0, c6c_m0=0, c6c_m1=0;
    next_pair(s5a_m0,s5a_m1); next_pair(c6a_m0,c6a_m1);
    next_pair(s5b_m0,s5b_m1); next_pair(c6b_m0,c6b_m1); next_pair(c6c_m0,c6c_m1);

    uint64_t s6a_m0=0, s6a_m1=0, c7a_m0=0, c7a_m1=0, c7b_m0=0, c7b_m1=0;
    uint64_t overflow_m0=0, overflow_m1=0;
    next_pair(s6a_m0,s6a_m1); next_pair(c7a_m0,c7a_m1); next_pair(c7b_m0,c7b_m1);
    next_pair(overflow_m0,overflow_m1);

    if (si != kMirroredScratchRows) {
        std::cout << "[ERROR]: Internal scratch allocation mismatch in mult_4bit_opt\n";
        return {};
    }

    auto ac_m0_opt = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
    auto ac_m1_opt = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);
    auto as_m0_opt = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::three_ra_groups(), used_rows);
    auto as_m1_opt = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::three_ra_groups(), used_rows);
    if (!ac_m0_opt || !ac_m1_opt || !as_m0_opt || !as_m1_opt) {
        std::cout << "[ERROR]: No free adder scratch groups for mult_4bit_opt\n";
        return {};
    }

    std::vector<uint64_t> add_cout_m0 = *ac_m0_opt;
    std::vector<uint64_t> add_cout_m1 = *ac_m1_opt;
    std::vector<uint64_t> add_sum_m0(as_m0_opt->begin(), as_m0_opt->begin() + 5);
    std::vector<uint64_t> add_sum_m1(as_m1_opt->begin(), as_m1_opt->begin() + 5);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    auto add_core = [&](uint64_t s1, uint64_t s2, uint64_t s3,
                        uint64_t sum0, uint64_t /*sum1*/,
                        uint64_t carry0, uint64_t carry1,
                        bool emit_sum_m1,
                        bool emit_carry_m0,
                        bool emit_carry_m1,
                        bool copy_sum_out) {
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };

        e(row_copy_fan(s1, add_cout_m0[0], add_sum_m0[0]));
        e(row_copy_fan(s2, add_cout_m0[1], add_sum_m0[1]));
        e(row_copy_fan(s3, add_cout_m0[2], add_sum_m0[2]));

        if (emit_sum_m1) {
            e(row_copy_fan(s1 + ROWS_PER_MAT, add_cout_m1[0], add_sum_m1[0]));
            e(row_copy_fan(s2 + ROWS_PER_MAT, add_cout_m1[1], add_sum_m1[1]));
            e(row_copy_fan(s3 + ROWS_PER_MAT, add_cout_m1[2], add_sum_m1[2]));
        } else {
            e(row_copy(s1 + ROWS_PER_MAT, add_cout_m1[0]));
            e(row_copy(s2 + ROWS_PER_MAT, add_cout_m1[1]));
            e(row_copy(s3 + ROWS_PER_MAT, add_cout_m1[2]));
        }

        e(maj3(add_cout_m0[0], add_cout_m0[1], add_cout_m0[2]));
        if (emit_carry_m0) e(row_copy(add_cout_m0[0], carry0));

        e(maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]));
        if (emit_carry_m1) e(row_copy(add_cout_m1[0], carry1));

        e(row_copy_fan(add_cout_m1[0], add_sum_m0[3], add_sum_m0[4]));
        e(maj5(add_sum_m0[0], add_sum_m0[1], add_sum_m0[2], add_sum_m0[3], add_sum_m0[4]));
        if (copy_sum_out) e(row_copy(add_sum_m0[0], sum0));

        if (emit_sum_m1) {
            e(row_copy_fan(add_cout_m0[0], add_sum_m1[3], add_sum_m1[4]));
            e(maj5(add_sum_m1[0], add_sum_m1[1], add_sum_m1[2], add_sum_m1[3], add_sum_m1[4]));
            if (copy_sum_out) e(row_copy(add_sum_m1[0], sum0)); // sum_m1 param unused in m0-only variant
        }

        return local;
    };

    auto add_ab0_m0 = [&](uint64_t a, uint64_t b,
                          uint64_t sum0, uint64_t carry0, uint64_t carry1) {
        append(add_core(a, b, zero_m0, sum0, 0, carry0, carry1, false, true, true, true));
    };

    auto add_abc_dual = [&](uint64_t a, uint64_t b, uint64_t c,
                            uint64_t sum0, uint64_t sum1,
                            uint64_t carry0, uint64_t carry1) {
        append(add_core(a, b, c, sum0, sum1, carry0, carry1, true, true, true, true));
    };

    auto add_ab0_dual = [&](uint64_t a, uint64_t b,
                            uint64_t sum0, uint64_t sum1,
                            uint64_t carry0, uint64_t carry1) {
        append(add_core(a, b, zero_m0, sum0, sum1, carry0, carry1, true, true, true, true));
    };

    auto add_ab0_m0_nocarry_fast = [&](uint64_t a, uint64_t b, uint64_t sum0) {
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };
        e(row_copy(a, add_sum_m0[0]));
        e(row_copy(b, add_sum_m0[1]));
        e(row_copy(zero_m0, add_sum_m0[2]));
        e(row_copy(a + ROWS_PER_MAT, add_cout_m1[0]));
        e(row_copy(b + ROWS_PER_MAT, add_cout_m1[1]));
        e(row_copy(zero_m1, add_cout_m1[2]));
        e(maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]));
        e(row_copy_fan(add_cout_m1[0], add_sum_m0[3], add_sum_m0[4]));
        e(maj5(add_sum_m0[0], add_sum_m0[1], add_sum_m0[2], add_sum_m0[3], add_sum_m0[4]));
        e(row_copy(add_sum_m0[0], sum0));
        append(local);
    };

    auto gen_pp_dual = [&](uint64_t a_m0, uint64_t b_m0,
                           uint64_t ppr0, uint64_t ppb0,
                           uint64_t ppr1, uint64_t ppb1) {
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };
        e(row_copy(a_m0, ppr0)); e(row_copy(b_m0, ppb0)); e(op_and(ppr0, ppb0));
        e(row_copy(a_m0 + ROWS_PER_MAT, ppr1)); e(row_copy(b_m0 + ROWS_PER_MAT, ppb1)); e(op_and(ppr1, ppb1));
        return local;
    };

    auto gen_pp_m0 = [&](uint64_t a_m0, uint64_t b_m0, uint64_t ppr0, uint64_t ppb0) {
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };
        e(row_copy(a_m0, ppr0)); e(row_copy(b_m0, ppb0)); e(op_and(ppr0, ppb0));
        return local;
    };

    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            if (j == 0 && i == 0) {
                append(gen_pp_m0(srcA[0], srcB[0], pp_m0_arr[0][0], pp_rhs_m0[0][0]));
                continue;
            }
            append(gen_pp_dual(srcA[i], srcB[j],
                               pp_m0_arr[j][i], pp_rhs_m0[j][i],
                               pp_m1_arr[j][i], pp_rhs_m1[j][i]));
        }
    }

    append(row_copy(pp_m0_arr[0][0], prod_m0[0]));

    add_ab0_m0(pp_m0_arr[0][1], pp_m0_arr[1][0], prod_m0[1], c3a_m0, c3a_m1);
    add_abc_dual(pp_m0_arr[0][2], pp_m0_arr[1][1], c3a_m0, s2a_m0, s2a_m1, c3b_m0, c3b_m1);
    add_ab0_m0(s2a_m0, pp_m0_arr[2][0], prod_m0[2], c4a_m0, c4a_m1);
    add_abc_dual(pp_m0_arr[0][3], pp_m0_arr[1][2], pp_m0_arr[2][1], s3a_m0, s3a_m1, c4b_m0, c4b_m1);
    add_abc_dual(pp_m0_arr[3][0], c3b_m0, c4a_m0, s3b_m0, s3b_m1, c4c_m0, c4c_m1);
    add_ab0_m0(s3a_m0, s3b_m0, prod_m0[3], c5a_m0, c5a_m1);
    add_abc_dual(pp_m0_arr[1][3], pp_m0_arr[2][2], pp_m0_arr[3][1], s4a_m0, s4a_m1, c5b_m0, c5b_m1);
    add_abc_dual(c4b_m0, c4c_m0, c5a_m0, s4b_m0, s4b_m1, c5c_m0, c5c_m1);
    add_ab0_m0(s4a_m0, s4b_m0, prod_m0[4], c6a_m0, c6a_m1);
    add_abc_dual(pp_m0_arr[2][3], pp_m0_arr[3][2], c5b_m0, s5a_m0, s5a_m1, c6b_m0, c6b_m1);
    add_ab0_dual(c5c_m0, c6a_m0, s5b_m0, s5b_m1, c6c_m0, c6c_m1);
    add_ab0_m0(s5a_m0, s5b_m0, prod_m0[5], c7a_m0, c7a_m1);
    add_abc_dual(pp_m0_arr[3][3], c6b_m0, c6c_m0, s6a_m0, s6a_m1, c7b_m0, c7b_m1);
    add_ab0_m0(s6a_m0, c7a_m0, prod_m0[6], overflow_m0, overflow_m1);
    add_ab0_m0_nocarry_fast(c7b_m0, overflow_m0, prod_m0[7]);

    (void)zero_m1;
    return inst_list;
}

static std::vector<uint32_t> mult_4bit_opt_v2_impl(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0,
    bool use_csa_schedule,
    bool use_maj5_via_maj3
) {
    std::vector<uint32_t> inst_list;

    if (srcA.size() != 4 || srcB.size() != 4 || prod_m0.size() != 8) {
        std::cout << "[ERROR]: mult_4bit_opt_v2_impl expects A/B=4 bits and product=8 bits\n";
        return {};
    }

    uint32_t bank = extract_bank(srcA[0]);
    for (int i = 0; i < 4; i++) {
        if (extract_bank(srcA[i]) != bank || extract_bank(srcB[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }
    for (int i = 0; i < 8; i++) {
        if (extract_bank(prod_m0[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    uint32_t src_mat0 = mat_id_from_row(extract_row(srcA[0]));
    uint32_t src_mat1 = src_mat0 + 1;
    for (int i = 0; i < 4; i++) {
        if (mat_id_from_row(extract_row(srcA[i])) != src_mat0 ||
            mat_id_from_row(extract_row(srcB[i])) != src_mat0) {
            std::cout << "[ERROR]: mult_4bit_opt_v2_impl expects srcA/srcB in same MAT\n";
            return {};
        }
    }
    for (int i = 0; i < 8; i++) {
        if (mat_id_from_row(extract_row(prod_m0[i])) != src_mat0) {
            std::cout << "[ERROR]: mult_4bit_opt_v2_impl expects outputs in source MAT\n";
            return {};
        }
    }

    const uint64_t zero_m0 = make_row_addr(bank, global_row_from_mat(src_mat0, g_const_zero_row));
    const uint64_t zero_m1 = make_row_addr(bank, global_row_from_mat(src_mat1, g_const_zero_row));

    std::set<uint32_t> used_rows;
    for (auto x : srcA)    cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : srcB)    cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : prod_m0) cud_internal::mark_used_addr_and_mirror(used_rows, x);
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat0, g_const_zero_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat0, g_const_one_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat1, g_const_zero_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat1, g_const_one_row));

    // Local lambda wrapping cud_internal::alloc_group_rows_anchored using captured context.
    auto local_alloc_anchored = [&](uint32_t mat_id,
                                    const std::vector<size_t>& group_candidates,
                                    uint32_t anchor_global_row
                                    ) -> std::optional<std::vector<uint64_t>> {
        if (mat_id_from_row(anchor_global_row) != mat_id) return std::nullopt;

        for (size_t gi : group_candidates) {
            if (gi >= SUPPORTED_GROUPS.size()) continue;
            const auto& offsets = SUPPORTED_GROUPS[gi].offsets;
            if (offsets.empty()) continue;
            const uint32_t max_off = offsets.back();
            if (max_off >= ROWS_PER_MAT) continue;

            for (uint32_t base = 0; base + max_off < ROWS_PER_MAT; ++base) {
                bool has_anchor = false, all_free = true;
                std::vector<uint32_t> cands;
                cands.reserve(offsets.size());

                for (uint32_t off : offsets) {
                    uint32_t lr = base + off;
                    uint32_t gr = global_row_from_mat(mat_id, lr);
                    if (gr == anchor_global_row) {
                        has_anchor = true;
                    } else if (used_rows.count(gr)) {
                        all_free = false;
                        break;
                    }
                    cands.push_back(gr);
                }
                if (!all_free || !has_anchor) continue;

                std::vector<uint64_t> addrs;
                addrs.reserve(cands.size());
                for (uint32_t gr : cands) {
                    if (gr != anchor_global_row) used_rows.insert(gr);
                    addrs.push_back(make_row_addr(bank, gr));
                }
                return addrs;
            }
        }
        return std::nullopt;
    };

    std::vector<std::vector<uint64_t>> pp_m0_arr(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_m1_arr(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_lhs_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_lhs_m1(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_rhs_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_rhs_m1(4, std::vector<uint64_t>(4));
    bool pp00_direct_to_p0 = false;

    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            if (j == 0 && i == 0) {
                auto g0 = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
                auto g1 = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);
                if (!g0 || !g1) {
                    std::cout << "[ERROR]: No free 2RA scratch group for pp00 in mult_4bit_opt_v2_impl\n";
                    return {};
                }
                pp_lhs_m0[0][0] = (*g0)[0]; pp_rhs_m0[0][0] = (*g0)[1]; pp_m0_arr[0][0] = (*g0)[3];
                pp_lhs_m1[0][0] = (*g1)[0]; pp_rhs_m1[0][0] = (*g1)[1]; pp_m1_arr[0][0] = (*g1)[3];
                pp00_direct_to_p0 = false;
                continue;
            }
            auto g0 = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
            auto g1 = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);
            if (!g0 || !g1) {
                std::cout << "[ERROR]: No free 2RA scratch groups for partial products in mult_4bit_opt_v2_impl\n";
                return {};
            }
            pp_lhs_m0[j][i] = (*g0)[0]; pp_rhs_m0[j][i] = (*g0)[1]; pp_m0_arr[j][i] = (*g0)[3];
            pp_lhs_m1[j][i] = (*g1)[0]; pp_rhs_m1[j][i] = (*g1)[1]; pp_m1_arr[j][i] = (*g1)[3];
        }
    }

    constexpr size_t kMirroredScratchRows = 22;
    auto ls_opt = cud_internal::alloc_mirrored_rows(src_mat0, src_mat1, kMirroredScratchRows, used_rows);
    if (!ls_opt) {
        std::cout << "[ERROR]: No free mirrored scratch rows for mult_4bit_opt_v2_impl\n";
        return {};
    }

    const auto& ls = *ls_opt;
    size_t si = 0;
    auto next_pair = [&](uint64_t& r0, uint64_t& r1) {
        uint32_t lr = ls[si++];
        r0 = make_row_addr(bank, global_row_from_mat(src_mat0, lr));
        r1 = make_row_addr(bank, global_row_from_mat(src_mat1, lr));
    };

    uint64_t s2a_m0=0, s2a_m1=0, c3a_m0=0, c3a_m1=0, c3b_m0=0, c3b_m1=0;
    next_pair(s2a_m0,s2a_m1); next_pair(c3a_m0,c3a_m1); next_pair(c3b_m0,c3b_m1);

    uint64_t s3a_m0=0, s3a_m1=0, c4a_m0=0, c4a_m1=0;
    uint64_t s3b_m0=0, s3b_m1=0, c4b_m0=0, c4b_m1=0, c4c_m0=0, c4c_m1=0;
    next_pair(s3a_m0,s3a_m1); next_pair(c4a_m0,c4a_m1);
    next_pair(s3b_m0,s3b_m1); next_pair(c4b_m0,c4b_m1); next_pair(c4c_m0,c4c_m1);

    uint64_t s4a_m0=0, s4a_m1=0, c5a_m0=0, c5a_m1=0;
    uint64_t s4b_m0=0, s4b_m1=0, c5b_m0=0, c5b_m1=0, c5c_m0=0, c5c_m1=0;
    next_pair(s4a_m0,s4a_m1); next_pair(c5a_m0,c5a_m1);
    next_pair(s4b_m0,s4b_m1); next_pair(c5b_m0,c5b_m1); next_pair(c5c_m0,c5c_m1);

    uint64_t s5a_m0=0, s5a_m1=0, c6a_m0=0, c6a_m1=0;
    uint64_t s5b_m0=0, s5b_m1=0, c6b_m0=0, c6b_m1=0, c6c_m0=0, c6c_m1=0;
    next_pair(s5a_m0,s5a_m1); next_pair(c6a_m0,c6a_m1);
    next_pair(s5b_m0,s5b_m1); next_pair(c6b_m0,c6b_m1); next_pair(c6c_m0,c6c_m1);

    uint64_t s6a_m0=0, s6a_m1=0, c7a_m0=0, c7a_m1=0, c7b_m0=0, c7b_m1=0;
    uint64_t overflow_m0=0, overflow_m1=0;
    next_pair(s6a_m0,s6a_m1); next_pair(c7a_m0,c7a_m1); next_pair(c7b_m0,c7b_m1);
    next_pair(overflow_m0,overflow_m1);

    if (si != kMirroredScratchRows) {
        std::cout << "[ERROR]: Internal scratch allocation mismatch in mult_4bit_opt_v2_impl\n";
        return {};
    }

    auto ac_m0_opt = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
    auto ac_m1_opt = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);
    auto as_m0_opt = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::three_ra_groups(), used_rows);
    auto as_m1_opt = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::three_ra_groups(), used_rows);
    if (!ac_m0_opt || !ac_m1_opt || !as_m0_opt || !as_m1_opt) {
        std::cout << "[ERROR]: No free adder scratch groups for mult_4bit_opt_v2_impl\n";
        return {};
    }

    std::vector<uint64_t> add_cout_m0 = *ac_m0_opt;
    std::vector<uint64_t> add_cout_m1 = *ac_m1_opt;
    std::vector<uint64_t> add_sum_m0(as_m0_opt->begin(), as_m0_opt->begin() + 5);
    std::vector<uint64_t> add_sum_m1(as_m1_opt->begin(), as_m1_opt->begin() + 5);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    cud_internal::ScopedExtraReserved scope(&used_rows);
    bool generation_ok = true;

    auto append_maj5_sum = [&](std::vector<uint32_t>& local,
                               uint64_t s1, uint64_t s2, uint64_t s3,
                               uint64_t s4, uint64_t s5) {
        std::vector<uint32_t> t;
        if (use_maj5_via_maj3) {
            t = maj5_via_maj3_sum_inplace(s1, s2, s3, s4, s5);
        } else {
            t = maj5(s1, s2, s3, s4, s5);
        }
        if (t.empty()) generation_ok = false;
        local.insert(local.end(), t.begin(), t.end());
    };

    // Dead-code lambdas kept for fidelity to original structure.
    auto gen_pp_dual_unused = [&](uint64_t a_m0, uint64_t b_m0,
                                  uint64_t ppr0, uint64_t ppb0,
                                  uint64_t ppr1, uint64_t ppb1) {
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };
        e(row_copy(a_m0, ppr0)); e(row_copy(b_m0, ppb0)); e(op_and(ppr0, ppb0));
        e(row_copy(a_m0 + ROWS_PER_MAT, ppr1)); e(row_copy(b_m0 + ROWS_PER_MAT, ppb1)); e(op_and(ppr1, ppb1));
        return local;
    };
    auto gen_pp_m0_unused = [&](uint64_t a_m0, uint64_t b_m0, uint64_t ppr0, uint64_t ppb0) {
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };
        e(row_copy(a_m0, ppr0)); e(row_copy(b_m0, ppb0)); e(op_and(ppr0, ppb0));
        return local;
    };
    (void)gen_pp_dual_unused;
    (void)gen_pp_m0_unused;

    auto add_core = [&](uint64_t s1, uint64_t s2, uint64_t s3,
                        uint64_t sum0, uint64_t /*sum1*/,
                        uint64_t carry0, uint64_t carry1,
                        bool emit_sum_m1,
                        bool emit_carry_m0,
                        bool emit_carry_m1,
                        bool copy_sum_out) {
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };

        e(row_copy_fan(s1, add_cout_m0[0], add_sum_m0[0]));
        e(row_copy_fan(s2, add_cout_m0[1], add_sum_m0[1]));
        e(row_copy_fan(s3, add_cout_m0[2], add_sum_m0[2]));

        if (emit_sum_m1) {
            e(row_copy_fan(s1 + ROWS_PER_MAT, add_cout_m1[0], add_sum_m1[0]));
            e(row_copy_fan(s2 + ROWS_PER_MAT, add_cout_m1[1], add_sum_m1[1]));
            e(row_copy_fan(s3 + ROWS_PER_MAT, add_cout_m1[2], add_sum_m1[2]));
        } else {
            e(row_copy(s1 + ROWS_PER_MAT, add_cout_m1[0]));
            e(row_copy(s2 + ROWS_PER_MAT, add_cout_m1[1]));
            e(row_copy(s3 + ROWS_PER_MAT, add_cout_m1[2]));
        }

        e(maj3(add_cout_m0[0], add_cout_m0[1], add_cout_m0[2]));
        if (emit_carry_m0) e(row_copy(add_cout_m0[0], carry0));

        e(maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]));
        if (emit_carry_m1) e(row_copy(add_cout_m1[0], carry1));

        e(row_copy_fan(add_cout_m1[0], add_sum_m0[3], add_sum_m0[4]));
        append_maj5_sum(local, add_sum_m0[0], add_sum_m0[1], add_sum_m0[2],
                        add_sum_m0[3], add_sum_m0[4]);
        if (copy_sum_out) e(row_copy(add_sum_m0[0], sum0));

        if (emit_sum_m1) {
            e(row_copy_fan(add_cout_m0[0], add_sum_m1[3], add_sum_m1[4]));
            append_maj5_sum(local, add_sum_m1[0], add_sum_m1[1], add_sum_m1[2],
                            add_sum_m1[3], add_sum_m1[4]);
            if (copy_sum_out) e(row_copy(add_sum_m1[0], sum0));
        }
        return local;
    };

    auto add_ab0_m0 = [&](uint64_t a, uint64_t b,
                          uint64_t sum0, uint64_t carry0, uint64_t carry1) {
        append(add_core(a, b, zero_m0, sum0, 0, carry0, carry1, false, true, true, true));
    };

    auto add_abc_dual = [&](uint64_t a, uint64_t b, uint64_t c,
                            uint64_t sum0, uint64_t sum1,
                            uint64_t carry0, uint64_t carry1) {
        append(add_core(a, b, c, sum0, sum1, carry0, carry1, true, true, true, true));
    };

    auto add_abc_dual_ephemeral_sum = [&](uint64_t a, uint64_t b, uint64_t c,
                                          uint64_t carry0, uint64_t carry1) {
        append(add_core(a, b, c, 0, 0, carry0, carry1, true, true, true, false));
    };

    auto add_ab0_dual_ephemeral_sum = [&](uint64_t a, uint64_t b,
                                          uint64_t carry0, uint64_t carry1) {
        append(add_core(a, b, zero_m0, 0, 0, carry0, carry1, true, true, true, false));
    };

    const uint64_t ephem_sum_m0 = add_sum_m0[0];

    auto alloc_sum_rows_anchored =
        [&](uint64_t sum_out_m0) -> std::optional<std::array<uint64_t, 5>> {
            const uint32_t out_gr = extract_row(sum_out_m0);
            auto grp_opt = local_alloc_anchored(src_mat0, cud_internal::three_ra_groups(), out_gr);
            if (!grp_opt) return std::nullopt;

            std::array<uint64_t, 5> rows{};
            rows[0] = sum_out_m0;
            size_t fill = 1;
            for (uint64_t addr : *grp_opt) {
                if (extract_row(addr) == out_gr) continue;
                if (fill < rows.size()) rows[fill++] = addr;
            }
            if (fill != rows.size()) return std::nullopt;
            return rows;
        };

    auto add_ab0_m0_direct_out = [&](uint64_t a, uint64_t b,
                                     uint64_t sum0, uint64_t carry0, uint64_t carry1,
                                     bool emit_c1 = true) {
        auto rows_opt = alloc_sum_rows_anchored(sum0);
        if (!rows_opt) {
            if (emit_c1) {
                add_ab0_m0(a, b, sum0, carry0, carry1);
            } else {
                append(add_core(a, b, zero_m0, sum0, 0, carry0, carry1, false, true, false, true));
            }
            return;
        }
        const auto rows = *rows_opt;
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };

        e(row_copy_fan(a, add_cout_m0[0], rows[0]));
        e(row_copy_fan(b, add_cout_m0[1], rows[1]));
        e(row_copy_fan(zero_m0, add_cout_m0[2], rows[2]));
        e(row_copy(a + ROWS_PER_MAT, add_cout_m1[0]));
        e(row_copy(b + ROWS_PER_MAT, add_cout_m1[1]));
        e(row_copy(zero_m1, add_cout_m1[2]));
        e(maj3(add_cout_m0[0], add_cout_m0[1], add_cout_m0[2]));
        e(row_copy(add_cout_m0[0], carry0));
        e(maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]));
        if (emit_c1) e(row_copy(add_cout_m1[0], carry1));
        e(row_copy_fan(add_cout_m1[0], rows[3], rows[4]));
        append_maj5_sum(local, rows[0], rows[1], rows[2], rows[3], rows[4]);
        append(local);
    };

    auto add_abc_m0_direct_out = [&](uint64_t a, uint64_t b, uint64_t c,
                                     uint64_t sum0, uint64_t carry0, uint64_t carry1,
                                     bool emit_c1 = true) {
        auto rows_opt = alloc_sum_rows_anchored(sum0);
        if (!rows_opt) {
            append(add_core(a, b, c, sum0, 0, carry0, carry1, false, true, emit_c1, true));
            return;
        }
        const auto rows = *rows_opt;
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };

        e(row_copy_fan(a, add_cout_m0[0], rows[0]));
        e(row_copy_fan(b, add_cout_m0[1], rows[1]));
        e(row_copy_fan(c, add_cout_m0[2], rows[2]));
        e(row_copy(a + ROWS_PER_MAT, add_cout_m1[0]));
        e(row_copy(b + ROWS_PER_MAT, add_cout_m1[1]));
        e(row_copy(c + ROWS_PER_MAT, add_cout_m1[2]));
        e(maj3(add_cout_m0[0], add_cout_m0[1], add_cout_m0[2]));
        e(row_copy(add_cout_m0[0], carry0));
        e(maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]));
        if (emit_c1) e(row_copy(add_cout_m1[0], carry1));
        e(row_copy_fan(add_cout_m1[0], rows[3], rows[4]));
        append_maj5_sum(local, rows[0], rows[1], rows[2], rows[3], rows[4]);
        append(local);
    };
    (void)add_abc_m0_direct_out;

    auto add_ab0_m0_nocarry_fast = [&](uint64_t a, uint64_t b, uint64_t sum0) {
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };
        e(row_copy(a, add_sum_m0[0]));
        e(row_copy(b, add_sum_m0[1]));
        e(row_copy(zero_m0, add_sum_m0[2]));
        e(row_copy(a + ROWS_PER_MAT, add_cout_m1[0]));
        e(row_copy(b + ROWS_PER_MAT, add_cout_m1[1]));
        e(row_copy(zero_m1, add_cout_m1[2]));
        e(maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]));
        e(row_copy_fan(add_cout_m1[0], add_sum_m0[3], add_sum_m0[4]));
        append_maj5_sum(local, add_sum_m0[0], add_sum_m0[1], add_sum_m0[2],
                        add_sum_m0[3], add_sum_m0[4]);
        e(row_copy(add_sum_m0[0], sum0));
        append(local);
    };

    auto add_ab0_m0_nocarry_direct_out = [&](uint64_t a, uint64_t b, uint64_t sum0) {
        auto rows_opt = alloc_sum_rows_anchored(sum0);
        if (!rows_opt) {
            add_ab0_m0_nocarry_fast(a, b, sum0);
            return;
        }
        const auto rows = *rows_opt;
        std::vector<uint32_t> local;
        auto e = [&](const std::vector<uint32_t>& v) { local.insert(local.end(), v.begin(), v.end()); };
        e(row_copy(a, rows[0]));
        e(row_copy(b, rows[1]));
        e(row_copy(zero_m0, rows[2]));
        e(row_copy(a + ROWS_PER_MAT, add_cout_m1[0]));
        e(row_copy(b + ROWS_PER_MAT, add_cout_m1[1]));
        e(row_copy(zero_m1, add_cout_m1[2]));
        e(maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]));
        e(row_copy_fan(add_cout_m1[0], rows[3], rows[4]));
        append_maj5_sum(local, rows[0], rows[1], rows[2], rows[3], rows[4]);
        append(local);
    };

    auto add_ab0_m0_copy_out = [&](uint64_t a, uint64_t b,
                                   uint64_t sum0, uint64_t carry0, uint64_t carry1,
                                   bool emit_c1 = true) {
        append(add_core(a, b, zero_m0, sum0, 0, carry0, carry1, false, true, emit_c1, true));
    };

    auto add_abc_m0_copy_out = [&](uint64_t a, uint64_t b, uint64_t c,
                                   uint64_t sum0, uint64_t carry0, uint64_t carry1,
                                   bool emit_c1 = true) {
        append(add_core(a, b, c, sum0, 0, carry0, carry1, false, true, emit_c1, true));
    };

    auto broadcast_to_rows = [&](uint64_t src, const std::vector<uint64_t>& dests) {
        if (dests.empty()) return;
        if (dests.size() == 1) { append(row_copy(src, dests[0])); return; }
        if (dests.size() == 2) { append(row_copy_fan(src, dests[0], dests[1])); return; }
        if (dests.size() == 3) {
            append(row_copy_fan(src, dests[0], dests[1]));
            append(row_copy(src, dests[2]));
            return;
        }
        size_t k = 0;
        for (; k + 1 < dests.size(); k += 2)
            append(row_copy_fan(src, dests[k], dests[k+1]));
        if (k < dests.size()) append(row_copy(src, dests[k]));
    };

    // Broadcast A operands into lhs rows.
    for (int i = 0; i < 4; i++) {
        std::vector<uint64_t> m0d, m1d;
        for (int j = 0; j < 4; j++) {
            m0d.push_back(pp_lhs_m0[j][i]);
            if (!(j == 0 && i == 0)) m1d.push_back(pp_lhs_m1[j][i]);
        }
        broadcast_to_rows(srcA[i], m0d);
        broadcast_to_rows(srcA[i] + ROWS_PER_MAT, m1d);
    }

    // Broadcast B operands into rhs rows.
    for (int j = 0; j < 4; j++) {
        std::vector<uint64_t> m0d, m1d;
        for (int i = 0; i < 4; i++) {
            m0d.push_back(pp_rhs_m0[j][i]);
            if (!(j == 0 && i == 0)) m1d.push_back(pp_rhs_m1[j][i]);
        }
        broadcast_to_rows(srcB[j], m0d);
        broadcast_to_rows(srcB[j] + ROWS_PER_MAT, m1d);
    }

    // Evaluate ANDs.
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            append(op_and(pp_lhs_m0[j][i], pp_rhs_m0[j][i]));
            if (!(j == 0 && i == 0)) append(op_and(pp_lhs_m1[j][i], pp_rhs_m1[j][i]));
        }
    }

    if (use_csa_schedule) {
        if (!pp00_direct_to_p0) append(row_copy(pp_m0_arr[0][0], prod_m0[0]));

        add_ab0_m0_copy_out(pp_m0_arr[0][1], pp_m0_arr[1][0], prod_m0[1], c3b_m0, c3b_m1);
        add_abc_dual_ephemeral_sum(pp_m0_arr[0][2], pp_m0_arr[1][1], pp_m0_arr[2][0], c3a_m0, c3a_m1);
        add_ab0_m0_copy_out(ephem_sum_m0, c3b_m0, prod_m0[2], c4c_m0, c4c_m1);
        add_abc_dual_ephemeral_sum(pp_m0_arr[0][3], pp_m0_arr[1][2], pp_m0_arr[2][1], c4a_m0, c4a_m1);
        add_abc_dual_ephemeral_sum(ephem_sum_m0, pp_m0_arr[3][0], c3a_m0, c4b_m0, c4b_m1);
        add_ab0_m0_copy_out(ephem_sum_m0, c4c_m0, prod_m0[3], c5c_m0, c5c_m1);
        add_abc_dual_ephemeral_sum(pp_m0_arr[1][3], pp_m0_arr[2][2], pp_m0_arr[3][1], c5a_m0, c5a_m1);
        add_abc_dual_ephemeral_sum(ephem_sum_m0, c4a_m0, c4b_m0, c5b_m0, c5b_m1);
        add_ab0_m0_copy_out(ephem_sum_m0, c5c_m0, prod_m0[4], c6b_m0, c6b_m1);
        add_abc_dual_ephemeral_sum(pp_m0_arr[2][3], pp_m0_arr[3][2], c5a_m0, c6a_m0, c6a_m1);
        add_abc_m0_copy_out(ephem_sum_m0, c5b_m0, c6b_m0, prod_m0[5], c6c_m0, c6c_m1, true);
        add_abc_m0_copy_out(pp_m0_arr[3][3], c6a_m0, c6c_m0, prod_m0[6], prod_m0[7], 0, false);
    } else {
        if (!pp00_direct_to_p0) append(row_copy(pp_m0_arr[0][0], prod_m0[0]));

        add_ab0_m0_direct_out(pp_m0_arr[0][1], pp_m0_arr[1][0], prod_m0[1], c3a_m0, c3a_m1);
        add_abc_dual_ephemeral_sum(pp_m0_arr[0][2], pp_m0_arr[1][1], c3a_m0, c3b_m0, c3b_m1);
        add_ab0_m0_direct_out(ephem_sum_m0, pp_m0_arr[2][0], prod_m0[2], c4a_m0, c4a_m1);
        add_abc_dual(pp_m0_arr[0][3], pp_m0_arr[1][2], pp_m0_arr[2][1], s3a_m0, s3a_m1, c4b_m0, c4b_m1);
        add_abc_dual_ephemeral_sum(pp_m0_arr[3][0], c3b_m0, c4a_m0, c4c_m0, c4c_m1);
        add_ab0_m0_direct_out(s3a_m0, ephem_sum_m0, prod_m0[3], c5a_m0, c5a_m1);
        add_abc_dual(pp_m0_arr[1][3], pp_m0_arr[2][2], pp_m0_arr[3][1], s4a_m0, s4a_m1, c5b_m0, c5b_m1);
        add_abc_dual_ephemeral_sum(c4b_m0, c4c_m0, c5a_m0, c5c_m0, c5c_m1);
        add_ab0_m0_direct_out(s4a_m0, ephem_sum_m0, prod_m0[4], c6a_m0, c6a_m1);
        add_abc_dual(pp_m0_arr[2][3], pp_m0_arr[3][2], c5b_m0, s5a_m0, s5a_m1, c6b_m0, c6b_m1);
        add_ab0_dual_ephemeral_sum(c5c_m0, c6a_m0, c6c_m0, c6c_m1);
        add_ab0_m0_direct_out(s5a_m0, ephem_sum_m0, prod_m0[5], c7a_m0, c7a_m1);
        add_abc_dual_ephemeral_sum(pp_m0_arr[3][3], c6b_m0, c6c_m0, c7b_m0, c7b_m1);
        add_ab0_m0_direct_out(ephem_sum_m0, c7a_m0, prod_m0[6], overflow_m0, overflow_m1);
        add_ab0_m0_nocarry_direct_out(c7b_m0, overflow_m0, prod_m0[7]);
    }

    if (!generation_ok) {
        std::cout << "[ERROR]: mult_4bit_opt_v2_impl failed while generating MAJ5/MAJ3 sum\n";
        return {};
    }

    (void)zero_m1;
    return inst_list;
}

std::vector<uint32_t> mult_4bit_csa(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0
) {
    return mult_4bit_opt_v2_impl(srcA, srcB, prod_m0, true, false);
}

std::vector<uint32_t> mult_4bit_csa_via_maj3(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0
) {
    return mult_4bit_opt_v2_impl(srcA, srcB, prod_m0, true, true);
}

std::vector<uint32_t> mult_4bit_opt_v2(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0
) {
    return mult_4bit_opt_v2_impl(srcA, srcB, prod_m0, false, false);
}

std::vector<uint32_t> mult_nbit(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0
) {
    std::vector<uint32_t> inst_list;

    const size_t n = srcA.size();
    if (n == 0 || srcB.size() != n || prod_m0.size() != (2 * n)) {
        std::cout << "[ERROR]: mult_nbit expects A/B=n bits and product=2n bits\n";
        return {};
    }

    uint32_t bank = extract_bank(srcA[0]);
    for (size_t i = 0; i < n; i++) {
        if (extract_bank(srcA[i]) != bank || extract_bank(srcB[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }
    for (size_t i = 0; i < 2 * n; i++) {
        if (extract_bank(prod_m0[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    uint32_t src_mat0 = mat_id_from_row(extract_row(srcA[0]));
    uint32_t src_mat1 = src_mat0 + 1;
    for (size_t i = 0; i < n; i++) {
        if (mat_id_from_row(extract_row(srcA[i])) != src_mat0 ||
            mat_id_from_row(extract_row(srcB[i])) != src_mat0) {
            std::cout << "[ERROR]: mult_nbit expects srcA/srcB in same MAT\n";
            return {};
        }
    }
    for (size_t i = 0; i < 2 * n; i++) {
        if (mat_id_from_row(extract_row(prod_m0[i])) != src_mat0) {
            std::cout << "[ERROR]: mult_nbit expects outputs in source MAT\n";
            return {};
        }
    }

    const uint64_t zero_m0 = make_row_addr(bank, global_row_from_mat(src_mat0, g_const_zero_row));
    const uint64_t zero_m1 = make_row_addr(bank, global_row_from_mat(src_mat1, g_const_zero_row));

    std::set<uint32_t> used_rows;
    for (auto x : srcA)    cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : srcB)    cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : prod_m0) cud_internal::mark_used_addr(used_rows, x);
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat0, g_const_zero_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat0, g_const_one_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat1, g_const_zero_row));
    cud_internal::mark_used_row(used_rows, global_row_from_mat(src_mat1, g_const_one_row));

    std::vector<uint64_t> pp_lhs_m0(n), pp_rhs_m0(n);
    std::vector<uint64_t> pp_lhs_m1(n), pp_rhs_m1(n);
    for (size_t i = 0; i < n; i++) {
        auto g0 = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
        auto g1 = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);
        if (!g0 || !g1) {
            std::cout << "[ERROR]: No free 2RA scratch groups for mult_nbit partial products\n";
            return {};
        }
        pp_lhs_m0[i] = (*g0)[0]; pp_rhs_m0[i] = (*g0)[1];
        pp_lhs_m1[i] = (*g1)[0]; pp_rhs_m1[i] = (*g1)[1];
    }

    const size_t mirrored_needed = (6 * n) + 2;
    auto ls_opt = cud_internal::alloc_mirrored_rows(src_mat0, src_mat1, mirrored_needed, used_rows);
    if (!ls_opt) {
        std::cout << "[ERROR]: No free mirrored scratch rows for mult_nbit\n";
        return {};
    }

    const auto& ls = *ls_opt;
    size_t si = 0;
    auto next_row_m0 = [&]() -> uint64_t {
        return make_row_addr(bank, global_row_from_mat(src_mat0, ls[si++]));
    };

    std::vector<uint64_t> acc_a_m0(2*n), acc_b_m0(2*n), term_m0(2*n);
    for (size_t k = 0; k < 2*n; k++) acc_a_m0[k] = next_row_m0();
    for (size_t k = 0; k < 2*n; k++) acc_b_m0[k] = next_row_m0();
    for (size_t k = 0; k < 2*n; k++) term_m0[k]  = next_row_m0();
    uint64_t carry0_m0 = next_row_m0();
    uint64_t carry1_m0 = next_row_m0();

    if (si != mirrored_needed) {
        std::cout << "[ERROR]: Internal scratch allocation mismatch in mult_nbit\n";
        return {};
    }

    auto to_m1 = [&](uint64_t addr_m0) -> uint64_t {
        return make_row_addr(bank, extract_row(addr_m0) + ROWS_PER_MAT);
    };

    std::vector<uint64_t> acc_a_m1(2*n), acc_b_m1(2*n), term_m1(2*n);
    for (size_t k = 0; k < 2*n; k++) { acc_a_m1[k] = to_m1(acc_a_m0[k]); acc_b_m1[k] = to_m1(acc_b_m0[k]); term_m1[k] = to_m1(term_m0[k]); }
    uint64_t carry0_m1 = to_m1(carry0_m0);
    uint64_t carry1_m1 = to_m1(carry1_m0);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    cud_internal::ScopedExtraReserved guard(&used_rows);

    for (size_t k = 0; k < 2*n; k++) {
        append(row_copy(zero_m0, acc_a_m0[k]));
        append(row_copy(zero_m1, acc_a_m1[k]));
    }

    for (size_t j = 0; j < n; j++) {
        for (size_t i = 0; i < n; i++) {
            append(row_copy(srcA[i], pp_lhs_m0[i]));
            append(row_copy(srcB[j], pp_rhs_m0[i]));
            append(op_and(pp_lhs_m0[i], pp_rhs_m0[i]));
            append(row_copy(srcA[i] + ROWS_PER_MAT, pp_lhs_m1[i]));
            append(row_copy(srcB[j] + ROWS_PER_MAT, pp_rhs_m1[i]));
            append(op_and(pp_lhs_m1[i], pp_rhs_m1[i]));
        }

        for (size_t k = 0; k < 2*n; k++) {
            append(row_copy(zero_m0, term_m0[k]));
            append(row_copy(zero_m1, term_m1[k]));
        }

        for (size_t i = 0; i < n; i++) {
            const size_t k = i + j;
            append(row_copy(pp_lhs_m0[i], term_m0[k]));
            append(row_copy(pp_lhs_m1[i], term_m1[k]));
        }

        uint64_t carry_in_m0 = zero_m0;
        bool use_carry0 = true;

        for (size_t k = 0; k < 2*n; k++) {
            uint64_t c_out_m0 = use_carry0 ? carry0_m0 : carry1_m0;
            uint64_t c_out_m1 = use_carry0 ? carry0_m1 : carry1_m1;

            auto stage = add_new(acc_a_m0[k], term_m0[k], carry_in_m0,
                                 acc_b_m0[k], acc_b_m1[k], c_out_m0, c_out_m1);
            if (stage.empty()) {
                std::cout << "[ERROR]: mult_nbit failed while invoking add_new stage\n";
                return {};
            }
            append(stage);

            carry_in_m0 = c_out_m0;
            use_carry0 = !use_carry0;
        }

        std::swap(acc_a_m0, acc_b_m0);
        std::swap(acc_a_m1, acc_b_m1);
    }

    for (size_t k = 0; k < 2*n; k++)
        append(row_copy(acc_a_m0[k], prod_m0[k]));

    return inst_list;
}
