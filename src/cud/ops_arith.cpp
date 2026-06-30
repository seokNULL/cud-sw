#include "internal.h"
#include "../../include/cud/ops.h"
#include "../../include/cud/types.h"
#include <iostream>
#include <set>

std::vector<uint32_t> add_new(
    uint64_t src1, uint64_t src2, uint64_t src3,
    uint64_t sum_m0, uint64_t sum_m1,
    uint64_t carry_m0, uint64_t carry_m1)
{
    std::vector<uint32_t> inst_list;

    uint32_t bank = extract_bank(src1);
    if (extract_bank(src2) != bank || extract_bank(src3) != bank ||
        extract_bank(sum_m0) != bank || extract_bank(sum_m1) != bank ||
        extract_bank(carry_m0) != bank || extract_bank(carry_m1) != bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return {};
    }

    uint32_t src_mat0 = mat_id_from_row(extract_row(src1));
    uint32_t src_mat1 = src_mat0 + 1;

    if (mat_id_from_row(extract_row(src2)) != src_mat0 ||
        mat_id_from_row(extract_row(src3)) != src_mat0) {
        std::cout << "[ERROR]: add_new expects src1/src2/src3 in same MAT" << std::endl;
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
    if (cud_internal::g_extra_reserved != nullptr) {
        used_rows.insert(cud_internal::g_extra_reserved->begin(),
                         cud_internal::g_extra_reserved->end());
    }

    auto cg0_opt = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
    auto cg1_opt = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);
    auto sg0_opt  = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::three_ra_groups(), used_rows);
    auto sg1_opt  = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::three_ra_groups(), used_rows);

    if (!cg0_opt || !cg1_opt || !sg0_opt || !sg1_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for add_new" << std::endl;
        return {};
    }

    const auto& cg0 = *cg0_opt;
    const auto& cg1 = *cg1_opt;
    std::vector<uint64_t> sg0(sg0_opt->begin(), sg0_opt->begin() + 5);
    std::vector<uint64_t> sg1(sg1_opt->begin(), sg1_opt->begin() + 5);

    cud_internal::ScopedExtraReserved guard(&used_rows);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    // Carry in MAT0
    append(row_copy(src1, cg0[0]));
    append(row_copy(src2, cg0[1]));
    append(row_copy(src3, cg0[2]));
    append(maj3(cg0[0], cg0[1], cg0[2]));
    append(row_copy(cg0[0], carry_m0));

    // Carry in MAT1
    append(row_copy(src1 + ROWS_PER_MAT, cg1[0]));
    append(row_copy(src2 + ROWS_PER_MAT, cg1[1]));
    append(row_copy(src3 + ROWS_PER_MAT, cg1[2]));
    append(maj3(cg1[0], cg1[1], cg1[2]));
    append(row_copy(cg1[0], carry_m1));

    // SUM in MAT0 via ~carry from MAT1
    append(row_copy(src1, sg0[0]));
    append(row_copy(src2, sg0[1]));
    append(row_copy(src3, sg0[2]));
    append(op_not(cg1[0], sg0[3]));
    append(row_copy(sg0[3], sg0[4]));
    append(maj5(sg0[0], sg0[1], sg0[2], sg0[3], sg0[4]));
    append(row_copy(sg0[0], sum_m0));

    // SUM in MAT1 via ~carry from MAT0
    append(row_copy(src1 + ROWS_PER_MAT, sg1[0]));
    append(row_copy(src2 + ROWS_PER_MAT, sg1[1]));
    append(row_copy(src3 + ROWS_PER_MAT, sg1[2]));
    append(op_not(cg0[0], sg1[3]));
    append(row_copy(sg1[3], sg1[4]));
    append(maj5(sg1[0], sg1[1], sg1[2], sg1[3], sg1[4]));
    append(row_copy(sg1[0], sum_m1));

    return inst_list;
}

std::vector<uint32_t> add_4bit(
    std::vector<uint64_t> src1, std::vector<uint64_t> src2,
    uint64_t src3, std::vector<uint64_t> sum, uint64_t c_out)
{
    std::vector<uint32_t> inst_list;

    if (src1.size() != 4 || src2.size() != 4 || sum.size() != 4) {
        std::cout << "[ERROR]: add_4bit expects 4-bit vectors" << std::endl;
        return {};
    }
    uint32_t bank = extract_bank(src1[0]);
    for (int i = 0; i < 4; i++) {
        if (extract_bank(src1[i]) != bank || extract_bank(src2[i]) != bank ||
            extract_bank(sum[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
            return {};
        }
    }
    if (extract_bank(src3) != bank || extract_bank(c_out) != bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return {};
    }

    uint32_t src_mat0 = mat_id_from_row(extract_row(src1[0]));
    uint32_t src_mat1 = src_mat0 + 1;

    for (int i = 0; i < 4; i++) {
        if (mat_id_from_row(extract_row(src1[i])) != src_mat0 ||
            mat_id_from_row(extract_row(src2[i])) != src_mat0) {
            std::cout << "[ERROR]: add_4bit expects src vectors in same MAT" << std::endl;
            return {};
        }
    }
    if (mat_id_from_row(extract_row(src3)) != src_mat0) {
        std::cout << "[ERROR]: add_4bit expects src3 in same MAT as src vectors" << std::endl;
        return {};
    }

    std::set<uint32_t> used_rows;
    for (auto x : src1) cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : src2) cud_internal::mark_used_addr_and_mirror(used_rows, x);
    cud_internal::mark_used_addr_and_mirror(used_rows, src3);
    for (auto x : sum) cud_internal::mark_used_addr(used_rows, x);
    cud_internal::mark_used_addr(used_rows, c_out);
    if (cud_internal::g_extra_reserved != nullptr) {
        used_rows.insert(cud_internal::g_extra_reserved->begin(),
                         cud_internal::g_extra_reserved->end());
    }

    auto cout_opt  = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
    auto sum_opt   = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::three_ra_groups(), used_rows);
    auto cout1_opt = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);

    if (!cout_opt || !sum_opt || !cout1_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for add_4bit" << std::endl;
        return {};
    }

    const auto& cout_group  = *cout_opt;
    const auto& sum_group   = *sum_opt;
    const auto& cout_group1 = *cout1_opt;
    cud_internal::ScopedExtraReserved guard(&used_rows);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    const uint32_t c_out_mat = mat_id_from_row(extract_row(c_out));
    const bool use_mat0_final_cout = (c_out_mat == src_mat0);

    auto add_stage = [&](int bit, bool final_stage) {
        const uint64_t a0 = src1[bit];
        const uint64_t b0 = src2[bit];
        const uint64_t a1 = a0 + ROWS_PER_MAT;
        const uint64_t b1 = b0 + ROWS_PER_MAT;
        const bool skip_mat1 = final_stage && use_mat0_final_cout;

        append(row_copy(a0, cout_group[0]));
        append(row_copy(b0, cout_group[1]));

        if (skip_mat1) {
            append(row_copy(a1, sum_group[0]));
            append(row_copy(b1, sum_group[1]));
        } else {
            append(row_copy_fan(a1, sum_group[0], cout_group1[0]));
            append(row_copy_fan(b1, sum_group[1], cout_group1[1]));
        }

        if (bit == 0) {
            append(row_copy(src3, cout_group[2]));
            if (skip_mat1) {
                append(row_copy(src3 + ROWS_PER_MAT, sum_group[2]));
            } else {
                append(row_copy_fan(src3 + ROWS_PER_MAT, sum_group[2], cout_group1[2]));
            }
        }

        append(maj3(cout_group[0], cout_group[1], cout_group[2]));
        append(row_copy_fan(cout_group[0], sum_group[3], sum_group[4]));
        append(maj5(sum_group[0], sum_group[1], sum_group[2], sum_group[3], sum_group[4]));
        append(row_copy(sum_group[0], sum[bit]));

        if (skip_mat1) {
            append(row_copy(cout_group[0], c_out));
            return;
        }

        append(maj3(cout_group1[0], cout_group1[1], cout_group1[2]));
        if (!final_stage) {
            append(row_copy(cout_group1[0], sum_group[2]));
        } else {
            append(row_copy(cout_group1[0], c_out));
        }
    };

    for (int i = 0; i < 4; ++i) {
        add_stage(i, i == 3);
    }
    return inst_list;
}

std::vector<uint32_t> add_4bit_or_sum(
    std::vector<uint64_t> src1, std::vector<uint64_t> src2,
    uint64_t src3, std::vector<uint64_t> sum, uint64_t c_out)
{
    std::vector<uint32_t> inst_list;

    if (src1.size() != 4 || src2.size() != 4 || sum.size() != 4) {
        std::cout << "[ERROR]: add_4bit_or_sum expects 4-bit vectors" << std::endl;
        return {};
    }
    uint32_t bank = extract_bank(src1[0]);
    for (int i = 0; i < 4; i++) {
        if (extract_bank(src1[i]) != bank || extract_bank(src2[i]) != bank ||
            extract_bank(sum[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
            return {};
        }
    }
    if (extract_bank(src3) != bank || extract_bank(c_out) != bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return {};
    }

    uint32_t src_mat0 = mat_id_from_row(extract_row(src1[0]));
    uint32_t src_mat1 = src_mat0 + 1;
    const uint64_t one_m1 = make_row_addr(bank, global_row_from_mat(src_mat1, g_const_one_row));

    for (int i = 0; i < 4; i++) {
        if (mat_id_from_row(extract_row(src1[i])) != src_mat0 ||
            mat_id_from_row(extract_row(src2[i])) != src_mat0) {
            std::cout << "[ERROR]: add_4bit_or_sum expects src vectors in same MAT" << std::endl;
            return {};
        }
    }
    if (mat_id_from_row(extract_row(src3)) != src_mat0) {
        std::cout << "[ERROR]: add_4bit_or_sum expects src3 in same MAT as src vectors" << std::endl;
        return {};
    }

    std::set<uint32_t> used_rows;
    for (auto x : src1) cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : src2) cud_internal::mark_used_addr_and_mirror(used_rows, x);
    cud_internal::mark_used_addr_and_mirror(used_rows, src3);
    for (auto x : sum) cud_internal::mark_used_addr(used_rows, x);
    cud_internal::mark_used_addr(used_rows, c_out);

    auto cout_opt  = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
    auto sum_opt   = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::three_ra_groups(), used_rows);
    auto cout1_opt = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);

    if (!cout_opt || !sum_opt || !cout1_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for add_4bit_or_sum" << std::endl;
        return {};
    }

    const auto& cout_group  = *cout_opt;
    const auto& sum_group   = *sum_opt;
    const auto& cout_group1 = *cout1_opt;
    cud_internal::ScopedExtraReserved guard(&used_rows);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    const uint32_t c_out_mat = mat_id_from_row(extract_row(c_out));
    const bool use_mat0_final_cout = (c_out_mat == src_mat0);

    auto add_stage = [&](int bit, bool final_stage) {
        const uint64_t a0 = src1[bit];
        const uint64_t b0 = src2[bit];
        const uint64_t a1 = a0 + ROWS_PER_MAT;
        const uint64_t b1 = b0 + ROWS_PER_MAT;
        const bool skip_mat1 = final_stage && use_mat0_final_cout;

        append(row_copy(a0, cout_group[0]));
        append(row_copy(b0, cout_group[1]));

        if (skip_mat1) {
            append(row_copy(a1, sum_group[0]));
            append(row_copy(b1, sum_group[1]));
        } else {
            append(row_copy_fan(a1, sum_group[0], cout_group1[0]));
            append(row_copy_fan(b1, sum_group[1], cout_group1[1]));
        }

        if (bit == 0) {
            append(row_copy(src3, cout_group[2]));
            if (skip_mat1) {
                append(row_copy(src3 + ROWS_PER_MAT, sum_group[2]));
            } else {
                append(row_copy_fan(src3 + ROWS_PER_MAT, sum_group[2], cout_group1[2]));
            }
        }

        append(maj3(cout_group[0], cout_group[1], cout_group[2]));
        append(row_copy_fan(one_m1, sum_group[3], sum_group[4]));
        append(maj5(sum_group[0], sum_group[1], sum_group[2], sum_group[3], sum_group[4]));
        append(row_copy(sum_group[0], sum[bit]));

        if (skip_mat1) {
            append(row_copy(cout_group[0], c_out));
            return;
        }

        append(maj3(cout_group1[0], cout_group1[1], cout_group1[2]));
        if (!final_stage) {
            append(row_copy(cout_group1[0], sum_group[2]));
        } else {
            append(row_copy(cout_group1[0], c_out));
        }
    };

    for (int i = 0; i < 4; ++i) {
        add_stage(i, i == 3);
    }
    return inst_list;
}

std::vector<uint32_t> add_nbit(
    std::vector<uint64_t> src1, std::vector<uint64_t> src2,
    uint64_t src3, std::vector<uint64_t> sum, uint64_t c_out)
{
    std::vector<uint32_t> inst_list;

    const size_t n = src1.size();
    if (n == 0 || src2.size() != n || sum.size() != n) {
        std::cout << "[ERROR]: add_nbit expects non-empty equal-size vectors" << std::endl;
        return {};
    }
    uint32_t bank = extract_bank(src1[0]);
    for (size_t i = 0; i < n; i++) {
        if (extract_bank(src1[i]) != bank || extract_bank(src2[i]) != bank ||
            extract_bank(sum[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
            return {};
        }
    }
    if (extract_bank(src3) != bank || extract_bank(c_out) != bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return {};
    }

    uint32_t src_mat0 = mat_id_from_row(extract_row(src1[0]));
    uint32_t src_mat1 = src_mat0 + 1;

    for (size_t i = 0; i < n; i++) {
        if (mat_id_from_row(extract_row(src1[i])) != src_mat0 ||
            mat_id_from_row(extract_row(src2[i])) != src_mat0) {
            std::cout << "[ERROR]: add_nbit expects src vectors in same MAT" << std::endl;
            return {};
        }
    }
    if (mat_id_from_row(extract_row(src3)) != src_mat0) {
        std::cout << "[ERROR]: add_nbit expects src3 in same MAT as src vectors" << std::endl;
        return {};
    }

    std::set<uint32_t> used_rows;
    for (auto x : src1) cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : src2) cud_internal::mark_used_addr_and_mirror(used_rows, x);
    cud_internal::mark_used_addr_and_mirror(used_rows, src3);
    for (auto x : sum) cud_internal::mark_used_addr(used_rows, x);
    cud_internal::mark_used_addr(used_rows, c_out);

    auto cout_opt  = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
    auto sum_opt   = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::three_ra_groups(), used_rows);
    auto cout1_opt = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);

    if (!cout_opt || !sum_opt || !cout1_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for add_nbit" << std::endl;
        return {};
    }

    const auto& cout_group  = *cout_opt;
    const auto& sum_group   = *sum_opt;
    const auto& cout_group1 = *cout1_opt;
    cud_internal::ScopedExtraReserved guard(&used_rows);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    const uint32_t c_out_mat = mat_id_from_row(extract_row(c_out));
    const bool use_mat0_final_cout = (c_out_mat == src_mat0);

    auto add_stage = [&](size_t bit, bool final_stage) {
        const uint64_t a0 = src1[bit];
        const uint64_t b0 = src2[bit];
        const uint64_t a1 = a0 + ROWS_PER_MAT;
        const uint64_t b1 = b0 + ROWS_PER_MAT;
        const bool skip_mat1 = final_stage && use_mat0_final_cout;

        append(row_copy(a0, cout_group[0]));
        append(row_copy(b0, cout_group[1]));

        if (skip_mat1) {
            append(row_copy(a1, sum_group[0]));
            append(row_copy(b1, sum_group[1]));
        } else {
            append(row_copy_fan(a1, sum_group[0], cout_group1[0]));
            append(row_copy_fan(b1, sum_group[1], cout_group1[1]));
        }

        if (bit == 0) {
            append(row_copy(src3, cout_group[2]));
            if (skip_mat1) {
                append(row_copy(src3 + ROWS_PER_MAT, sum_group[2]));
            } else {
                append(row_copy_fan(src3 + ROWS_PER_MAT, sum_group[2], cout_group1[2]));
            }
        }

        append(maj3(cout_group[0], cout_group[1], cout_group[2]));
        append(row_copy_fan(cout_group[0], sum_group[3], sum_group[4]));
        append(maj5(sum_group[0], sum_group[1], sum_group[2], sum_group[3], sum_group[4]));
        append(row_copy(sum_group[0], sum[bit]));

        if (skip_mat1) {
            append(row_copy(cout_group[0], c_out));
            return;
        }

        append(maj3(cout_group1[0], cout_group1[1], cout_group1[2]));
        if (!final_stage) {
            append(row_copy(cout_group1[0], sum_group[2]));
        } else {
            append(row_copy(cout_group1[0], c_out));
        }
    };

    for (size_t i = 0; i < n; ++i) {
        add_stage(i, i == n - 1);
    }
    return inst_list;
}

std::vector<uint32_t> add_nbit_via_maj3(
    std::vector<uint64_t> src1, std::vector<uint64_t> src2,
    uint64_t src3, std::vector<uint64_t> sum, uint64_t c_out)
{
    std::vector<uint32_t> inst_list;

    const size_t n = src1.size();
    if (n == 0 || src2.size() != n || sum.size() != n) {
        std::cout << "[ERROR]: add_nbit_via_maj3 expects non-empty equal-size vectors" << std::endl;
        return {};
    }
    uint32_t bank = extract_bank(src1[0]);
    for (size_t i = 0; i < n; i++) {
        if (extract_bank(src1[i]) != bank || extract_bank(src2[i]) != bank ||
            extract_bank(sum[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
            return {};
        }
    }
    if (extract_bank(src3) != bank || extract_bank(c_out) != bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return {};
    }

    uint32_t src_mat0 = mat_id_from_row(extract_row(src1[0]));
    uint32_t src_mat1 = src_mat0 + 1;

    for (size_t i = 0; i < n; i++) {
        if (mat_id_from_row(extract_row(src1[i])) != src_mat0 ||
            mat_id_from_row(extract_row(src2[i])) != src_mat0) {
            std::cout << "[ERROR]: add_nbit_via_maj3 expects src vectors in same MAT" << std::endl;
            return {};
        }
    }
    if (mat_id_from_row(extract_row(src3)) != src_mat0) {
        std::cout << "[ERROR]: add_nbit_via_maj3 expects src3 in same MAT as src vectors" << std::endl;
        return {};
    }

    std::set<uint32_t> used_rows;
    for (auto x : src1) cud_internal::mark_used_addr_and_mirror(used_rows, x);
    for (auto x : src2) cud_internal::mark_used_addr_and_mirror(used_rows, x);
    cud_internal::mark_used_addr_and_mirror(used_rows, src3);
    for (auto x : sum) cud_internal::mark_used_addr(used_rows, x);
    cud_internal::mark_used_addr(used_rows, c_out);

    auto cout_opt  = cud_internal::alloc_group_rows(bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
    auto sum_opt   = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::three_ra_groups(), used_rows);
    auto cout1_opt = cud_internal::alloc_group_rows(bank, src_mat1, cud_internal::two_ra_groups(), used_rows);

    if (!cout_opt || !sum_opt || !cout1_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for add_nbit_via_maj3" << std::endl;
        return {};
    }

    const auto& cout_group  = *cout_opt;
    const auto& sum_group   = *sum_opt;
    const auto& cout_group1 = *cout1_opt;
    cud_internal::ScopedExtraReserved guard(&used_rows);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    const uint32_t c_out_mat = mat_id_from_row(extract_row(c_out));
    const bool use_mat0_final_cout = (c_out_mat == src_mat0);

    auto add_stage = [&](size_t bit, bool final_stage) {
        const uint64_t a0 = src1[bit];
        const uint64_t b0 = src2[bit];
        const uint64_t a1 = a0 + ROWS_PER_MAT;
        const uint64_t b1 = b0 + ROWS_PER_MAT;
        const bool skip_mat1 = final_stage && use_mat0_final_cout;

        append(row_copy(a0, cout_group[0]));
        append(row_copy(b0, cout_group[1]));

        if (skip_mat1) {
            append(row_copy(a1, sum_group[0]));
            append(row_copy(b1, sum_group[1]));
        } else {
            append(row_copy_fan(a1, sum_group[0], cout_group1[0]));
            append(row_copy_fan(b1, sum_group[1], cout_group1[1]));
        }

        if (bit == 0) {
            append(row_copy(src3, cout_group[2]));
            if (skip_mat1) {
                append(row_copy(src3 + ROWS_PER_MAT, sum_group[2]));
            } else {
                append(row_copy_fan(src3 + ROWS_PER_MAT, sum_group[2], cout_group1[2]));
            }
        }

        append(maj3(cout_group[0], cout_group[1], cout_group[2]));
        append(row_copy_fan(cout_group[0], sum_group[3], sum_group[4]));
        append(maj5_via_maj3(sum_group[0], sum_group[1], sum_group[2],
                              sum_group[3], sum_group[4], sum_group[0]));
        append(row_copy(sum_group[0], sum[bit]));

        if (skip_mat1) {
            append(row_copy(cout_group[0], c_out));
            return;
        }

        append(maj3(cout_group1[0], cout_group1[1], cout_group1[2]));
        if (!final_stage) {
            append(row_copy(cout_group1[0], sum_group[2]));
        } else {
            append(row_copy(cout_group1[0], c_out));
        }
    };

    for (size_t i = 0; i < n; ++i) {
        add_stage(i, i == n - 1);
    }
    return inst_list;
}

std::vector<uint32_t> add_legacy(uint64_t src1, uint64_t src2, uint64_t src3)
{
    std::vector<uint32_t> inst_list;

    uint32_t src1_bank = extract_bank(src1);
    uint32_t src2_bank = extract_bank(src2);
    uint32_t src3_bank = extract_bank(src3);

    if (src1_bank != src2_bank || src2_bank != src3_bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return inst_list;
    }

    uint32_t src1_row = extract_row(src1);
    uint32_t src2_row = extract_row(src2);
    uint32_t src3_row = extract_row(src3);

    uint32_t src_mat0 = mat_id_from_row(src1_row);
    uint32_t src_mat1 = src_mat0 + 1;

    if (mat_id_from_row(src2_row) != src_mat0 ||
        mat_id_from_row(src3_row) != src_mat0) {
        std::cout << "[ERROR]: add_legacy expects all sources in same MAT" << std::endl;
        return inst_list;
    }

    std::set<uint32_t> used_rows;
    cud_internal::mark_used_addr_and_mirror(used_rows, src1);
    cud_internal::mark_used_addr_and_mirror(used_rows, src2);
    cud_internal::mark_used_addr_and_mirror(used_rows, src3);

    auto cout_opt = cud_internal::alloc_group_rows(src1_bank, src_mat0, cud_internal::two_ra_groups(), used_rows);
    auto sum_opt  = cud_internal::alloc_group_rows(src1_bank, src_mat1, cud_internal::three_ra_groups(), used_rows);

    if (!cout_opt || !sum_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for add_legacy" << std::endl;
        return inst_list;
    }

    const auto& cout_group = *cout_opt;
    const auto& sum_group  = *sum_opt;

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    append(row_copy(src1 + ROWS_PER_MAT, sum_group[0]));
    append(row_copy(src1, cout_group[0]));
    append(row_copy(src2 + ROWS_PER_MAT, sum_group[1]));
    append(row_copy(src2, cout_group[1]));
    append(row_copy(src3 + ROWS_PER_MAT, sum_group[2]));
    append(row_copy(src3, cout_group[2]));
    append(maj3(cout_group[0], cout_group[1], cout_group[2]));
    append(op_not(cout_group[0], sum_group[3]));
    append(row_copy(sum_group[3], sum_group[4]));
    append(maj5(sum_group[0], sum_group[1], sum_group[2], sum_group[3], sum_group[4]));

    return inst_list;
}
