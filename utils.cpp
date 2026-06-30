// #include "instruction.h"
#include <cstdint>
#include <iostream>
#include <vector>
#include <algorithm>
#include <array>
#include "include/instruction.h"


uint32_t temp_row_1 = 1183; //to store 0xfffffffff
uint32_t temp_row_0 = 1182; //to store 0x000000000

namespace {

constexpr uint32_t MAX_ROW_ADDR = (1u << 17) - 1;
const std::set<uint32_t>* g_add_new_extra_reserved_rows = nullptr;

const std::vector<size_t>& two_ra_group_indices() {
    static const std::vector<size_t> kIndices = {0, 1, 2, 3, 4, 5, 6};
    return kIndices;
}

const std::vector<size_t>& three_ra_group_indices() {
    static const std::vector<size_t> kIndices = {7, 8, 9, 10, 11, 12, 13, 14};
    return kIndices;
}

void reserve_row(std::set<uint32_t>& used_rows, uint32_t global_row) {
    if (global_row <= MAX_ROW_ADDR) {
        used_rows.insert(global_row);
    }
}

void reserve_addr(std::set<uint32_t>& used_rows, uint64_t addr) {
    reserve_row(used_rows, extract_row_addr(addr));
}

void reserve_addr_with_next_mat(std::set<uint32_t>& used_rows, uint64_t addr) {
    uint32_t global_row = extract_row_addr(addr);
    reserve_row(used_rows, global_row);
    if (global_row + ROWS_PER_MAT <= MAX_ROW_ADDR) {
        reserve_row(used_rows, global_row + ROWS_PER_MAT);
    }
}

std::optional<std::vector<uint64_t>> allocate_group_rows(
    uint32_t bank,
    uint32_t mat_id,
    const std::vector<size_t>& group_candidates,
    std::set<uint32_t>& used_rows
) {
    for (size_t group_index : group_candidates) {
        if (group_index >= SUPPORTED_GROUPS.size()) {
            continue;
        }

        const auto& offsets = SUPPORTED_GROUPS[group_index].offsets;
        if (offsets.empty()) {
            continue;
        }

        uint32_t max_offset = offsets.back();
        if (max_offset >= ROWS_PER_MAT) {
            continue;
        }

        for (uint32_t base_local = 0; base_local + max_offset < ROWS_PER_MAT; ++base_local) {
            bool free = true;
            std::vector<uint32_t> candidate_rows;
            candidate_rows.reserve(offsets.size());

            for (uint32_t offset : offsets) {
                uint32_t global_row = make_global_row(mat_id, base_local + offset);
                if (used_rows.find(global_row) != used_rows.end()) {
                    free = false;
                    break;
                }
                candidate_rows.push_back(global_row);
            }

            if (!free) {
                continue;
            }

            std::vector<uint64_t> addrs;
            addrs.reserve(candidate_rows.size());
            for (uint32_t global_row : candidate_rows) {
                used_rows.insert(global_row);
                addrs.push_back(make_addr(bank, global_row));
            }
            return addrs;
        }
    }

    return std::nullopt;
}

std::optional<std::vector<uint64_t>> allocate_group_rows_with_anchor(
    uint32_t bank,
    uint32_t mat_id,
    const std::vector<size_t>& group_candidates,
    uint32_t anchor_global_row,
    std::set<uint32_t>& used_rows
) {
    if (get_mat_id_from_row(anchor_global_row) != mat_id) {
        return std::nullopt;
    }

    for (size_t group_index : group_candidates) {
        if (group_index >= SUPPORTED_GROUPS.size()) {
            continue;
        }

        const auto& offsets = SUPPORTED_GROUPS[group_index].offsets;
        if (offsets.empty()) {
            continue;
        }

        uint32_t max_offset = offsets.back();
        if (max_offset >= ROWS_PER_MAT) {
            continue;
        }

        for (uint32_t base_local = 0; base_local + max_offset < ROWS_PER_MAT; ++base_local) {
            bool contains_anchor = false;
            bool free = true;
            std::vector<uint32_t> candidate_rows;
            candidate_rows.reserve(offsets.size());

            for (uint32_t offset : offsets) {
                uint32_t global_row = make_global_row(mat_id, base_local + offset);
                if (global_row == anchor_global_row) {
                    contains_anchor = true;
                } else if (used_rows.find(global_row) != used_rows.end()) {
                    free = false;
                    break;
                }
                candidate_rows.push_back(global_row);
            }

            if (!free || !contains_anchor) {
                continue;
            }

            std::vector<uint64_t> addrs;
            addrs.reserve(candidate_rows.size());
            for (uint32_t global_row : candidate_rows) {
                if (global_row != anchor_global_row) {
                    used_rows.insert(global_row);
                }
                addrs.push_back(make_addr(bank, global_row));
            }
            return addrs;
        }
    }

    return std::nullopt;
}

std::optional<std::vector<uint32_t>> allocate_mirrored_local_rows(
    uint32_t mat0_id,
    uint32_t mat1_id,
    size_t needed_count,
    std::set<uint32_t>& used_rows
) {
    std::vector<uint32_t> locals;
    locals.reserve(needed_count);

    while (locals.size() < needed_count) {
        bool found_group = false;

        for (size_t group_index : three_ra_group_indices()) {
            if (group_index >= SUPPORTED_GROUPS.size()) {
                continue;
            }

            const auto& offsets = SUPPORTED_GROUPS[group_index].offsets;
            if (offsets.empty()) {
                continue;
            }

            uint32_t max_offset = offsets.back();
            if (max_offset >= ROWS_PER_MAT) {
                continue;
            }

            for (uint32_t base_local = 0; base_local + max_offset < ROWS_PER_MAT; ++base_local) {
                bool free = true;
                std::vector<uint32_t> candidate_locals;
                candidate_locals.reserve(offsets.size());

                for (uint32_t offset : offsets) {
                    uint32_t local_row = base_local + offset;
                    uint32_t row_m0 = make_global_row(mat0_id, local_row);
                    uint32_t row_m1 = make_global_row(mat1_id, local_row);

                    if (used_rows.find(row_m0) != used_rows.end() ||
                        used_rows.find(row_m1) != used_rows.end()) {
                        free = false;
                        break;
                    }
                    candidate_locals.push_back(local_row);
                }

                if (!free) {
                    continue;
                }

                for (uint32_t local_row : candidate_locals) {
                    used_rows.insert(make_global_row(mat0_id, local_row));
                    used_rows.insert(make_global_row(mat1_id, local_row));
                    locals.push_back(local_row);
                }

                found_group = true;
                break;
            }

            if (found_group) {
                break;
            }
        }

        if (!found_group) {
            return std::nullopt;
        }
    }

    locals.resize(needed_count);
    return locals;
}

class ScopedAddNewReservedRows {
public:
    explicit ScopedAddNewReservedRows(const std::set<uint32_t>* rows)
        : prev_(g_add_new_extra_reserved_rows) {
        g_add_new_extra_reserved_rows = rows;
    }

    ~ScopedAddNewReservedRows() {
        g_add_new_extra_reserved_rows = prev_;
    }

private:
    const std::set<uint32_t>* prev_;
};

} // namespace


uint32_t END() {
    return 0x00000000;
}

uint32_t MULTI_BANK_ENTRY(uint32_t open_banks_mode) {
    if (open_banks_mode > 2) {
        std::cout << "[ERROR]: MULTI_BANK_ENTRY open_banks_mode must be 0(4), 1(8), or 2(16)" << std::endl;
        return 0;
    }
    return build_address(AddrMode::MULTI_BANK_ENTRY, 0, 0, open_banks_mode, 0, 0, 0);
}

uint32_t MULTI_BANK_EXIT() {
    return build_address(AddrMode::MULTI_BANK_EXIT, 0, 0, 0, 0, 0, 0);
}

std::vector<uint32_t> single_Row_Copy(uint64_t src, uint64_t dest) {
    std::vector<uint32_t> inst_list;

    uint32_t src_bank = extract_bank_addr(src);
    uint32_t dest_bank = extract_bank_addr(dest);

    if(src_bank != dest_bank){
        std::cout<<"[ERROR]: Invalid Bank Addr Pair within Single RowCopy"<<std::endl;
        return inst_list;
    }

    uint32_t src_row = extract_row_addr(src);
    uint32_t dest_row = extract_row_addr(dest);

    inst_list.push_back(build_address(AddrMode::ROW_COPY,src_bank,src_row,0,0,0));
    inst_list.push_back(build_address(AddrMode::ROW_COPY_1,dest_bank,dest_row,1,0,0));

    return inst_list;
}

std::vector<uint32_t> Multi_Row_Copy(uint64_t src, uint64_t dest1, uint64_t dest2) {
    std::vector<uint32_t> inst_list;

    uint32_t src_bank = extract_bank_addr(src);
    uint32_t dest1_bank = extract_bank_addr(dest1);
    uint32_t dest2_bank = extract_bank_addr(dest2);

    if(src_bank != dest1_bank || src_bank != dest2_bank){
        std::cout<<"[ERROR]: Invalid Addr Pair within Multiple RowCopy"<<std::endl;
        return inst_list;
    }

    uint32_t src_row = extract_row_addr(src);
    uint32_t dest1_row = extract_row_addr(dest1);
    uint32_t dest2_row = extract_row_addr(dest2);

    inst_list.push_back(build_address(AddrMode::ROW_COPY,src_bank,src_row,0,0,0));
    inst_list.push_back(build_address(AddrMode::ROW_COPY_1,dest1_bank,dest1_row,0,0,0));
    inst_list.push_back(build_address(AddrMode::ROW_COPY_1,dest2_bank,dest2_row,1,0,0));

    return inst_list;
}

std::vector<uint32_t> Maj3(uint64_t src1, uint64_t src2, uint64_t src3) {
    std::vector<uint32_t> inst_list;

    uint32_t src1_bank = extract_bank_addr(src1);
    uint32_t src2_bank = extract_bank_addr(src2);
    uint32_t src3_bank = extract_bank_addr(src3);

    if (src1_bank != src2_bank || src2_bank != src3_bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within MAJ3" << std::endl;
        return inst_list;
    }

    uint32_t src1_row = extract_row_addr(src1);
    uint32_t src2_row = extract_row_addr(src2);
    uint32_t src3_row = extract_row_addr(src3);

    uint32_t mat1 = get_mat_id_from_row(src1_row);
    uint32_t mat2 = get_mat_id_from_row(src2_row);
    uint32_t mat3 = get_mat_id_from_row(src3_row);

    if (mat1 != mat2 || mat2 != mat3) {
        std::cout << "[ERROR]: MAJ3 rows cross MAT boundary" << std::endl;
        return inst_list;
    }

    uint32_t lrow1 = get_local_row_from_global(src1_row);
    uint32_t lrow2 = get_local_row_from_global(src2_row);
    uint32_t lrow3 = get_local_row_from_global(src3_row);

    AnalyzeResult result = analyze_rows({lrow1, lrow2, lrow3});

    if (!result.valid) {
        std::cout << "[ERROR]: NO_RA_PAIR within MAJ3" << std::endl;
        return inst_list;
    }

    uint32_t local_base_row = result.base_row;
    uint32_t global_base_row = make_global_row(mat1, local_base_row);

    uint32_t ra_group_index = result.ra_group_index;
    if (ra_group_index > 6) {
        std::cout << "[ERROR]: MAJ3 matched non-2RA group" << std::endl;
        return inst_list;
    }

    uint32_t dont_care_pos = get_dont_care_pos_maj3(ra_group_index);
    const auto& group_offsets = SUPPORTED_GROUPS[ra_group_index].offsets;
    uint32_t max_offset = group_offsets.back();

    if (local_base_row + max_offset >= ROWS_PER_MAT) {
        std::cout << "[ERROR]: MAJ3 group spills outside MAT boundary" << std::endl;
        return inst_list;
    }

    std::vector<uint32_t> free_offsets;
    for (auto off : group_offsets) {
        if (std::find(result.frac_rows.begin(),
                      result.frac_rows.end(),
                      off) == result.frac_rows.end()) {
            free_offsets.push_back(off);
        }
    }

    if (free_offsets.size() != 1) {
        std::cout << "[ERROR]: MAJ3 expects exactly one free row" << std::endl;
        return inst_list;
    }

    uint32_t out_offset = free_offsets[0];

    uint32_t frac_pos = std::distance(
        group_offsets.begin(),
        std::find(group_offsets.begin(), group_offsets.end(), out_offset)
    );

    inst_list.push_back(
        build_address(
            AddrMode::MAJ3,
            src1_bank,
            global_base_row,
            0,
            frac_pos,
            0,
            dont_care_pos
        )
    );

    return inst_list;
}


std::vector<uint32_t> Maj5(uint64_t src1, uint64_t src2, uint64_t src3, uint64_t src4, uint64_t src5) {
    std::vector<uint32_t> inst_list;

    uint32_t src1_bank = extract_bank_addr(src1);
    uint32_t src2_bank = extract_bank_addr(src2);
    uint32_t src3_bank = extract_bank_addr(src3);
    uint32_t src4_bank = extract_bank_addr(src4);
    uint32_t src5_bank = extract_bank_addr(src5);

    if (src1_bank != src2_bank || src1_bank != src3_bank || src1_bank != src4_bank || src1_bank != src5_bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within MAJ5" << std::endl;
        return inst_list;
    }

    uint32_t src1_row = extract_row_addr(src1);
    uint32_t src2_row = extract_row_addr(src2);
    uint32_t src3_row = extract_row_addr(src3);
    uint32_t src4_row = extract_row_addr(src4);
    uint32_t src5_row = extract_row_addr(src5);

    uint32_t mat1 = get_mat_id_from_row(src1_row);
    uint32_t mat2 = get_mat_id_from_row(src2_row);
    uint32_t mat3 = get_mat_id_from_row(src3_row);
    uint32_t mat4 = get_mat_id_from_row(src4_row);
    uint32_t mat5 = get_mat_id_from_row(src5_row);

    if (mat1 != mat2 || mat1 != mat3 || mat1 != mat4 || mat1 != mat5) {
        std::cout << "[ERROR]: MAJ5 rows cross MAT boundary" << std::endl;
        return inst_list;
    }

    uint32_t lrow1 = get_local_row_from_global(src1_row);
    uint32_t lrow2 = get_local_row_from_global(src2_row);
    uint32_t lrow3 = get_local_row_from_global(src3_row);
    uint32_t lrow4 = get_local_row_from_global(src4_row);
    uint32_t lrow5 = get_local_row_from_global(src5_row);

    AnalyzeResult result = analyze_rows({lrow1, lrow2, lrow3, lrow4, lrow5});

    if (!result.valid) {
        std::cout << "[ERROR]: NO_RA_PAIR within MAJ5" << std::endl;
        return inst_list;
    }

    uint32_t local_base_row = result.base_row;
    uint32_t global_base_row = make_global_row(mat1, local_base_row);

    uint32_t ra_group_index = result.ra_group_index;
    if (ra_group_index < 7) {
        std::cout << "[ERROR]: MAJ5 matched non-3RA group" << std::endl;
        return inst_list;
    }

    uint32_t dont_care_pos = get_dont_care_pos_maj5(ra_group_index);
    const auto& group_offsets = SUPPORTED_GROUPS[ra_group_index].offsets;
    uint32_t max_offset = group_offsets.back();

    if (local_base_row + max_offset >= ROWS_PER_MAT) {
        std::cout << "[ERROR]: MAJ5 group spills outside MAT boundary" << std::endl;
        return inst_list;
    }

    std::vector<uint32_t> free_offsets;
    for (auto off : group_offsets) {
        if (std::find(result.frac_rows.begin(),
                      result.frac_rows.end(),
                      off) == result.frac_rows.end()) {
            free_offsets.push_back(off);
        }
    }

    if (free_offsets.size() != 3) {
        std::cout << "[ERROR]: MAJ5 expects exactly three free rows" << std::endl;
        return inst_list;
    }

    std::vector<uint32_t> frac_pos;
    for (auto off : free_offsets) {
        frac_pos.push_back(std::distance(
            group_offsets.begin(),
            std::find(group_offsets.begin(), group_offsets.end(), off)
        ));
    }

    inst_list.push_back(build_address(AddrMode::MAJ5_1, src1_bank, global_base_row, 0, 0, 0, dont_care_pos));
    inst_list.push_back(build_address(AddrMode::MAJ5_2, 0, 0, 0, frac_pos[0], frac_pos[1], frac_pos[2]));

    return inst_list;
}

std::vector<uint32_t> OR(uint64_t src1, uint64_t src2) {
    std::vector<uint32_t> inst_list;

    uint32_t src1_bank = extract_bank_addr(src1);
    uint32_t src2_bank = extract_bank_addr(src2);

    if (src1_bank != src2_bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within OR" << std::endl;
        return inst_list;
    }

    uint32_t src1_row = extract_row_addr(src1);
    uint32_t src2_row = extract_row_addr(src2);

    uint32_t mat1 = get_mat_id_from_row(src1_row);
    uint32_t mat2 = get_mat_id_from_row(src2_row);

    if (mat1 != mat2) {
        std::cout << "[ERROR]: OR rows cross MAT boundary" << std::endl;
        return inst_list;
    }

    uint32_t lrow1 = get_local_row_from_global(src1_row);
    uint32_t lrow2 = get_local_row_from_global(src2_row);

    AnalyzeResult result = analyze_rows({lrow1, lrow2});
    if (!result.valid) {
        std::cout << "[ERROR]: NO_RA_PAIR within OR" << std::endl;
        return inst_list;
    }

    uint32_t local_base_row = result.base_row;
    uint32_t global_base_row = make_global_row(mat1, local_base_row);
    uint32_t ra_group_index = result.ra_group_index;

    if (ra_group_index > 6) {
        std::cout << "[ERROR]: OR matched non-2RA group" << std::endl;
        return inst_list;
    }

    uint32_t dont_care_pos = get_dont_care_pos_maj3(ra_group_index);
    const auto& group_offsets = SUPPORTED_GROUPS[ra_group_index].offsets;
    uint32_t max_offset = group_offsets.back();

    if (local_base_row + max_offset >= ROWS_PER_MAT) {
        std::cout << "[ERROR]: OR group spills outside MAT boundary" << std::endl;
        return inst_list;
    }

    std::vector<uint32_t> free_offsets;
    for (auto off : group_offsets) {
        if (std::find(result.frac_rows.begin(),
                      result.frac_rows.end(),
                      off) == result.frac_rows.end()) {
            free_offsets.push_back(off);
        }
    }

    if (free_offsets.size() < 2) {
        std::cout << "[ERROR]: Not enough free rows for OR operation" << std::endl;
        return inst_list;
    }

    uint32_t const_offset = free_offsets[0];
    uint32_t const_row_global = global_base_row + const_offset;

    uint32_t mat_id = mat1;
    uint32_t const_src_global = make_global_row(mat_id, temp_row_1);

    uint32_t const_src_addr  = make_addr(src1_bank, const_src_global);
    uint32_t const_dest_addr = make_addr(src1_bank, const_row_global);

    auto temp_const = single_Row_Copy(const_src_addr, const_dest_addr);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    uint32_t out_offset = free_offsets[1];
    uint32_t frac_pos = std::distance(
        group_offsets.begin(),
        std::find(group_offsets.begin(), group_offsets.end(), out_offset)
    );

    inst_list.push_back(
        build_address(
            AddrMode::MAJ3,
            src1_bank,
            global_base_row,
            0,
            frac_pos,
            0,
            dont_care_pos
        )
    );

    return inst_list;
}

std::vector<uint32_t> AND(uint64_t src1, uint64_t src2) {
    std::vector<uint32_t> inst_list;

    uint32_t src1_bank = extract_bank_addr(src1);
    uint32_t src2_bank = extract_bank_addr(src2);

    if (src1_bank != src2_bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within AND" << std::endl;
        return inst_list;
    }

    uint32_t src1_row = extract_row_addr(src1);
    uint32_t src2_row = extract_row_addr(src2);

    uint32_t mat1 = get_mat_id_from_row(src1_row);
    uint32_t mat2 = get_mat_id_from_row(src2_row);

    if (mat1 != mat2) {
        std::cout << "[ERROR]: AND rows cross MAT boundary" << std::endl;
        return inst_list;
    }

    uint32_t lrow1 = get_local_row_from_global(src1_row);
    uint32_t lrow2 = get_local_row_from_global(src2_row);

    AnalyzeResult result = analyze_rows({lrow1, lrow2});
    if (!result.valid) {
        std::cout << "[ERROR]: NO_RA_PAIR within AND" << std::endl;
        return inst_list;
    }

    uint32_t local_base_row = result.base_row;
    uint32_t global_base_row = make_global_row(mat1, local_base_row);
    uint32_t ra_group_index = result.ra_group_index;

    if (ra_group_index > 6) {
        std::cout << "[ERROR]: AND matched non-2RA group" << std::endl;
        return inst_list;
    }

    uint32_t dont_care_pos = get_dont_care_pos_maj3(ra_group_index);
    const auto& group_offsets = SUPPORTED_GROUPS[ra_group_index].offsets;
    uint32_t max_offset = group_offsets.back();

    if (local_base_row + max_offset >= ROWS_PER_MAT) {
        std::cout << "[ERROR]: AND group spills outside MAT boundary" << std::endl;
        return inst_list;
    }
    

    std::vector<uint32_t> free_offsets;
    for (auto off : group_offsets) {
        if (std::find(result.frac_rows.begin(),
                      result.frac_rows.end(),
                      off) == result.frac_rows.end()) {
            free_offsets.push_back(off);
        }
    }

    if (free_offsets.size() < 2) {
        std::cout << "[ERROR]: Not enough free rows for AND operation" << std::endl;
        return inst_list;
    }

    uint32_t const_offset = free_offsets[0];
    uint32_t const_row_global = global_base_row + const_offset;

    uint32_t mat_id = mat1;
    uint32_t const_src_global = make_global_row(mat_id, temp_row_0);

    uint32_t const_src_addr  = make_addr(src1_bank, const_src_global);
    uint32_t const_dest_addr = make_addr(src1_bank, const_row_global);

    auto temp_const = single_Row_Copy(const_src_addr, const_dest_addr);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    uint32_t out_offset = free_offsets[1];
    uint32_t frac_pos = std::distance(
        group_offsets.begin(),
        std::find(group_offsets.begin(), group_offsets.end(), out_offset)
    );

    inst_list.push_back(
        build_address(
            AddrMode::MAJ3,
            src1_bank,
            global_base_row,
            0,
            frac_pos,
            0,
            dont_care_pos
        )
    );

    return inst_list;
}

std::vector<uint32_t> NOT(uint64_t src, uint64_t dest) {
    std::vector<uint32_t> inst_list;

    uint32_t src_bank = extract_bank_addr(src);
    uint32_t dst_bank = extract_bank_addr(dest);

    if (src_bank != dst_bank) {
        std::cout << "[ERROR]: NOT requires same bank" << std::endl;
        return inst_list;
    }

    uint32_t src_row = extract_row_addr(src);
    uint32_t dst_row = extract_row_addr(dest);

    uint32_t src_mat = get_mat_id_from_row(src_row);
    uint32_t dst_mat = get_mat_id_from_row(dst_row);

    uint32_t hops = (src_mat > dst_mat) ? (src_mat - dst_mat) : (dst_mat - src_mat);

    if ((hops % 2) == 0) {
        std::cout << "[ERROR]: NOT requires odd MAT distance" << std::endl;
        return inst_list;
    }

    return single_Row_Copy(src, dest);
}


std::vector<uint32_t> XOR(uint64_t src1, uint64_t src2) {
    std::vector<uint32_t> inst_list;
    
    uint32_t src1_bank = extract_bank_addr(src1);
    uint32_t src2_bank = extract_bank_addr(src2);

    if (src1_bank != src2_bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return inst_list;
    }

    uint32_t src1_row = extract_row_addr(src1);
    uint32_t src2_row = extract_row_addr(src2);

    uint32_t src_mat0 = get_mat_id_from_row(src1_row);
    uint32_t src_mat1 = src_mat0 + 1;

    if (get_mat_id_from_row(src2_row) != src_mat0) {
        std::cout << "[ERROR]: XOR expects src1/src2 in same MAT" << std::endl;
        return inst_list;
    }

    std::set<uint32_t> used_rows;
    reserve_addr_with_next_mat(used_rows, src1);
    reserve_addr_with_next_mat(used_rows, src2);

    auto row_group_1_opt = allocate_group_rows(src1_bank, src_mat0, two_ra_group_indices(), used_rows);
    auto row_group_2_opt = allocate_group_rows(src1_bank, src_mat1, two_ra_group_indices(), used_rows);

    if (!row_group_1_opt || !row_group_2_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for XOR" << std::endl;
        return inst_list;
    }

    const auto& row_group_1 = *row_group_1_opt;
    const auto& row_group_2 = *row_group_2_opt;

    //RowCopy A into row_group 1 and 2
    auto temp_const = single_Row_Copy(src1, row_group_1[0]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());
    temp_const = single_Row_Copy(src1 + ROWS_PER_MAT, row_group_2[0]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    //RowCopy B into row_group 1 and 2
    temp_const = single_Row_Copy(src2, row_group_1[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());
    temp_const = single_Row_Copy(src2 + ROWS_PER_MAT, row_group_2[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    //OR row_group_1
    temp_const = OR(row_group_1[0], row_group_1[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    //AND row_group_2
    temp_const = AND(row_group_2[0], row_group_2[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    //RowCopy ~row_group_2 data into row_group_1
    temp_const = NOT(row_group_2[0], row_group_1[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    //AND row_group_1 to get XOR
    temp_const = AND(row_group_1[0], row_group_1[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    return inst_list;
}

std::vector<uint32_t> Maj5_via_Maj3(uint64_t src1,
                                    uint64_t src2,
                                    uint64_t src3,
                                    uint64_t src4,
                                    uint64_t src5,
                                    uint64_t out) {
    std::vector<uint32_t> inst_list;

    uint32_t bank = extract_bank_addr(src1);
    if (extract_bank_addr(src2) != bank ||
        extract_bank_addr(src3) != bank ||
        extract_bank_addr(src4) != bank ||
        extract_bank_addr(src5) != bank ||
        extract_bank_addr(out)  != bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within Maj5_via_Maj3" << std::endl;
        return {};
    }

    uint32_t row1    = extract_row_addr(src1);
    uint32_t row2    = extract_row_addr(src2);
    uint32_t row3    = extract_row_addr(src3);
    uint32_t row4    = extract_row_addr(src4);
    uint32_t row5    = extract_row_addr(src5);
    uint32_t out_row = extract_row_addr(out);

    uint32_t mat = get_mat_id_from_row(row1);
    if (get_mat_id_from_row(row2) != mat ||
        get_mat_id_from_row(row3) != mat ||
        get_mat_id_from_row(row4) != mat ||
        get_mat_id_from_row(row5) != mat ||
        get_mat_id_from_row(out_row) != mat) {
        std::cout << "[ERROR]: Maj5_via_Maj3 rows must stay in same MAT" << std::endl;
        return {};
    }

    std::set<uint32_t> used_rows;
    reserve_addr(used_rows, src1);
    reserve_addr(used_rows, src2);
    reserve_addr(used_rows, src3);
    reserve_addr(used_rows, src4);
    reserve_addr(used_rows, src5);
    reserve_addr(used_rows, out);
    if (g_add_new_extra_reserved_rows != nullptr) {
        used_rows.insert(g_add_new_extra_reserved_rows->begin(),
                         g_add_new_extra_reserved_rows->end());
    }

    auto g1_opt = allocate_group_rows(bank, mat, two_ra_group_indices(), used_rows);
    auto g2_opt = allocate_group_rows(bank, mat, two_ra_group_indices(), used_rows);
    auto g3_opt = allocate_group_rows(bank, mat, two_ra_group_indices(), used_rows);

    bool final_stage_anchored = false;
    std::optional<std::vector<uint64_t>> g4_opt;
    if (out_row == row1) {
        g4_opt = allocate_group_rows_with_anchor(
            bank, mat, two_ra_group_indices(), out_row, used_rows);
        final_stage_anchored = g4_opt.has_value();
    }
    if (!g4_opt) {
        g4_opt = allocate_group_rows(bank, mat, two_ra_group_indices(), used_rows);
    }

    if (!g1_opt || !g2_opt || !g3_opt || !g4_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for Maj5_via_Maj3" << std::endl;
        return {};
    }

    const auto& g1 = *g1_opt;
    const auto& g2 = *g2_opt;
    const auto& g3 = *g3_opt;
    const auto& g4 = *g4_opt;

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    // Exact decomposition:
    // t1  = MAJ3(src2, src3, src4)
    // t2  = MAJ3(src1, src3, src4)
    // t3  = MAJ3(src2, src5, t2)
    // out = MAJ3(src1, t1, t3)

    // t1 = MAJ3(src2, src3, src4)
    append(single_Row_Copy(src2, g1[0]));
    append(single_Row_Copy(src3, g1[1]));
    append(single_Row_Copy(src4, g1[2]));
    append(Maj3(g1[0], g1[1], g1[2]));

    // t2 = MAJ3(src1, src3, src4)
    append(single_Row_Copy(src1, g2[0]));
    append(single_Row_Copy(src3, g2[1]));
    append(single_Row_Copy(src4, g2[2]));
    append(Maj3(g2[0], g2[1], g2[2]));

    // t3 = MAJ3(src2, src5, t2)
    append(single_Row_Copy(src2, g3[0]));
    append(single_Row_Copy(src5, g3[1]));
    append(single_Row_Copy(g2[0], g3[2]));
    append(Maj3(g3[0], g3[1], g3[2]));

    // out = MAJ3(src1, t1, t3)
    if (final_stage_anchored) {
        std::vector<uint64_t> helper_rows;
        helper_rows.reserve(2);
        for (uint64_t addr : g4) {
            if (extract_row_addr(addr) != out_row && helper_rows.size() < 2) {
                helper_rows.push_back(addr);
            }
        }
        if (helper_rows.size() != 2) {
            std::cout << "[ERROR]: Failed to anchor final MAJ3 in Maj5_via_Maj3" << std::endl;
            return {};
        }
        append(single_Row_Copy(g1[0], helper_rows[0]));
        append(single_Row_Copy(g3[0], helper_rows[1]));
        append(Maj3(out, helper_rows[0], helper_rows[1]));
    } else {
        append(single_Row_Copy(src1, g4[0]));
        append(single_Row_Copy(g1[0], g4[1]));
        append(single_Row_Copy(g3[0], g4[2]));
        append(Maj3(g4[0], g4[1], g4[2]));
        append(single_Row_Copy(g4[0], out));
    }

    return inst_list;
}

static std::vector<uint32_t> Maj5_via_Maj3_sum_inplace(uint64_t src1,
                                                       uint64_t src2,
                                                       uint64_t src3,
                                                       uint64_t src4,
                                                       uint64_t src5) {
    std::vector<uint32_t> inst_list;

    uint32_t bank = extract_bank_addr(src1);
    if (extract_bank_addr(src2) != bank ||
        extract_bank_addr(src3) != bank ||
        extract_bank_addr(src4) != bank ||
        extract_bank_addr(src5) != bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within Maj5_via_Maj3_sum_inplace" << std::endl;
        return {};
    }

    uint32_t row1 = extract_row_addr(src1);
    uint32_t row2 = extract_row_addr(src2);
    uint32_t row3 = extract_row_addr(src3);
    uint32_t row4 = extract_row_addr(src4);
    uint32_t row5 = extract_row_addr(src5);

    uint32_t mat = get_mat_id_from_row(row1);
    if (get_mat_id_from_row(row2) != mat ||
        get_mat_id_from_row(row3) != mat ||
        get_mat_id_from_row(row4) != mat ||
        get_mat_id_from_row(row5) != mat) {
        std::cout << "[ERROR]: Maj5_via_Maj3_sum_inplace rows must stay in same MAT" << std::endl;
        return {};
    }

    if (row1 == row3) {
        return Maj5_via_Maj3(src1, src2, src3, src4, src5, src1);
    }

    std::set<uint32_t> used_rows;
    reserve_addr(used_rows, src1);
    reserve_addr(used_rows, src2);
    reserve_addr(used_rows, src3);
    reserve_addr(used_rows, src4);
    reserve_addr(used_rows, src5);
    if (g_add_new_extra_reserved_rows != nullptr) {
        used_rows.insert(g_add_new_extra_reserved_rows->begin(),
                         g_add_new_extra_reserved_rows->end());
    }

    auto g1_opt = allocate_group_rows(bank, mat, two_ra_group_indices(), used_rows);
    auto g23_opt = allocate_group_rows_with_anchor(
        bank, mat, two_ra_group_indices(), row3, used_rows);

    std::optional<std::vector<uint64_t>> g4_opt =
        allocate_group_rows_with_anchor(bank, mat, two_ra_group_indices(), row1, used_rows);
    const bool final_stage_anchored = g4_opt.has_value();
    if (!g4_opt) {
        g4_opt = allocate_group_rows(bank, mat, two_ra_group_indices(), used_rows);
    }

    if (!g1_opt || !g23_opt || !g4_opt) {
        return Maj5_via_Maj3(src1, src2, src3, src4, src5, src1);
    }

    const auto& g1 = *g1_opt;
    const auto& g23 = *g23_opt;
    const auto& g4 = *g4_opt;

    std::vector<uint64_t> g23_helpers;
    g23_helpers.reserve(2);
    for (uint64_t addr : g23) {
        if (extract_row_addr(addr) != row3 && g23_helpers.size() < 2) {
            g23_helpers.push_back(addr);
        }
    }
    if (g23_helpers.size() != 2) {
        return Maj5_via_Maj3(src1, src2, src3, src4, src5, src1);
    }

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    // t1 = MAJ3(src2, src3, src4)
    append(single_Row_Copy(src2, g1[0]));
    append(single_Row_Copy(src3, g1[1]));
    append(single_Row_Copy(src4, g1[2]));
    append(Maj3(g1[0], g1[1], g1[2]));

    // Reuse src3 as the temporary row: t2 = MAJ3(src1, src3, src4).
    append(single_Row_Copy(src1, g23_helpers[0]));
    append(single_Row_Copy(src4, g23_helpers[1]));
    append(Maj3(src3, g23_helpers[0], g23_helpers[1]));

    // Reuse the same anchored group: t3 = MAJ3(src2, src5, t2).
    append(single_Row_Copy(src2, g23_helpers[0]));
    append(single_Row_Copy(src5, g23_helpers[1]));
    append(Maj3(src3, g23_helpers[0], g23_helpers[1]));

    // Final SUM is written directly into src1 when possible.
    if (final_stage_anchored) {
        std::vector<uint64_t> g4_helpers;
        g4_helpers.reserve(2);
        for (uint64_t addr : g4) {
            if (extract_row_addr(addr) != row1 && g4_helpers.size() < 2) {
                g4_helpers.push_back(addr);
            }
        }
        if (g4_helpers.size() != 2) {
            return {};
        }
        append(single_Row_Copy(g1[0], g4_helpers[0]));
        append(single_Row_Copy(src3, g4_helpers[1]));
        append(Maj3(src1, g4_helpers[0], g4_helpers[1]));
    } else {
        append(single_Row_Copy(src1, g4[0]));
        append(single_Row_Copy(g1[0], g4[1]));
        append(single_Row_Copy(src3, g4[2]));
        append(Maj3(g4[0], g4[1], g4[2]));
        append(single_Row_Copy(g4[0], src1));
    }

    return inst_list;
}


std::vector<uint32_t> ADD_new(uint64_t src1,
                                      uint64_t src2,
                                      uint64_t src3,
                                      uint64_t sum_m0,
                                      uint64_t sum_m1,
                                      uint64_t carry_m0,
                                      uint64_t carry_m1) {
    std::vector<uint32_t> inst_list;

    uint32_t bank = extract_bank_addr(src1);

    if (extract_bank_addr(src2) != bank ||
        extract_bank_addr(src3) != bank ||
        extract_bank_addr(sum_m0) != bank ||
        extract_bank_addr(sum_m1) != bank ||
        extract_bank_addr(carry_m0) != bank ||
        extract_bank_addr(carry_m1) != bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return {};
    }

    uint32_t src_mat0 = get_mat_id_from_row(extract_row_addr(src1));
    uint32_t src_mat1 = src_mat0 + 1;

    if (get_mat_id_from_row(extract_row_addr(src2)) != src_mat0 ||
        get_mat_id_from_row(extract_row_addr(src3)) != src_mat0) {
        std::cout << "[ERROR]: ADD_new expects src1/src2/src3 in same MAT" << std::endl;
        return {};
    }

    std::set<uint32_t> used_rows;
    reserve_addr_with_next_mat(used_rows, src1);
    reserve_addr_with_next_mat(used_rows, src2);
    reserve_addr_with_next_mat(used_rows, src3);
    reserve_addr(used_rows, sum_m0);
    reserve_addr(used_rows, sum_m1);
    reserve_addr(used_rows, carry_m0);
    reserve_addr(used_rows, carry_m1);
    if (g_add_new_extra_reserved_rows != nullptr) {
        used_rows.insert(g_add_new_extra_reserved_rows->begin(),
                         g_add_new_extra_reserved_rows->end());
    }

    auto cout_group_m0_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
    auto cout_group_m1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);
    auto sum_group_m0_full_opt = allocate_group_rows(bank, src_mat0, three_ra_group_indices(), used_rows);
    auto sum_group_m1_full_opt = allocate_group_rows(bank, src_mat1, three_ra_group_indices(), used_rows);

    if (!cout_group_m0_opt || !cout_group_m1_opt ||
        !sum_group_m0_full_opt || !sum_group_m1_full_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for ADD_new" << std::endl;
        return {};
    }

    std::vector<uint64_t> cout_group_m0 = *cout_group_m0_opt;
    std::vector<uint64_t> cout_group_m1 = *cout_group_m1_opt;
    std::vector<uint64_t> sum_group_m0(sum_group_m0_full_opt->begin(),
                                       sum_group_m0_full_opt->begin() + 5);
    std::vector<uint64_t> sum_group_m1(sum_group_m1_full_opt->begin(),
                                       sum_group_m1_full_opt->begin() + 5);

    std::vector<uint32_t> temp_const;
    ScopedAddNewReservedRows add_new_internal_scope(&used_rows);

    // =====================================================
    // Build carry in MAT0
    // =====================================================
    temp_const = single_Row_Copy(src1, cout_group_m0[0]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(src2, cout_group_m0[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(src3, cout_group_m0[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = Maj3(cout_group_m0[0], cout_group_m0[1], cout_group_m0[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    // store carry in MAT0
    temp_const = single_Row_Copy(cout_group_m0[0], carry_m0);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    // =====================================================
    // Build carry in MAT1
    // =====================================================
    temp_const = single_Row_Copy(src1 + ROWS_PER_MAT, cout_group_m1[0]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(src2 + ROWS_PER_MAT, cout_group_m1[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(src3 + ROWS_PER_MAT, cout_group_m1[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = Maj3(cout_group_m1[0], cout_group_m1[1], cout_group_m1[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    // store carry in MAT1
    temp_const = single_Row_Copy(cout_group_m1[0], carry_m1);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    // =====================================================
    // Build SUM in MAT0 using ~COUT from MAT1
    // SUM = MAJ5(A, B, Cin, ~COUT, ~COUT)
    // =====================================================
    temp_const = single_Row_Copy(src1, sum_group_m0[0]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(src2, sum_group_m0[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(src3, sum_group_m0[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = NOT(cout_group_m1[0], sum_group_m0[3]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(sum_group_m0[3], sum_group_m0[4]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = Maj5(sum_group_m0[0], sum_group_m0[1], sum_group_m0[2],
                      sum_group_m0[3], sum_group_m0[4]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(sum_group_m0[0], sum_m0);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    // temp_const = Maj5_via_Maj3(sum_group_m0[0], sum_group_m0[1], sum_group_m0[2],
    //                        sum_group_m0[3], sum_group_m0[4], sum_m0);
    // inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    // =====================================================
    // Build SUM in MAT1 using ~COUT from MAT0
    // =====================================================
    temp_const = single_Row_Copy(src1 + ROWS_PER_MAT, sum_group_m1[0]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(src2 + ROWS_PER_MAT, sum_group_m1[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(src3 + ROWS_PER_MAT, sum_group_m1[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = NOT(cout_group_m0[0], sum_group_m1[3]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(sum_group_m1[3], sum_group_m1[4]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = Maj5(sum_group_m1[0], sum_group_m1[1], sum_group_m1[2],
                      sum_group_m1[3], sum_group_m1[4]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(sum_group_m1[0], sum_m1);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    // temp_const = Maj5_via_Maj3(sum_group_m1[0], sum_group_m1[1], sum_group_m1[2],
    //                        sum_group_m1[3], sum_group_m1[4], sum_m1);
    // inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    return inst_list;
}

std::vector<uint32_t> Mult_4_bit_new(    
                                    std::vector<uint64_t> srcA,
                                    std::vector<uint64_t> srcB,
                                    std::vector<uint64_t> prod_m0_dst,
                                    std::vector<uint64_t> prod_m1_dst
) {
    std::vector<uint32_t> inst_list;

    if (srcA.size() != 4 || srcB.size() != 4 ||
        prod_m0_dst.size() != 8 || prod_m1_dst.size() != 8) {
        std::cout << "[ERROR]: Mult_4_bit_new_dualout expects A/B=4 bits and product=8 bits\n";
        return {};
    }

    uint32_t bank = extract_bank_addr(srcA[0]);

    for (int i = 0; i < 4; i++) {
        if (extract_bank_addr(srcA[i]) != bank ||
            extract_bank_addr(srcB[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    for (int i = 0; i < 8; i++) {
        if (extract_bank_addr(prod_m0_dst[i]) != bank ||
            extract_bank_addr(prod_m1_dst[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    uint32_t src_mat0 = get_mat_id_from_row(extract_row_addr(srcA[0]));
    uint32_t src_mat1 = src_mat0 + 1;

    for (int i = 0; i < 4; i++) {
        if (get_mat_id_from_row(extract_row_addr(srcA[i])) != src_mat0 ||
            get_mat_id_from_row(extract_row_addr(srcB[i])) != src_mat0) {
            std::cout << "[ERROR]: Mult_4_bit_new expects srcA/srcB in same MAT\n";
            return {};
        }
    }

    for (int i = 0; i < 8; i++) {
        if (get_mat_id_from_row(extract_row_addr(prod_m0_dst[i])) != src_mat0 ||
            get_mat_id_from_row(extract_row_addr(prod_m1_dst[i])) != src_mat1) {
            std::cout << "[ERROR]: Mult_4_bit_new expects product outputs in adjacent MATs\n";
            return {};
        }
    }

    // ---------------------------------------------------------
    // Constants
    // ---------------------------------------------------------
    uint64_t zero_m0 = make_addr(bank, make_global_row(src_mat0, temp_row_0));

    std::set<uint32_t> used_rows;
    for (auto x : srcA) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : srcB) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : prod_m0_dst) reserve_addr(used_rows, x);
    for (auto x : prod_m1_dst) reserve_addr(used_rows, x);
    reserve_row(used_rows, make_global_row(src_mat0, temp_row_0));
    reserve_row(used_rows, make_global_row(src_mat0, temp_row_1));
    reserve_row(used_rows, make_global_row(src_mat1, temp_row_0));
    reserve_row(used_rows, make_global_row(src_mat1, temp_row_1));

    auto and_group_m0_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
    auto and_group_m1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);
    if (!and_group_m0_opt || !and_group_m1_opt) {
        std::cout << "[ERROR]: No free 2RA scratch groups for AND stage in Mult_4_bit_new\n";
        return {};
    }

    std::vector<uint64_t> and_group_m0 = *and_group_m0_opt;
    std::vector<uint64_t> and_group_m1 = *and_group_m1_opt;
    uint64_t and_a_m0 = and_group_m0[0];
    uint64_t and_b_m0 = and_group_m0[1];
    uint64_t and_a_m1 = and_group_m1[0];
    uint64_t and_b_m1 = and_group_m1[1];

    constexpr size_t kMirroredScratchRows = 39; // 16 pp + 23 reduction rows
    auto local_scratch_opt = allocate_mirrored_local_rows(
        src_mat0, src_mat1, kMirroredScratchRows, used_rows);
    if (!local_scratch_opt) {
        std::cout << "[ERROR]: No free mirrored scratch rows for Mult_4_bit_new\n";
        return {};
    }

    const auto& local_scratch = *local_scratch_opt;
    size_t scratch_idx = 0;
    auto next_pair = [&](uint64_t& row_m0, uint64_t& row_m1) {
        uint32_t local_row = local_scratch[scratch_idx++];
        row_m0 = make_addr(bank, make_global_row(src_mat0, local_row));
        row_m1 = make_addr(bank, make_global_row(src_mat1, local_row));
    };

    // 16 partial products in both MATs
    // pp[j][i] = A[i] & B[j]
    std::vector<std::vector<uint64_t>> pp_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_m1(4, std::vector<uint64_t>(4));
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            next_pair(pp_m0[j][i], pp_m1[j][i]);
        }
    }

    // Intermediate sums/carries in both MATs
    uint64_t s2a_m0 = 0, s2a_m1 = 0;
    uint64_t c3a_m0 = 0, c3a_m1 = 0;
    uint64_t c3b_m0 = 0, c3b_m1 = 0;
    next_pair(s2a_m0, s2a_m1);
    next_pair(c3a_m0, c3a_m1);
    next_pair(c3b_m0, c3b_m1);

    uint64_t s3a_m0 = 0, s3a_m1 = 0;
    uint64_t c4a_m0 = 0, c4a_m1 = 0;
    uint64_t s3b_m0 = 0, s3b_m1 = 0;
    uint64_t c4b_m0 = 0, c4b_m1 = 0;
    uint64_t c4c_m0 = 0, c4c_m1 = 0;
    next_pair(s3a_m0, s3a_m1);
    next_pair(c4a_m0, c4a_m1);
    next_pair(s3b_m0, s3b_m1);
    next_pair(c4b_m0, c4b_m1);
    next_pair(c4c_m0, c4c_m1);

    uint64_t s4a_m0 = 0, s4a_m1 = 0;
    uint64_t c5a_m0 = 0, c5a_m1 = 0;
    uint64_t s4b_m0 = 0, s4b_m1 = 0;
    uint64_t c5b_m0 = 0, c5b_m1 = 0;
    uint64_t c5c_m0 = 0, c5c_m1 = 0;
    next_pair(s4a_m0, s4a_m1);
    next_pair(c5a_m0, c5a_m1);
    next_pair(s4b_m0, s4b_m1);
    next_pair(c5b_m0, c5b_m1);
    next_pair(c5c_m0, c5c_m1);

    uint64_t s5a_m0 = 0, s5a_m1 = 0;
    uint64_t c6a_m0 = 0, c6a_m1 = 0;
    uint64_t s5b_m0 = 0, s5b_m1 = 0;
    uint64_t c6b_m0 = 0, c6b_m1 = 0;
    uint64_t c6c_m0 = 0, c6c_m1 = 0;
    next_pair(s5a_m0, s5a_m1);
    next_pair(c6a_m0, c6a_m1);
    next_pair(s5b_m0, s5b_m1);
    next_pair(c6b_m0, c6b_m1);
    next_pair(c6c_m0, c6c_m1);

    uint64_t s6a_m0 = 0, s6a_m1 = 0;
    uint64_t c7a_m0 = 0, c7a_m1 = 0;
    uint64_t c7b_m0 = 0, c7b_m1 = 0;
    uint64_t overflow_m0 = 0, overflow_m1 = 0;
    uint64_t final_carry_m0 = 0, final_carry_m1 = 0;
    next_pair(s6a_m0, s6a_m1);
    next_pair(c7a_m0, c7a_m1);
    next_pair(c7b_m0, c7b_m1);
    next_pair(overflow_m0, overflow_m1);
    next_pair(final_carry_m0, final_carry_m1);

    if (scratch_idx != kMirroredScratchRows) {
        std::cout << "[ERROR]: Internal scratch allocation mismatch in Mult_4_bit_new\n";
        return {};
    }

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    // ---------------------------------------------------------
    // Helper: compute one partial product in both MATs
    // dst_m0 = A[i] & B[j]
    // dst_m1 = A[i]+1184 & B[j]+1184
    // ---------------------------------------------------------
    auto gen_pp_dual = [&](uint64_t a_m0, uint64_t b_m0,
                           uint64_t dst_m0, uint64_t dst_m1) {
        std::vector<uint32_t> local;

        // MAT0
        auto t = single_Row_Copy(a_m0, and_a_m0);
        local.insert(local.end(), t.begin(), t.end());

        t = single_Row_Copy(b_m0, and_b_m0);
        local.insert(local.end(), t.begin(), t.end());

        t = AND(and_a_m0, and_b_m0);
        local.insert(local.end(), t.begin(), t.end());

        t = single_Row_Copy(and_a_m0, dst_m0);
        local.insert(local.end(), t.begin(), t.end());

        // MAT1
        t = single_Row_Copy(a_m0 + ROWS_PER_MAT, and_a_m1);
        local.insert(local.end(), t.begin(), t.end());

        t = single_Row_Copy(b_m0 + ROWS_PER_MAT, and_b_m1);
        local.insert(local.end(), t.begin(), t.end());

        t = AND(and_a_m1, and_b_m1);
        local.insert(local.end(), t.begin(), t.end());

        t = single_Row_Copy(and_a_m1, dst_m1);
        local.insert(local.end(), t.begin(), t.end());

        return local;
    };

    // ---------------------------------------------------------
    // Generate all 16 partial products
    // ---------------------------------------------------------
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            append(gen_pp_dual(srcA[i], srcB[j], pp_m0[j][i], pp_m1[j][i]));
        }
    }

    // =========================================================
    // P0 = pp00
    // =========================================================
    append(single_Row_Copy(pp_m0[0][0], prod_m0_dst[0]));
    append(single_Row_Copy(pp_m1[0][0], prod_m1_dst[0]));

    // Keep ADD_new scratch allocation away from multiplier intermediates.
    ScopedAddNewReservedRows add_new_scope(&used_rows);

    // =========================================================
    // P1: pp10 + pp01
    // =========================================================
    append(ADD_new(pp_m0[0][1], pp_m0[1][0], zero_m0,
                           prod_m0_dst[1], prod_m1_dst[1],
                           c3a_m0, c3a_m1));
    // Here c3a is really carry into next column (column 2)

    // =========================================================
    // P2: pp20 + pp11 + pp02 + carry_from_P1
    // FA1(pp20, pp11, c3a) -> s2a, c3b
    // FA2(s2a,  pp02, 0)   -> P2, c4a
    // Carries into P3 are: c3b and c4a
    // =========================================================
    append(ADD_new(pp_m0[0][2], pp_m0[1][1], c3a_m0,
                           s2a_m0, s2a_m1,
                           c3b_m0, c3b_m1));

    append(ADD_new(s2a_m0, pp_m0[2][0], zero_m0,
                           prod_m0_dst[2], prod_m1_dst[2],
                           c4a_m0, c4a_m1));

    // =========================================================
    // P3 inputs:
    // pp30, pp21, pp12, pp03, c3b, c4a
    // FA1(pp30, pp21, pp12) -> s3a, c4b
    // FA2(pp03, c3b, c4a)   -> s3b, c4c
    // FA3(s3a,  s3b, 0)     -> P3,  c5a
    // Carries into P4 are: c4b, c4c, c5a
    // =========================================================
    append(ADD_new(pp_m0[0][3], pp_m0[1][2], pp_m0[2][1],
                           s3a_m0, s3a_m1,
                           c4b_m0, c4b_m1));

    append(ADD_new(pp_m0[3][0], c3b_m0, c4a_m0,
                           s3b_m0, s3b_m1,
                           c4c_m0, c4c_m1));

    append(ADD_new(s3a_m0, s3b_m0, zero_m0,
                           prod_m0_dst[3], prod_m1_dst[3],
                           c5a_m0, c5a_m1));

    // =========================================================
    // P4 inputs:
    // pp31, pp22, pp13, c4b, c4c, c5a
    // FA1(pp31, pp22, pp13) -> s4a, c5b
    // FA2(c4b, c4c, c5a)    -> s4b, c5c
    // FA3(s4a, s4b, 0)      -> P4,  c6a
    // Carries into P5 are: c5b, c5c, c6a
    // =========================================================
    append(ADD_new(pp_m0[1][3], pp_m0[2][2], pp_m0[3][1],
                           s4a_m0, s4a_m1,
                           c5b_m0, c5b_m1));

    append(ADD_new(c4b_m0, c4c_m0, c5a_m0,
                           s4b_m0, s4b_m1,
                           c5c_m0, c5c_m1));

    append(ADD_new(s4a_m0, s4b_m0, zero_m0,
                           prod_m0_dst[4], prod_m1_dst[4],
                           c6a_m0, c6a_m1));

    // =========================================================
    // P5 inputs:
    // pp32, pp23, c5b, c5c, c6a
    // FA1(pp32, pp23, c5b) -> s5a, c6b
    // FA2(c5c, c6a, 0)     -> s5b, c6c
    // FA3(s5a, s5b, 0)     -> P5,  c7a
    // Carries into P6 are: c6b, c6c, c7a
    // =========================================================
    append(ADD_new(pp_m0[2][3], pp_m0[3][2], c5b_m0,
                           s5a_m0, s5a_m1,
                           c6b_m0, c6b_m1));

    append(ADD_new(c5c_m0, c6a_m0, zero_m0,
                           s5b_m0, s5b_m1,
                           c6c_m0, c6c_m1));

    append(ADD_new(s5a_m0, s5b_m0, zero_m0,
                           prod_m0_dst[5], prod_m1_dst[5],
                           c7a_m0, c7a_m1));

    // =========================================================
    // P6 inputs:
    // pp33, c6b, c6c, c7a
    // FA1(pp33, c6b, c6c) -> s6a, c7b
    // FA2(s6a,  c7a, 0)   -> P6,  overflow
    // P7 uses c7b + overflow
    // =========================================================
    append(ADD_new(pp_m0[3][3], c6b_m0, c6c_m0,
                           s6a_m0, s6a_m1,
                           c7b_m0, c7b_m1));

    append(ADD_new(s6a_m0, c7a_m0, zero_m0,
                           prod_m0_dst[6], prod_m1_dst[6],
                           overflow_m0, overflow_m1));

    // =========================================================
    // P7 inputs:
    // c7b + overflow
    // =========================================================
    append(ADD_new(c7b_m0, overflow_m0, zero_m0,
                           prod_m0_dst[7], prod_m1_dst[7],
                           final_carry_m0, final_carry_m1)); // final overflow ignored

    return inst_list;
}

static std::vector<uint32_t> ADD_new_fast_for_mult(
    uint64_t src1,
    uint64_t src2,
    uint64_t src3,
    uint64_t sum_m0,
    uint64_t sum_m1,
    uint64_t carry_m0,
    uint64_t carry_m1
) {
    std::vector<uint32_t> inst_list;

    uint32_t bank = extract_bank_addr(src1);

    if (extract_bank_addr(src2) != bank ||
        extract_bank_addr(src3) != bank ||
        extract_bank_addr(sum_m0) != bank ||
        extract_bank_addr(sum_m1) != bank ||
        extract_bank_addr(carry_m0) != bank ||
        extract_bank_addr(carry_m1) != bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return {};
    }

    uint32_t src_mat0 = get_mat_id_from_row(extract_row_addr(src1));
    uint32_t src_mat1 = src_mat0 + 1;

    if (get_mat_id_from_row(extract_row_addr(src2)) != src_mat0 ||
        get_mat_id_from_row(extract_row_addr(src3)) != src_mat0) {
        std::cout << "[ERROR]: ADD_new expects src1/src2/src3 in same MAT" << std::endl;
        return {};
    }

    std::set<uint32_t> used_rows;
    reserve_addr_with_next_mat(used_rows, src1);
    reserve_addr_with_next_mat(used_rows, src2);
    reserve_addr_with_next_mat(used_rows, src3);
    reserve_addr(used_rows, sum_m0);
    reserve_addr(used_rows, sum_m1);
    reserve_addr(used_rows, carry_m0);
    reserve_addr(used_rows, carry_m1);
    if (g_add_new_extra_reserved_rows != nullptr) {
        used_rows.insert(g_add_new_extra_reserved_rows->begin(),
                         g_add_new_extra_reserved_rows->end());
    }

    auto cout_group_m0_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
    auto cout_group_m1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);
    auto sum_group_m0_full_opt = allocate_group_rows(bank, src_mat0, three_ra_group_indices(), used_rows);
    auto sum_group_m1_full_opt = allocate_group_rows(bank, src_mat1, three_ra_group_indices(), used_rows);

    if (!cout_group_m0_opt || !cout_group_m1_opt ||
        !sum_group_m0_full_opt || !sum_group_m1_full_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for ADD_new" << std::endl;
        return {};
    }

    std::vector<uint64_t> cout_group_m0 = *cout_group_m0_opt;
    std::vector<uint64_t> cout_group_m1 = *cout_group_m1_opt;
    std::vector<uint64_t> sum_group_m0(sum_group_m0_full_opt->begin(),
                                       sum_group_m0_full_opt->begin() + 5);
    std::vector<uint64_t> sum_group_m1(sum_group_m1_full_opt->begin(),
                                       sum_group_m1_full_opt->begin() + 5);

    std::vector<uint32_t> temp_const;
    ScopedAddNewReservedRows add_new_internal_scope(&used_rows);

    // Fan out each source into carry/sum scratch in one shot.
    temp_const = Multi_Row_Copy(src1, cout_group_m0[0], sum_group_m0[0]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = Multi_Row_Copy(src2, cout_group_m0[1], sum_group_m0[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = Multi_Row_Copy(src3, cout_group_m0[2], sum_group_m0[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = Multi_Row_Copy(src1 + ROWS_PER_MAT, cout_group_m1[0], sum_group_m1[0]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = Multi_Row_Copy(src2 + ROWS_PER_MAT, cout_group_m1[1], sum_group_m1[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = Multi_Row_Copy(src3 + ROWS_PER_MAT, cout_group_m1[2], sum_group_m1[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    // Carry in MAT0
    temp_const = Maj3(cout_group_m0[0], cout_group_m0[1], cout_group_m0[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(cout_group_m0[0], carry_m0);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    // Carry in MAT1
    temp_const = Maj3(cout_group_m1[0], cout_group_m1[1], cout_group_m1[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(cout_group_m1[0], carry_m1);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    // Sum in MAT0 using ~carry from MAT1
    temp_const = NOT(cout_group_m1[0], sum_group_m0[3]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(sum_group_m0[3], sum_group_m0[4]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = Maj5(sum_group_m0[0], sum_group_m0[1], sum_group_m0[2],
                      sum_group_m0[3], sum_group_m0[4]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(sum_group_m0[0], sum_m0);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    // Sum in MAT1 using ~carry from MAT0
    temp_const = NOT(cout_group_m0[0], sum_group_m1[3]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(sum_group_m1[3], sum_group_m1[4]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = Maj5(sum_group_m1[0], sum_group_m1[1], sum_group_m1[2],
                      sum_group_m1[3], sum_group_m1[4]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    temp_const = single_Row_Copy(sum_group_m1[0], sum_m1);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    return inst_list;
}

std::vector<uint32_t> Mult_4_bit_optimized(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0_dst,
    std::vector<uint64_t> prod_m1_dst
) {
    std::vector<uint32_t> inst_list;

    if (srcA.size() != 4 || srcB.size() != 4 ||
        prod_m0_dst.size() != 8 || prod_m1_dst.size() != 8) {
        std::cout << "[ERROR]: Mult_4_bit_optimized expects A/B=4 bits and product=8 bits\n";
        return {};
    }

    uint32_t bank = extract_bank_addr(srcA[0]);

    for (int i = 0; i < 4; i++) {
        if (extract_bank_addr(srcA[i]) != bank ||
            extract_bank_addr(srcB[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    for (int i = 0; i < 8; i++) {
        if (extract_bank_addr(prod_m0_dst[i]) != bank ||
            extract_bank_addr(prod_m1_dst[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    uint32_t src_mat0 = get_mat_id_from_row(extract_row_addr(srcA[0]));
    uint32_t src_mat1 = src_mat0 + 1;

    for (int i = 0; i < 4; i++) {
        if (get_mat_id_from_row(extract_row_addr(srcA[i])) != src_mat0 ||
            get_mat_id_from_row(extract_row_addr(srcB[i])) != src_mat0) {
            std::cout << "[ERROR]: Mult_4_bit_optimized expects srcA/srcB in same MAT\n";
            return {};
        }
    }

    for (int i = 0; i < 8; i++) {
        if (get_mat_id_from_row(extract_row_addr(prod_m0_dst[i])) != src_mat0 ||
            get_mat_id_from_row(extract_row_addr(prod_m1_dst[i])) != src_mat1) {
            std::cout << "[ERROR]: Mult_4_bit_optimized expects product outputs in adjacent MATs\n";
            return {};
        }
    }

    uint64_t zero_m0 = make_addr(bank, make_global_row(src_mat0, temp_row_0));

    std::set<uint32_t> used_rows;
    for (auto x : srcA) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : srcB) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : prod_m0_dst) reserve_addr(used_rows, x);
    for (auto x : prod_m1_dst) reserve_addr(used_rows, x);
    reserve_row(used_rows, make_global_row(src_mat0, temp_row_0));
    reserve_row(used_rows, make_global_row(src_mat0, temp_row_1));
    reserve_row(used_rows, make_global_row(src_mat1, temp_row_0));
    reserve_row(used_rows, make_global_row(src_mat1, temp_row_1));

    // Dedicated 2RA groups for each partial product.
    std::vector<std::vector<uint64_t>> pp_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_m1(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_rhs_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_rhs_m1(4, std::vector<uint64_t>(4));

    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            auto g0_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
            auto g1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);
            if (!g0_opt || !g1_opt) {
                std::cout << "[ERROR]: No free 2RA scratch groups for partial products in Mult_4_bit_optimized\n";
                return {};
            }

            pp_m0[j][i] = (*g0_opt)[0];
            pp_rhs_m0[j][i] = (*g0_opt)[1];
            pp_m1[j][i] = (*g1_opt)[0];
            pp_rhs_m1[j][i] = (*g1_opt)[1];
        }
    }

    constexpr size_t kMirroredScratchRows = 23; // reduction rows only
    auto local_scratch_opt = allocate_mirrored_local_rows(
        src_mat0, src_mat1, kMirroredScratchRows, used_rows);
    if (!local_scratch_opt) {
        std::cout << "[ERROR]: No free mirrored scratch rows for Mult_4_bit_optimized\n";
        return {};
    }

    const auto& local_scratch = *local_scratch_opt;
    size_t scratch_idx = 0;
    auto next_pair = [&](uint64_t& row_m0, uint64_t& row_m1) {
        uint32_t local_row = local_scratch[scratch_idx++];
        row_m0 = make_addr(bank, make_global_row(src_mat0, local_row));
        row_m1 = make_addr(bank, make_global_row(src_mat1, local_row));
    };

    // Intermediate sums/carries in both MATs
    uint64_t s2a_m0 = 0, s2a_m1 = 0;
    uint64_t c3a_m0 = 0, c3a_m1 = 0;
    uint64_t c3b_m0 = 0, c3b_m1 = 0;
    next_pair(s2a_m0, s2a_m1);
    next_pair(c3a_m0, c3a_m1);
    next_pair(c3b_m0, c3b_m1);

    uint64_t s3a_m0 = 0, s3a_m1 = 0;
    uint64_t c4a_m0 = 0, c4a_m1 = 0;
    uint64_t s3b_m0 = 0, s3b_m1 = 0;
    uint64_t c4b_m0 = 0, c4b_m1 = 0;
    uint64_t c4c_m0 = 0, c4c_m1 = 0;
    next_pair(s3a_m0, s3a_m1);
    next_pair(c4a_m0, c4a_m1);
    next_pair(s3b_m0, s3b_m1);
    next_pair(c4b_m0, c4b_m1);
    next_pair(c4c_m0, c4c_m1);

    uint64_t s4a_m0 = 0, s4a_m1 = 0;
    uint64_t c5a_m0 = 0, c5a_m1 = 0;
    uint64_t s4b_m0 = 0, s4b_m1 = 0;
    uint64_t c5b_m0 = 0, c5b_m1 = 0;
    uint64_t c5c_m0 = 0, c5c_m1 = 0;
    next_pair(s4a_m0, s4a_m1);
    next_pair(c5a_m0, c5a_m1);
    next_pair(s4b_m0, s4b_m1);
    next_pair(c5b_m0, c5b_m1);
    next_pair(c5c_m0, c5c_m1);

    uint64_t s5a_m0 = 0, s5a_m1 = 0;
    uint64_t c6a_m0 = 0, c6a_m1 = 0;
    uint64_t s5b_m0 = 0, s5b_m1 = 0;
    uint64_t c6b_m0 = 0, c6b_m1 = 0;
    uint64_t c6c_m0 = 0, c6c_m1 = 0;
    next_pair(s5a_m0, s5a_m1);
    next_pair(c6a_m0, c6a_m1);
    next_pair(s5b_m0, s5b_m1);
    next_pair(c6b_m0, c6b_m1);
    next_pair(c6c_m0, c6c_m1);

    uint64_t s6a_m0 = 0, s6a_m1 = 0;
    uint64_t c7a_m0 = 0, c7a_m1 = 0;
    uint64_t c7b_m0 = 0, c7b_m1 = 0;
    uint64_t overflow_m0 = 0, overflow_m1 = 0;
    uint64_t final_carry_m0 = 0, final_carry_m1 = 0;
    next_pair(s6a_m0, s6a_m1);
    next_pair(c7a_m0, c7a_m1);
    next_pair(c7b_m0, c7b_m1);
    next_pair(overflow_m0, overflow_m1);
    next_pair(final_carry_m0, final_carry_m1);

    if (scratch_idx != kMirroredScratchRows) {
        std::cout << "[ERROR]: Internal scratch allocation mismatch in Mult_4_bit_optimized\n";
        return {};
    }

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    // Directly write AND result into each partial-product row (no final move needed).
    auto gen_pp_dual = [&](uint64_t a_m0, uint64_t b_m0,
                           uint64_t pp_row_m0, uint64_t pp_b_row_m0,
                           uint64_t pp_row_m1, uint64_t pp_b_row_m1) {
        std::vector<uint32_t> local;

        // MAT0
        auto t = single_Row_Copy(a_m0, pp_row_m0);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b_m0, pp_b_row_m0);
        local.insert(local.end(), t.begin(), t.end());
        t = AND(pp_row_m0, pp_b_row_m0);
        local.insert(local.end(), t.begin(), t.end());

        // MAT1
        t = single_Row_Copy(a_m0 + ROWS_PER_MAT, pp_row_m1);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b_m0 + ROWS_PER_MAT, pp_b_row_m1);
        local.insert(local.end(), t.begin(), t.end());
        t = AND(pp_row_m1, pp_b_row_m1);
        local.insert(local.end(), t.begin(), t.end());

        return local;
    };

    // auto gen_pp_m0 = [&](uint64_t a_m0, uint64_t b_m0,
    //                      uint64_t pp_row_m0, uint64_t pp_b_row_m0) {
    //     std::vector<uint32_t> local;
    //     auto t = single_Row_Copy(a_m0, pp_row_m0);
    //     local.insert(local.end(), t.begin(), t.end());
    //     t = single_Row_Copy(b_m0, pp_b_row_m0);
    //     local.insert(local.end(), t.begin(), t.end());
    //     t = AND(pp_row_m0, pp_b_row_m0);
    //     local.insert(local.end(), t.begin(), t.end());
    //     return local;
    // };

    // Generate all 16 partial products
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            append(gen_pp_dual(srcA[i], srcB[j],
                               pp_m0[j][i], pp_rhs_m0[j][i],
                               pp_m1[j][i], pp_rhs_m1[j][i]));
        }
    }

    // P0 = pp00
    append(single_Row_Copy(pp_m0[0][0], prod_m0_dst[0]));
    append(single_Row_Copy(pp_m1[0][0], prod_m1_dst[0]));

    // Keep ADD scratch allocation away from multiplier intermediates.
    ScopedAddNewReservedRows add_new_scope(&used_rows);

    // P1
    append(ADD_new_fast_for_mult(pp_m0[0][1], pp_m0[1][0], zero_m0,
                                 prod_m0_dst[1], prod_m1_dst[1],
                                 c3a_m0, c3a_m1));

    // P2
    append(ADD_new_fast_for_mult(pp_m0[0][2], pp_m0[1][1], c3a_m0,
                                 s2a_m0, s2a_m1,
                                 c3b_m0, c3b_m1));
    append(ADD_new_fast_for_mult(s2a_m0, pp_m0[2][0], zero_m0,
                                 prod_m0_dst[2], prod_m1_dst[2],
                                 c4a_m0, c4a_m1));

    // P3
    append(ADD_new_fast_for_mult(pp_m0[0][3], pp_m0[1][2], pp_m0[2][1],
                                 s3a_m0, s3a_m1,
                                 c4b_m0, c4b_m1));
    append(ADD_new_fast_for_mult(pp_m0[3][0], c3b_m0, c4a_m0,
                                 s3b_m0, s3b_m1,
                                 c4c_m0, c4c_m1));
    append(ADD_new_fast_for_mult(s3a_m0, s3b_m0, zero_m0,
                                 prod_m0_dst[3], prod_m1_dst[3],
                                 c5a_m0, c5a_m1));

    // P4
    append(ADD_new_fast_for_mult(pp_m0[1][3], pp_m0[2][2], pp_m0[3][1],
                                 s4a_m0, s4a_m1,
                                 c5b_m0, c5b_m1));
    append(ADD_new_fast_for_mult(c4b_m0, c4c_m0, c5a_m0,
                                 s4b_m0, s4b_m1,
                                 c5c_m0, c5c_m1));
    append(ADD_new_fast_for_mult(s4a_m0, s4b_m0, zero_m0,
                                 prod_m0_dst[4], prod_m1_dst[4],
                                 c6a_m0, c6a_m1));

    // P5
    append(ADD_new_fast_for_mult(pp_m0[2][3], pp_m0[3][2], c5b_m0,
                                 s5a_m0, s5a_m1,
                                 c6b_m0, c6b_m1));
    append(ADD_new_fast_for_mult(c5c_m0, c6a_m0, zero_m0,
                                 s5b_m0, s5b_m1,
                                 c6c_m0, c6c_m1));
    append(ADD_new_fast_for_mult(s5a_m0, s5b_m0, zero_m0,
                                 prod_m0_dst[5], prod_m1_dst[5],
                                 c7a_m0, c7a_m1));

    // P6
    append(ADD_new_fast_for_mult(pp_m0[3][3], c6b_m0, c6c_m0,
                                 s6a_m0, s6a_m1,
                                 c7b_m0, c7b_m1));
    append(ADD_new_fast_for_mult(s6a_m0, c7a_m0, zero_m0,
                                 prod_m0_dst[6], prod_m1_dst[6],
                                 overflow_m0, overflow_m1));

    // P7
    append(ADD_new_fast_for_mult(c7b_m0, overflow_m0, zero_m0,
                                 prod_m0_dst[7], prod_m1_dst[7],
                                 final_carry_m0, final_carry_m1)); // final overflow ignored

    return inst_list;
}

std::vector<uint32_t> Mult_4_bit_optimized_m0(
                                    std::vector<uint64_t> srcA,
                                    std::vector<uint64_t> srcB,
                                    std::vector<uint64_t> prod_m0_dst
) {
    std::vector<uint32_t> inst_list;

    if (srcA.size() != 4 || srcB.size() != 4 || prod_m0_dst.size() != 8) {
        std::cout << "[ERROR]: Mult_4_bit_optimized_m0 expects A/B=4 bits and product=8 bits\n";
        return {};
    }

    uint32_t bank = extract_bank_addr(srcA[0]);

    for (int i = 0; i < 4; ++i) {
        if (extract_bank_addr(srcA[i]) != bank ||
            extract_bank_addr(srcB[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    for (int i = 0; i < 8; ++i) {
        if (extract_bank_addr(prod_m0_dst[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    uint32_t src_mat0 = get_mat_id_from_row(extract_row_addr(srcA[0]));
    uint32_t src_mat1 = src_mat0 + 1;

    for (int i = 0; i < 4; ++i) {
        if (get_mat_id_from_row(extract_row_addr(srcA[i])) != src_mat0 ||
            get_mat_id_from_row(extract_row_addr(srcB[i])) != src_mat0) {
            std::cout << "[ERROR]: Mult_4_bit_optimized_m0 expects srcA/srcB in same MAT\n";
            return {};
        }
    }

    for (int i = 0; i < 8; ++i) {
        if (get_mat_id_from_row(extract_row_addr(prod_m0_dst[i])) != src_mat0) {
            std::cout << "[ERROR]: Mult_4_bit_optimized_m0 expects outputs in source MAT\n";
            return {};
        }
    }

    const uint64_t zero_m0 = make_addr(bank, make_global_row(src_mat0, temp_row_0));
    const uint64_t zero_m1 = make_addr(bank, make_global_row(src_mat1, temp_row_0));

    std::set<uint32_t> used_rows;
    for (auto x : srcA) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : srcB) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : prod_m0_dst) reserve_addr(used_rows, x);
    reserve_row(used_rows, make_global_row(src_mat0, temp_row_0));
    reserve_row(used_rows, make_global_row(src_mat0, temp_row_1));
    reserve_row(used_rows, make_global_row(src_mat1, temp_row_0));
    reserve_row(used_rows, make_global_row(src_mat1, temp_row_1));

    // Dedicated 2RA groups for each partial product.
    std::vector<std::vector<uint64_t>> pp_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_m1(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_rhs_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_rhs_m1(4, std::vector<uint64_t>(4));

    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            auto g0_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
            auto g1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);
            if (!g0_opt || !g1_opt) {
                std::cout << "[ERROR]: No free 2RA scratch groups for partial products in Mult_4_bit_optimized_m0\n";
                return {};
            }

            pp_m0[j][i] = (*g0_opt)[0];
            pp_rhs_m0[j][i] = (*g0_opt)[1];
            pp_m1[j][i] = (*g1_opt)[0];
            pp_rhs_m1[j][i] = (*g1_opt)[1];
        }
    }

    // Reduction intermediates that must remain mirrored across MAT0/MAT1.
    constexpr size_t kMirroredScratchRows = 22;
    auto local_scratch_opt = allocate_mirrored_local_rows(
        src_mat0, src_mat1, kMirroredScratchRows, used_rows);
    if (!local_scratch_opt) {
        std::cout << "[ERROR]: No free mirrored scratch rows for Mult_4_bit_optimized_m0\n";
        return {};
    }

    const auto& local_scratch = *local_scratch_opt;
    size_t scratch_idx = 0;
    auto next_pair = [&](uint64_t& row_m0, uint64_t& row_m1) {
        uint32_t local_row = local_scratch[scratch_idx++];
        row_m0 = make_addr(bank, make_global_row(src_mat0, local_row));
        row_m1 = make_addr(bank, make_global_row(src_mat1, local_row));
    };

    uint64_t s2a_m0 = 0, s2a_m1 = 0;
    uint64_t c3a_m0 = 0, c3a_m1 = 0;
    uint64_t c3b_m0 = 0, c3b_m1 = 0;
    next_pair(s2a_m0, s2a_m1);
    next_pair(c3a_m0, c3a_m1);
    next_pair(c3b_m0, c3b_m1);

    uint64_t s3a_m0 = 0, s3a_m1 = 0;
    uint64_t c4a_m0 = 0, c4a_m1 = 0;
    uint64_t s3b_m0 = 0, s3b_m1 = 0;
    uint64_t c4b_m0 = 0, c4b_m1 = 0;
    uint64_t c4c_m0 = 0, c4c_m1 = 0;
    next_pair(s3a_m0, s3a_m1);
    next_pair(c4a_m0, c4a_m1);
    next_pair(s3b_m0, s3b_m1);
    next_pair(c4b_m0, c4b_m1);
    next_pair(c4c_m0, c4c_m1);

    uint64_t s4a_m0 = 0, s4a_m1 = 0;
    uint64_t c5a_m0 = 0, c5a_m1 = 0;
    uint64_t s4b_m0 = 0, s4b_m1 = 0;
    uint64_t c5b_m0 = 0, c5b_m1 = 0;
    uint64_t c5c_m0 = 0, c5c_m1 = 0;
    next_pair(s4a_m0, s4a_m1);
    next_pair(c5a_m0, c5a_m1);
    next_pair(s4b_m0, s4b_m1);
    next_pair(c5b_m0, c5b_m1);
    next_pair(c5c_m0, c5c_m1);

    uint64_t s5a_m0 = 0, s5a_m1 = 0;
    uint64_t c6a_m0 = 0, c6a_m1 = 0;
    uint64_t s5b_m0 = 0, s5b_m1 = 0;
    uint64_t c6b_m0 = 0, c6b_m1 = 0;
    uint64_t c6c_m0 = 0, c6c_m1 = 0;
    next_pair(s5a_m0, s5a_m1);
    next_pair(c6a_m0, c6a_m1);
    next_pair(s5b_m0, s5b_m1);
    next_pair(c6b_m0, c6b_m1);
    next_pair(c6c_m0, c6c_m1);

    uint64_t s6a_m0 = 0, s6a_m1 = 0;
    uint64_t c7a_m0 = 0, c7a_m1 = 0;
    uint64_t c7b_m0 = 0, c7b_m1 = 0;
    uint64_t overflow_m0 = 0, overflow_m1 = 0;
    next_pair(s6a_m0, s6a_m1);
    next_pair(c7a_m0, c7a_m1);
    next_pair(c7b_m0, c7b_m1);
    next_pair(overflow_m0, overflow_m1);

    if (scratch_idx != kMirroredScratchRows) {
        std::cout << "[ERROR]: Internal scratch allocation mismatch in Mult_4_bit_optimized_m0\n";
        return {};
    }

    // Reused adder scratch rows.
    auto add_cout_m0_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
    auto add_cout_m1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);
    auto add_sum_m0_full_opt = allocate_group_rows(bank, src_mat0, three_ra_group_indices(), used_rows);
    auto add_sum_m1_full_opt = allocate_group_rows(bank, src_mat1, three_ra_group_indices(), used_rows);
    if (!add_cout_m0_opt || !add_cout_m1_opt || !add_sum_m0_full_opt || !add_sum_m1_full_opt) {
        std::cout << "[ERROR]: No free adder scratch groups for Mult_4_bit_optimized_m0\n";
        return {};
    }

    std::vector<uint64_t> add_cout_m0 = *add_cout_m0_opt;
    std::vector<uint64_t> add_cout_m1 = *add_cout_m1_opt;
    std::vector<uint64_t> add_sum_m0(add_sum_m0_full_opt->begin(),
                                     add_sum_m0_full_opt->begin() + 5);
    std::vector<uint64_t> add_sum_m1(add_sum_m1_full_opt->begin(),
                                     add_sum_m1_full_opt->begin() + 5);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    auto add_core = [&](uint64_t src1, uint64_t src2, uint64_t src3,
                        uint64_t sum_m0, uint64_t sum_m1,
                        uint64_t carry_m0, uint64_t carry_m1,
                        bool emit_sum_m1,
                        bool emit_carry_m0,
                        bool emit_carry_m1,
                        bool copy_sum_out) {
        std::vector<uint32_t> local;
        std::vector<uint32_t> t;

        // MAT0 fanout to carry+sum scratch.
        t = Multi_Row_Copy(src1, add_cout_m0[0], add_sum_m0[0]);
        local.insert(local.end(), t.begin(), t.end());
        t = Multi_Row_Copy(src2, add_cout_m0[1], add_sum_m0[1]);
        local.insert(local.end(), t.begin(), t.end());
        t = Multi_Row_Copy(src3, add_cout_m0[2], add_sum_m0[2]);
        local.insert(local.end(), t.begin(), t.end());

        // MAT1 carry path for generating ~carry into MAT0.
        if (emit_sum_m1) {
            t = Multi_Row_Copy(src1 + ROWS_PER_MAT, add_cout_m1[0], add_sum_m1[0]);
            local.insert(local.end(), t.begin(), t.end());
            t = Multi_Row_Copy(src2 + ROWS_PER_MAT, add_cout_m1[1], add_sum_m1[1]);
            local.insert(local.end(), t.begin(), t.end());
            t = Multi_Row_Copy(src3 + ROWS_PER_MAT, add_cout_m1[2], add_sum_m1[2]);
            local.insert(local.end(), t.begin(), t.end());
        } else {
            t = single_Row_Copy(src1 + ROWS_PER_MAT, add_cout_m1[0]);
            local.insert(local.end(), t.begin(), t.end());
            t = single_Row_Copy(src2 + ROWS_PER_MAT, add_cout_m1[1]);
            local.insert(local.end(), t.begin(), t.end());
            t = single_Row_Copy(src3 + ROWS_PER_MAT, add_cout_m1[2]);
            local.insert(local.end(), t.begin(), t.end());
        }

        t = Maj3(add_cout_m0[0], add_cout_m0[1], add_cout_m0[2]);
        local.insert(local.end(), t.begin(), t.end());
        if (emit_carry_m0) {
            t = single_Row_Copy(add_cout_m0[0], carry_m0);
            local.insert(local.end(), t.begin(), t.end());
        }

        t = Maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]);
        local.insert(local.end(), t.begin(), t.end());
        if (emit_carry_m1) {
            t = single_Row_Copy(add_cout_m1[0], carry_m1);
            local.insert(local.end(), t.begin(), t.end());
        }

        // SUM in MAT0.
        t = Multi_Row_Copy(add_cout_m1[0], add_sum_m0[3], add_sum_m0[4]);
        local.insert(local.end(), t.begin(), t.end());
        t = Maj5(add_sum_m0[0], add_sum_m0[1], add_sum_m0[2], add_sum_m0[3], add_sum_m0[4]);
        local.insert(local.end(), t.begin(), t.end());
        if (copy_sum_out) {
            t = single_Row_Copy(add_sum_m0[0], sum_m0);
            local.insert(local.end(), t.begin(), t.end());
        }

        // Optional mirrored SUM (needed only for intermediate values).
        if (emit_sum_m1) {
            t = Multi_Row_Copy(add_cout_m0[0], add_sum_m1[3], add_sum_m1[4]);
            local.insert(local.end(), t.begin(), t.end());
            t = Maj5(add_sum_m1[0], add_sum_m1[1], add_sum_m1[2], add_sum_m1[3], add_sum_m1[4]);
            local.insert(local.end(), t.begin(), t.end());
            if (copy_sum_out) {
                t = single_Row_Copy(add_sum_m1[0], sum_m1);
                local.insert(local.end(), t.begin(), t.end());
            }
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
        std::vector<uint32_t> t;

        // Build SUM inputs in MAT0.
        t = single_Row_Copy(a, add_sum_m0[0]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b, add_sum_m0[1]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(zero_m0, add_sum_m0[2]);
        local.insert(local.end(), t.begin(), t.end());

        // Build carry in MAT1 only (we do not need MAT0 carry in final stage).
        t = single_Row_Copy(a + ROWS_PER_MAT, add_cout_m1[0]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b + ROWS_PER_MAT, add_cout_m1[1]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(zero_m1, add_cout_m1[2]);
        local.insert(local.end(), t.begin(), t.end());
        t = Maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]);
        local.insert(local.end(), t.begin(), t.end());

        // MAT1 carry copied to MAT0 gives inverted carry on odd hop.
        t = Multi_Row_Copy(add_cout_m1[0], add_sum_m0[3], add_sum_m0[4]);
        local.insert(local.end(), t.begin(), t.end());

        t = Maj5(add_sum_m0[0], add_sum_m0[1], add_sum_m0[2], add_sum_m0[3], add_sum_m0[4]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(add_sum_m0[0], sum0);
        local.insert(local.end(), t.begin(), t.end());

        append(local);
    };

    auto gen_pp_dual = [&](uint64_t a_m0, uint64_t b_m0,
                           uint64_t pp_row_m0, uint64_t pp_b_row_m0,
                           uint64_t pp_row_m1, uint64_t pp_b_row_m1) {
        std::vector<uint32_t> local;

        auto t = single_Row_Copy(a_m0, pp_row_m0);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b_m0, pp_b_row_m0);
        local.insert(local.end(), t.begin(), t.end());
        t = AND(pp_row_m0, pp_b_row_m0);
        local.insert(local.end(), t.begin(), t.end());

        t = single_Row_Copy(a_m0 + ROWS_PER_MAT, pp_row_m1);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b_m0 + ROWS_PER_MAT, pp_b_row_m1);
        local.insert(local.end(), t.begin(), t.end());
        t = AND(pp_row_m1, pp_b_row_m1);
        local.insert(local.end(), t.begin(), t.end());

        return local;
    };

    auto gen_pp_m0 = [&](uint64_t a_m0, uint64_t b_m0,
                         uint64_t pp_row_m0, uint64_t pp_b_row_m0) {
        std::vector<uint32_t> local;

        auto t = single_Row_Copy(a_m0, pp_row_m0);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b_m0, pp_b_row_m0);
        local.insert(local.end(), t.begin(), t.end());
        t = AND(pp_row_m0, pp_b_row_m0);
        local.insert(local.end(), t.begin(), t.end());

        return local;
    };

    // Generate all 16 partial products in both MATs.
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            if (j == 0 && i == 0) {
                // P0 only needs MAT0 value; skip mirrored pp00 generation.
                append(gen_pp_m0(srcA[0], srcB[0], pp_m0[0][0], pp_rhs_m0[0][0]));
                continue;
            }
            append(gen_pp_dual(srcA[i], srcB[j],
                               pp_m0[j][i], pp_rhs_m0[j][i],
                               pp_m1[j][i], pp_rhs_m1[j][i]));
        }
    }

    // P0
    append(single_Row_Copy(pp_m0[0][0], prod_m0_dst[0]));

    // P1
    add_ab0_m0(pp_m0[0][1], pp_m0[1][0], prod_m0_dst[1], c3a_m0, c3a_m1);

    // P2
    add_abc_dual(pp_m0[0][2], pp_m0[1][1], c3a_m0, s2a_m0, s2a_m1, c3b_m0, c3b_m1);
    add_ab0_m0(s2a_m0, pp_m0[2][0], prod_m0_dst[2], c4a_m0, c4a_m1);

    // P3
    add_abc_dual(pp_m0[0][3], pp_m0[1][2], pp_m0[2][1], s3a_m0, s3a_m1, c4b_m0, c4b_m1);
    add_abc_dual(pp_m0[3][0], c3b_m0, c4a_m0, s3b_m0, s3b_m1, c4c_m0, c4c_m1);
    add_ab0_m0(s3a_m0, s3b_m0, prod_m0_dst[3], c5a_m0, c5a_m1);

    // P4
    add_abc_dual(pp_m0[1][3], pp_m0[2][2], pp_m0[3][1], s4a_m0, s4a_m1, c5b_m0, c5b_m1);
    add_abc_dual(c4b_m0, c4c_m0, c5a_m0, s4b_m0, s4b_m1, c5c_m0, c5c_m1);
    add_ab0_m0(s4a_m0, s4b_m0, prod_m0_dst[4], c6a_m0, c6a_m1);

    // P5
    add_abc_dual(pp_m0[2][3], pp_m0[3][2], c5b_m0, s5a_m0, s5a_m1, c6b_m0, c6b_m1);
    add_ab0_dual(c5c_m0, c6a_m0, s5b_m0, s5b_m1, c6c_m0, c6c_m1);
    add_ab0_m0(s5a_m0, s5b_m0, prod_m0_dst[5], c7a_m0, c7a_m1);

    // P6
    add_abc_dual(pp_m0[3][3], c6b_m0, c6c_m0, s6a_m0, s6a_m1, c7b_m0, c7b_m1);
    add_ab0_m0(s6a_m0, c7a_m0, prod_m0_dst[6], overflow_m0, overflow_m1);

    // P7 (final carry-out ignored)
    add_ab0_m0_nocarry_fast(c7b_m0, overflow_m0, prod_m0_dst[7]);

    (void)zero_m1;
    return inst_list;
}

static std::vector<uint32_t> Mult_4_bit_m0_variant(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0_dst,
    bool use_csa_schedule,
    bool use_maj5_via_maj3
) {
    std::vector<uint32_t> inst_list;

    if (srcA.size() != 4 || srcB.size() != 4 || prod_m0_dst.size() != 8) {
        std::cout << "[ERROR]: Mult_4_bit_optimized_m0_1 expects A/B=4 bits and product=8 bits\n";
        return {};
    }

    uint32_t bank = extract_bank_addr(srcA[0]);

    for (int i = 0; i < 4; ++i) {
        if (extract_bank_addr(srcA[i]) != bank ||
            extract_bank_addr(srcB[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    for (int i = 0; i < 8; ++i) {
        if (extract_bank_addr(prod_m0_dst[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    uint32_t src_mat0 = get_mat_id_from_row(extract_row_addr(srcA[0]));
    uint32_t src_mat1 = src_mat0 + 1;

    for (int i = 0; i < 4; ++i) {
        if (get_mat_id_from_row(extract_row_addr(srcA[i])) != src_mat0 ||
            get_mat_id_from_row(extract_row_addr(srcB[i])) != src_mat0) {
            std::cout << "[ERROR]: Mult_4_bit_optimized_m0_1 expects srcA/srcB in same MAT\n";
            return {};
        }
    }

    for (int i = 0; i < 8; ++i) {
        if (get_mat_id_from_row(extract_row_addr(prod_m0_dst[i])) != src_mat0) {
            std::cout << "[ERROR]: Mult_4_bit_optimized_m0_1 expects outputs in source MAT\n";
            return {};
        }
    }

    const uint64_t zero_m0 = make_addr(bank, make_global_row(src_mat0, temp_row_0));
    const uint64_t zero_m1 = make_addr(bank, make_global_row(src_mat1, temp_row_0));

    std::set<uint32_t> used_rows;
    for (auto x : srcA) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : srcB) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : prod_m0_dst) reserve_addr_with_next_mat(used_rows, x);
    reserve_row(used_rows, make_global_row(src_mat0, temp_row_0));
    reserve_row(used_rows, make_global_row(src_mat0, temp_row_1));
    reserve_row(used_rows, make_global_row(src_mat1, temp_row_0));
    reserve_row(used_rows, make_global_row(src_mat1, temp_row_1));

    auto allocate_group_rows_with_anchor = [&](uint32_t mat_id,
                                               const std::vector<size_t>& group_candidates,
                                               uint32_t anchor_global_row) -> std::optional<std::vector<uint64_t>> {
        if (get_mat_id_from_row(anchor_global_row) != mat_id) {
            return std::nullopt;
        }

        for (size_t group_index : group_candidates) {
            if (group_index >= SUPPORTED_GROUPS.size()) {
                continue;
            }

            const auto& offsets = SUPPORTED_GROUPS[group_index].offsets;
            if (offsets.empty()) {
                continue;
            }

            const uint32_t max_offset = offsets.back();
            if (max_offset >= ROWS_PER_MAT) {
                continue;
            }

            for (uint32_t base_local = 0; base_local + max_offset < ROWS_PER_MAT; ++base_local) {
                bool contains_anchor = false;
                bool free = true;
                std::vector<uint32_t> candidate_rows;
                candidate_rows.reserve(offsets.size());

                for (uint32_t offset : offsets) {
                    const uint32_t local_row = base_local + offset;
                    const uint32_t global_row = make_global_row(mat_id, local_row);

                    if (global_row == anchor_global_row) {
                        contains_anchor = true;
                    } else if (used_rows.find(global_row) != used_rows.end()) {
                        free = false;
                        break;
                    }

                    candidate_rows.push_back(global_row);
                }

                if (!free || !contains_anchor) {
                    continue;
                }

                std::vector<uint64_t> addrs;
                addrs.reserve(candidate_rows.size());
                for (uint32_t global_row : candidate_rows) {
                    if (global_row != anchor_global_row) {
                        used_rows.insert(global_row);
                    }
                    addrs.push_back(make_addr(bank, global_row));
                }

                return addrs;
            }
        }

        return std::nullopt;
    };

    // Dedicated 2RA groups for each partial product.
    std::vector<std::vector<uint64_t>> pp_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_m1(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_lhs_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_lhs_m1(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_rhs_m0(4, std::vector<uint64_t>(4));
    std::vector<std::vector<uint64_t>> pp_rhs_m1(4, std::vector<uint64_t>(4));
    bool pp00_direct_to_p0 = false;

    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            if (j == 0 && i == 0) {
                // Allocate regular MAT0 scratch for pp00. Hardware observes the
                // reliable AND result in the FRAC/output row, not necessarily
                // the left input row, so do not anchor pp00 directly on P0.
                auto g0_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
                auto g1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);
                if (!g0_opt || !g1_opt) {
                    std::cout << "[ERROR]: No free 2RA scratch group for pp00 in Mult_4_bit_optimized_m0_1\n";
                    return {};
                }
                pp_lhs_m0[0][0] = (*g0_opt)[0];
                pp_rhs_m0[0][0] = (*g0_opt)[1];
                pp_m0[0][0] = (*g0_opt)[3];
                pp_lhs_m1[0][0] = (*g1_opt)[0];
                pp_rhs_m1[0][0] = (*g1_opt)[1];
                pp_m1[0][0] = (*g1_opt)[3];
                pp00_direct_to_p0 = false;
                continue;
            }

            auto g0_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
            auto g1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);
            if (!g0_opt || !g1_opt) {
                std::cout << "[ERROR]: No free 2RA scratch groups for partial products in Mult_4_bit_optimized_m0_1\n";
                return {};
            }

            pp_lhs_m0[j][i] = (*g0_opt)[0];
            pp_rhs_m0[j][i] = (*g0_opt)[1];
            pp_m0[j][i] = (*g0_opt)[3];
            pp_lhs_m1[j][i] = (*g1_opt)[0];
            pp_rhs_m1[j][i] = (*g1_opt)[1];
            pp_m1[j][i] = (*g1_opt)[3];
        }
    }

    // Reduction intermediates that must remain mirrored across MAT0/MAT1.
    constexpr size_t kMirroredScratchRows = 22;
    auto local_scratch_opt = allocate_mirrored_local_rows(
        src_mat0, src_mat1, kMirroredScratchRows, used_rows);
    if (!local_scratch_opt) {
        std::cout << "[ERROR]: No free mirrored scratch rows for Mult_4_bit_optimized_m0_1\n";
        return {};
    }

    const auto& local_scratch = *local_scratch_opt;
    size_t scratch_idx = 0;
    auto next_pair = [&](uint64_t& row_m0, uint64_t& row_m1) {
        uint32_t local_row = local_scratch[scratch_idx++];
        row_m0 = make_addr(bank, make_global_row(src_mat0, local_row));
        row_m1 = make_addr(bank, make_global_row(src_mat1, local_row));
    };

    uint64_t s2a_m0 = 0, s2a_m1 = 0;
    uint64_t c3a_m0 = 0, c3a_m1 = 0;
    uint64_t c3b_m0 = 0, c3b_m1 = 0;
    next_pair(s2a_m0, s2a_m1);
    next_pair(c3a_m0, c3a_m1);
    next_pair(c3b_m0, c3b_m1);

    uint64_t s3a_m0 = 0, s3a_m1 = 0;
    uint64_t c4a_m0 = 0, c4a_m1 = 0;
    uint64_t s3b_m0 = 0, s3b_m1 = 0;
    uint64_t c4b_m0 = 0, c4b_m1 = 0;
    uint64_t c4c_m0 = 0, c4c_m1 = 0;
    next_pair(s3a_m0, s3a_m1);
    next_pair(c4a_m0, c4a_m1);
    next_pair(s3b_m0, s3b_m1);
    next_pair(c4b_m0, c4b_m1);
    next_pair(c4c_m0, c4c_m1);

    uint64_t s4a_m0 = 0, s4a_m1 = 0;
    uint64_t c5a_m0 = 0, c5a_m1 = 0;
    uint64_t s4b_m0 = 0, s4b_m1 = 0;
    uint64_t c5b_m0 = 0, c5b_m1 = 0;
    uint64_t c5c_m0 = 0, c5c_m1 = 0;
    next_pair(s4a_m0, s4a_m1);
    next_pair(c5a_m0, c5a_m1);
    next_pair(s4b_m0, s4b_m1);
    next_pair(c5b_m0, c5b_m1);
    next_pair(c5c_m0, c5c_m1);

    uint64_t s5a_m0 = 0, s5a_m1 = 0;
    uint64_t c6a_m0 = 0, c6a_m1 = 0;
    uint64_t s5b_m0 = 0, s5b_m1 = 0;
    uint64_t c6b_m0 = 0, c6b_m1 = 0;
    uint64_t c6c_m0 = 0, c6c_m1 = 0;
    next_pair(s5a_m0, s5a_m1);
    next_pair(c6a_m0, c6a_m1);
    next_pair(s5b_m0, s5b_m1);
    next_pair(c6b_m0, c6b_m1);
    next_pair(c6c_m0, c6c_m1);

    uint64_t s6a_m0 = 0, s6a_m1 = 0;
    uint64_t c7a_m0 = 0, c7a_m1 = 0;
    uint64_t c7b_m0 = 0, c7b_m1 = 0;
    uint64_t overflow_m0 = 0, overflow_m1 = 0;
    next_pair(s6a_m0, s6a_m1);
    next_pair(c7a_m0, c7a_m1);
    next_pair(c7b_m0, c7b_m1);
    next_pair(overflow_m0, overflow_m1);

    if (scratch_idx != kMirroredScratchRows) {
        std::cout << "[ERROR]: Internal scratch allocation mismatch in Mult_4_bit_optimized_m0_1\n";
        return {};
    }

    // Reused adder scratch rows.
    auto add_cout_m0_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
    auto add_cout_m1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);
    auto add_sum_m0_full_opt = allocate_group_rows(bank, src_mat0, three_ra_group_indices(), used_rows);
    auto add_sum_m1_full_opt = allocate_group_rows(bank, src_mat1, three_ra_group_indices(), used_rows);
    if (!add_cout_m0_opt || !add_cout_m1_opt || !add_sum_m0_full_opt || !add_sum_m1_full_opt) {
        std::cout << "[ERROR]: No free adder scratch groups for Mult_4_bit_optimized_m0_1\n";
        return {};
    }

    std::vector<uint64_t> add_cout_m0 = *add_cout_m0_opt;
    std::vector<uint64_t> add_cout_m1 = *add_cout_m1_opt;
    std::vector<uint64_t> add_sum_m0(add_sum_m0_full_opt->begin(),
                                     add_sum_m0_full_opt->begin() + 5);
    std::vector<uint64_t> add_sum_m1(add_sum_m1_full_opt->begin(),
                                     add_sum_m1_full_opt->begin() + 5);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    ScopedAddNewReservedRows mult4_internal_scope(&used_rows);
    bool generation_ok = true;

    auto append_maj5_sum = [&](std::vector<uint32_t>& local,
                               uint64_t src1,
                               uint64_t src2,
                               uint64_t src3,
                               uint64_t src4,
                               uint64_t src5) {
        std::vector<uint32_t> t;
        if (use_maj5_via_maj3) {
            t = Maj5_via_Maj3_sum_inplace(src1, src2, src3, src4, src5);
        } else {
            t = Maj5(src1, src2, src3, src4, src5);
        }
        if (t.empty()) {
            generation_ok = false;
        }
        local.insert(local.end(), t.begin(), t.end());
    };

    auto gen_pp_dual = [&](uint64_t a_m0, uint64_t b_m0,
                           uint64_t pp_row_m0, uint64_t pp_b_row_m0,
                           uint64_t pp_row_m1, uint64_t pp_b_row_m1) {
        std::vector<uint32_t> local;

        auto t = single_Row_Copy(a_m0, pp_row_m0);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b_m0, pp_b_row_m0);
        local.insert(local.end(), t.begin(), t.end());
        t = AND(pp_row_m0, pp_b_row_m0);
        local.insert(local.end(), t.begin(), t.end());

        t = single_Row_Copy(a_m0 + ROWS_PER_MAT, pp_row_m1);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b_m0 + ROWS_PER_MAT, pp_b_row_m1);
        local.insert(local.end(), t.begin(), t.end());
        t = AND(pp_row_m1, pp_b_row_m1);
        local.insert(local.end(), t.begin(), t.end());

        return local;
    };

    auto gen_pp_m0 = [&](uint64_t a_m0, uint64_t b_m0,
                         uint64_t pp_row_m0, uint64_t pp_b_row_m0) {
        std::vector<uint32_t> local;

        auto t = single_Row_Copy(a_m0, pp_row_m0);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b_m0, pp_b_row_m0);
        local.insert(local.end(), t.begin(), t.end());
        t = AND(pp_row_m0, pp_b_row_m0);
        local.insert(local.end(), t.begin(), t.end());

        return local;
    };
    (void)gen_pp_dual;
    (void)gen_pp_m0;

    auto add_core = [&](uint64_t src1, uint64_t src2, uint64_t src3,
                        uint64_t sum_m0, uint64_t sum_m1,
                        uint64_t carry_m0, uint64_t carry_m1,
                        bool emit_sum_m1,
                        bool emit_carry_m0,
                        bool emit_carry_m1,
                        bool copy_sum_out) {
        std::vector<uint32_t> local;
        std::vector<uint32_t> t;

        // MAT0 fanout to carry+sum scratch.
        t = Multi_Row_Copy(src1, add_cout_m0[0], add_sum_m0[0]);
        local.insert(local.end(), t.begin(), t.end());
        t = Multi_Row_Copy(src2, add_cout_m0[1], add_sum_m0[1]);
        local.insert(local.end(), t.begin(), t.end());
        t = Multi_Row_Copy(src3, add_cout_m0[2], add_sum_m0[2]);
        local.insert(local.end(), t.begin(), t.end());

        // MAT1 carry path for generating ~carry into MAT0.
        if (emit_sum_m1) {
            t = Multi_Row_Copy(src1 + ROWS_PER_MAT, add_cout_m1[0], add_sum_m1[0]);
            local.insert(local.end(), t.begin(), t.end());
            t = Multi_Row_Copy(src2 + ROWS_PER_MAT, add_cout_m1[1], add_sum_m1[1]);
            local.insert(local.end(), t.begin(), t.end());
            t = Multi_Row_Copy(src3 + ROWS_PER_MAT, add_cout_m1[2], add_sum_m1[2]);
            local.insert(local.end(), t.begin(), t.end());
        } else {
            t = single_Row_Copy(src1 + ROWS_PER_MAT, add_cout_m1[0]);
            local.insert(local.end(), t.begin(), t.end());
            t = single_Row_Copy(src2 + ROWS_PER_MAT, add_cout_m1[1]);
            local.insert(local.end(), t.begin(), t.end());
            t = single_Row_Copy(src3 + ROWS_PER_MAT, add_cout_m1[2]);
            local.insert(local.end(), t.begin(), t.end());
        }

        t = Maj3(add_cout_m0[0], add_cout_m0[1], add_cout_m0[2]);
        local.insert(local.end(), t.begin(), t.end());
        if (emit_carry_m0) {
            t = single_Row_Copy(add_cout_m0[0], carry_m0);
            local.insert(local.end(), t.begin(), t.end());
        }

        t = Maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]);
        local.insert(local.end(), t.begin(), t.end());
        if (emit_carry_m1) {
            t = single_Row_Copy(add_cout_m1[0], carry_m1);
            local.insert(local.end(), t.begin(), t.end());
        }

        // SUM in MAT0.
        t = Multi_Row_Copy(add_cout_m1[0], add_sum_m0[3], add_sum_m0[4]);
        local.insert(local.end(), t.begin(), t.end());
        append_maj5_sum(local, add_sum_m0[0], add_sum_m0[1], add_sum_m0[2],
                        add_sum_m0[3], add_sum_m0[4]);
        if (copy_sum_out) {
            t = single_Row_Copy(add_sum_m0[0], sum_m0);
            local.insert(local.end(), t.begin(), t.end());
        }

        // Optional mirrored SUM (needed only for intermediate values).
        if (emit_sum_m1) {
            t = Multi_Row_Copy(add_cout_m0[0], add_sum_m1[3], add_sum_m1[4]);
            local.insert(local.end(), t.begin(), t.end());
            append_maj5_sum(local, add_sum_m1[0], add_sum_m1[1], add_sum_m1[2],
                            add_sum_m1[3], add_sum_m1[4]);
            if (copy_sum_out) {
                t = single_Row_Copy(add_sum_m1[0], sum_m1);
                local.insert(local.end(), t.begin(), t.end());
            }
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

    auto allocate_sum_rows_with_output_anchor =
        [&](uint64_t sum_out_m0) -> std::optional<std::array<uint64_t, 5>> {
            const uint32_t out_global_row = extract_row_addr(sum_out_m0);
            auto group_opt = allocate_group_rows_with_anchor(
                src_mat0, three_ra_group_indices(), out_global_row);
            if (!group_opt) {
                return std::nullopt;
            }

            std::array<uint64_t, 5> rows{};
            rows[0] = sum_out_m0;
            size_t fill = 1;
            for (uint64_t addr : *group_opt) {
                if (extract_row_addr(addr) == out_global_row) {
                    continue;
                }
                if (fill < rows.size()) {
                    rows[fill++] = addr;
                }
            }

            if (fill != rows.size()) {
                return std::nullopt;
            }

            return rows;
        };

    auto add_ab0_m0_direct_out = [&](uint64_t a, uint64_t b,
                                     uint64_t sum0, uint64_t carry0, uint64_t carry1,
                                     bool emit_carry_m1 = true) {
        auto rows_opt = allocate_sum_rows_with_output_anchor(sum0);
        if (!rows_opt) {
            if (emit_carry_m1) {
                add_ab0_m0(a, b, sum0, carry0, carry1);
            } else {
                append(add_core(a, b, zero_m0, sum0, 0, carry0, carry1, false, true, false, true));
            }
            return;
        }

        const auto rows = *rows_opt;
        std::vector<uint32_t> local;
        std::vector<uint32_t> t;

        // MAT0 carry+sum fanout (sum0 is anchored as row[0]).
        t = Multi_Row_Copy(a, add_cout_m0[0], rows[0]);
        local.insert(local.end(), t.begin(), t.end());
        t = Multi_Row_Copy(b, add_cout_m0[1], rows[1]);
        local.insert(local.end(), t.begin(), t.end());
        t = Multi_Row_Copy(zero_m0, add_cout_m0[2], rows[2]);
        local.insert(local.end(), t.begin(), t.end());

        // MAT1 carry path.
        t = single_Row_Copy(a + ROWS_PER_MAT, add_cout_m1[0]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b + ROWS_PER_MAT, add_cout_m1[1]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(zero_m1, add_cout_m1[2]);
        local.insert(local.end(), t.begin(), t.end());

        t = Maj3(add_cout_m0[0], add_cout_m0[1], add_cout_m0[2]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(add_cout_m0[0], carry0);
        local.insert(local.end(), t.begin(), t.end());

        t = Maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]);
        local.insert(local.end(), t.begin(), t.end());
        if (emit_carry_m1) {
            t = single_Row_Copy(add_cout_m1[0], carry1);
            local.insert(local.end(), t.begin(), t.end());
        }

        // Build two ~carry replicas in the anchored sum group.
        t = Multi_Row_Copy(add_cout_m1[0], rows[3], rows[4]);
        local.insert(local.end(), t.begin(), t.end());

        append_maj5_sum(local, rows[0], rows[1], rows[2], rows[3], rows[4]);

        append(local);
    };

    auto add_abc_m0_direct_out = [&](uint64_t a, uint64_t b, uint64_t c,
                                     uint64_t sum0, uint64_t carry0, uint64_t carry1,
                                     bool emit_carry_m1 = true) {
        auto rows_opt = allocate_sum_rows_with_output_anchor(sum0);
        if (!rows_opt) {
            append(add_core(a, b, c, sum0, 0, carry0, carry1, false, true, emit_carry_m1, true));
            return;
        }

        const auto rows = *rows_opt;
        std::vector<uint32_t> local;
        std::vector<uint32_t> t;

        // MAT0 carry+sum fanout (sum0 is anchored as row[0]).
        t = Multi_Row_Copy(a, add_cout_m0[0], rows[0]);
        local.insert(local.end(), t.begin(), t.end());
        t = Multi_Row_Copy(b, add_cout_m0[1], rows[1]);
        local.insert(local.end(), t.begin(), t.end());
        t = Multi_Row_Copy(c, add_cout_m0[2], rows[2]);
        local.insert(local.end(), t.begin(), t.end());

        // MAT1 carry path.
        t = single_Row_Copy(a + ROWS_PER_MAT, add_cout_m1[0]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b + ROWS_PER_MAT, add_cout_m1[1]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(c + ROWS_PER_MAT, add_cout_m1[2]);
        local.insert(local.end(), t.begin(), t.end());

        t = Maj3(add_cout_m0[0], add_cout_m0[1], add_cout_m0[2]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(add_cout_m0[0], carry0);
        local.insert(local.end(), t.begin(), t.end());

        t = Maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]);
        local.insert(local.end(), t.begin(), t.end());
        if (emit_carry_m1) {
            t = single_Row_Copy(add_cout_m1[0], carry1);
            local.insert(local.end(), t.begin(), t.end());
        }

        // Build two ~carry replicas in anchored sum group and compute SUM directly into row[0].
        t = Multi_Row_Copy(add_cout_m1[0], rows[3], rows[4]);
        local.insert(local.end(), t.begin(), t.end());
        append_maj5_sum(local, rows[0], rows[1], rows[2], rows[3], rows[4]);

        append(local);
    };
    (void)add_abc_m0_direct_out;

    auto add_ab0_m0_nocarry_fast = [&](uint64_t a, uint64_t b, uint64_t sum0) {
        std::vector<uint32_t> local;
        std::vector<uint32_t> t;

        t = single_Row_Copy(a, add_sum_m0[0]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b, add_sum_m0[1]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(zero_m0, add_sum_m0[2]);
        local.insert(local.end(), t.begin(), t.end());

        t = single_Row_Copy(a + ROWS_PER_MAT, add_cout_m1[0]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b + ROWS_PER_MAT, add_cout_m1[1]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(zero_m1, add_cout_m1[2]);
        local.insert(local.end(), t.begin(), t.end());
        t = Maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]);
        local.insert(local.end(), t.begin(), t.end());

        t = Multi_Row_Copy(add_cout_m1[0], add_sum_m0[3], add_sum_m0[4]);
        local.insert(local.end(), t.begin(), t.end());

        append_maj5_sum(local, add_sum_m0[0], add_sum_m0[1], add_sum_m0[2],
                        add_sum_m0[3], add_sum_m0[4]);
        t = single_Row_Copy(add_sum_m0[0], sum0);
        local.insert(local.end(), t.begin(), t.end());

        append(local);
    };

    auto add_ab0_m0_nocarry_direct_out = [&](uint64_t a, uint64_t b, uint64_t sum0) {
        auto rows_opt = allocate_sum_rows_with_output_anchor(sum0);
        if (!rows_opt) {
            add_ab0_m0_nocarry_fast(a, b, sum0);
            return;
        }

        const auto rows = *rows_opt;
        std::vector<uint32_t> local;
        std::vector<uint32_t> t;

        t = single_Row_Copy(a, rows[0]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b, rows[1]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(zero_m0, rows[2]);
        local.insert(local.end(), t.begin(), t.end());

        t = single_Row_Copy(a + ROWS_PER_MAT, add_cout_m1[0]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(b + ROWS_PER_MAT, add_cout_m1[1]);
        local.insert(local.end(), t.begin(), t.end());
        t = single_Row_Copy(zero_m1, add_cout_m1[2]);
        local.insert(local.end(), t.begin(), t.end());
        t = Maj3(add_cout_m1[0], add_cout_m1[1], add_cout_m1[2]);
        local.insert(local.end(), t.begin(), t.end());

        t = Multi_Row_Copy(add_cout_m1[0], rows[3], rows[4]);
        local.insert(local.end(), t.begin(), t.end());
        append_maj5_sum(local, rows[0], rows[1], rows[2], rows[3], rows[4]);

        append(local);
    };

    auto add_ab0_m0_copy_out = [&](uint64_t a, uint64_t b,
                                   uint64_t sum0, uint64_t carry0, uint64_t carry1,
                                   bool emit_carry_m1 = true) {
        append(add_core(a, b, zero_m0,
                        sum0, 0,
                        carry0, carry1,
                        false, true, emit_carry_m1, true));
    };

    auto add_abc_m0_copy_out = [&](uint64_t a, uint64_t b, uint64_t c,
                                   uint64_t sum0, uint64_t carry0, uint64_t carry1,
                                   bool emit_carry_m1 = true) {
        append(add_core(a, b, c,
                        sum0, 0,
                        carry0, carry1,
                        false, true, emit_carry_m1, true));
    };

    auto broadcast_to_rows = [&](uint64_t src, const std::vector<uint64_t>& dests) {
        if (dests.empty()) {
            return;
        }
        if (dests.size() == 1) {
            append(single_Row_Copy(src, dests[0]));
            return;
        }
        if (dests.size() == 2) {
            append(Multi_Row_Copy(src, dests[0], dests[1]));
            return;
        }
        if (dests.size() == 3) {
            append(Multi_Row_Copy(src, dests[0], dests[1]));
            append(single_Row_Copy(src, dests[2]));
            return;
        }

        size_t i = 0;
        for (; i + 1 < dests.size(); i += 2) {
            append(Multi_Row_Copy(src, dests[i], dests[i + 1]));
        }
        if (i < dests.size()) {
            append(single_Row_Copy(src, dests[i]));
        }
    };

    // Generate partial products with broadcast fanout to reduce setup copies.
    // 1) Broadcast A operands into pp rows.
    for (int i = 0; i < 4; ++i) {
        std::vector<uint64_t> mat0_dsts;
        std::vector<uint64_t> mat1_dsts;
        mat0_dsts.reserve(4);
        mat1_dsts.reserve(4);
        for (int j = 0; j < 4; ++j) {
            mat0_dsts.push_back(pp_lhs_m0[j][i]);
            if (!(j == 0 && i == 0)) {
                mat1_dsts.push_back(pp_lhs_m1[j][i]);
            }
        }
        broadcast_to_rows(srcA[i], mat0_dsts);
        broadcast_to_rows(srcA[i] + ROWS_PER_MAT, mat1_dsts);
    }

    // 2) Broadcast B operands into rhs rows.
    for (int j = 0; j < 4; ++j) {
        std::vector<uint64_t> mat0_dsts;
        std::vector<uint64_t> mat1_dsts;
        mat0_dsts.reserve(4);
        mat1_dsts.reserve(4);
        for (int i = 0; i < 4; ++i) {
            mat0_dsts.push_back(pp_rhs_m0[j][i]);
            if (!(j == 0 && i == 0)) {
                mat1_dsts.push_back(pp_rhs_m1[j][i]);
            }
        }
        broadcast_to_rows(srcB[j], mat0_dsts);
        broadcast_to_rows(srcB[j] + ROWS_PER_MAT, mat1_dsts);
    }

    // 3) Evaluate ANDs for each partial product.
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            append(AND(pp_lhs_m0[j][i], pp_rhs_m0[j][i]));
            if (!(j == 0 && i == 0)) {
                append(AND(pp_lhs_m1[j][i], pp_rhs_m1[j][i]));
            }
        }
    }

    if (use_csa_schedule) {
        // P0
        if (!pp00_direct_to_p0) {
            append(single_Row_Copy(pp_m0[0][0], prod_m0_dst[0]));
        }

        // Carry rows reused as CPA chain:
        // c2 -> c3b, c3 -> c4c, c4 -> c5c, c5 -> c6b, c6 -> c6c, c7 -> c7b
        add_ab0_m0_copy_out(pp_m0[0][1], pp_m0[1][0], prod_m0_dst[1], c3b_m0, c3b_m1);
        add_abc_dual_ephemeral_sum(pp_m0[0][2], pp_m0[1][1], pp_m0[2][0], c3a_m0, c3a_m1);
        add_ab0_m0_copy_out(ephem_sum_m0, c3b_m0, prod_m0_dst[2], c4c_m0, c4c_m1);
        add_abc_dual_ephemeral_sum(pp_m0[0][3], pp_m0[1][2], pp_m0[2][1], c4a_m0, c4a_m1);
        add_abc_dual_ephemeral_sum(ephem_sum_m0, pp_m0[3][0], c3a_m0, c4b_m0, c4b_m1);
        add_ab0_m0_copy_out(ephem_sum_m0, c4c_m0, prod_m0_dst[3], c5c_m0, c5c_m1);
        add_abc_dual_ephemeral_sum(pp_m0[1][3], pp_m0[2][2], pp_m0[3][1], c5a_m0, c5a_m1);
        add_abc_dual_ephemeral_sum(ephem_sum_m0, c4a_m0, c4b_m0, c5b_m0, c5b_m1);
        add_ab0_m0_copy_out(ephem_sum_m0, c5c_m0, prod_m0_dst[4], c6b_m0, c6b_m1);
        add_abc_dual_ephemeral_sum(pp_m0[2][3], pp_m0[3][2], c5a_m0, c6a_m0, c6a_m1);
        add_abc_m0_copy_out(ephem_sum_m0, c5b_m0, c6b_m0,
                            prod_m0_dst[5], c6c_m0, c6c_m1, true);
        add_abc_m0_copy_out(pp_m0[3][3], c6a_m0, c6c_m0,
                            prod_m0_dst[6], prod_m0_dst[7], 0, false);
    } else {
        // P0
        if (!pp00_direct_to_p0) {
            append(single_Row_Copy(pp_m0[0][0], prod_m0_dst[0]));
        }

        // P1
        add_ab0_m0_direct_out(pp_m0[0][1], pp_m0[1][0], prod_m0_dst[1], c3a_m0, c3a_m1);

        // P2
        add_abc_dual_ephemeral_sum(pp_m0[0][2], pp_m0[1][1], c3a_m0, c3b_m0, c3b_m1);
        add_ab0_m0_direct_out(ephem_sum_m0, pp_m0[2][0], prod_m0_dst[2], c4a_m0, c4a_m1);

        // P3 
        add_abc_dual(pp_m0[0][3], pp_m0[1][2], pp_m0[2][1], s3a_m0, s3a_m1, c4b_m0, c4b_m1);
        add_abc_dual_ephemeral_sum(pp_m0[3][0], c3b_m0, c4a_m0, c4c_m0, c4c_m1);
        add_ab0_m0_direct_out(s3a_m0, ephem_sum_m0, prod_m0_dst[3], c5a_m0, c5a_m1);

        // P4
        add_abc_dual(pp_m0[1][3], pp_m0[2][2], pp_m0[3][1], s4a_m0, s4a_m1, c5b_m0, c5b_m1);
        add_abc_dual_ephemeral_sum(c4b_m0, c4c_m0, c5a_m0, c5c_m0, c5c_m1);
        add_ab0_m0_direct_out(s4a_m0, ephem_sum_m0, prod_m0_dst[4], c6a_m0, c6a_m1);

        // P5
        add_abc_dual(pp_m0[2][3], pp_m0[3][2], c5b_m0, s5a_m0, s5a_m1, c6b_m0, c6b_m1);
        add_ab0_dual_ephemeral_sum(c5c_m0, c6a_m0, c6c_m0, c6c_m1);
        add_ab0_m0_direct_out(s5a_m0, ephem_sum_m0, prod_m0_dst[5], c7a_m0, c7a_m1);

        // P6
        add_abc_dual_ephemeral_sum(pp_m0[3][3], c6b_m0, c6c_m0, c7b_m0, c7b_m1);
        add_ab0_m0_direct_out(ephem_sum_m0, c7a_m0, prod_m0_dst[6], overflow_m0, overflow_m1);

        // P7
        add_ab0_m0_nocarry_direct_out(c7b_m0, overflow_m0, prod_m0_dst[7]);
    }

    if (!generation_ok) {
        std::cout << "[ERROR]: Mult_4_bit_m0_variant failed while generating MAJ5/MAJ3 sum\n";
        return {};
    }

    (void)zero_m1;
    return inst_list;
}

std::vector<uint32_t> Mult_4_bit_csa(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0_dst
) {
    return Mult_4_bit_m0_variant(srcA, srcB, prod_m0_dst, true, false);
}

std::vector<uint32_t> Mult_4_bit_csa_via_Maj3(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0_dst
) {
    return Mult_4_bit_m0_variant(srcA, srcB, prod_m0_dst, true, true);
}

std::vector<uint32_t> Mult_4_bit_optimized_m0_1(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0_dst
) {
    return Mult_4_bit_m0_variant(srcA, srcB, prod_m0_dst, false, false);
}

std::vector<uint32_t> Mult_n_bit(
    std::vector<uint64_t> srcA,
    std::vector<uint64_t> srcB,
    std::vector<uint64_t> prod_m0_dst
) {
    std::vector<uint32_t> inst_list;

    const size_t n = srcA.size();
    if (n == 0 || srcB.size() != n || prod_m0_dst.size() != (2 * n)) {
        std::cout << "[ERROR]: Mult_n_bit expects A/B=n bits and product=2n bits\n";
        return {};
    }

    uint32_t bank = extract_bank_addr(srcA[0]);
    for (size_t i = 0; i < n; ++i) {
        if (extract_bank_addr(srcA[i]) != bank ||
            extract_bank_addr(srcB[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }
    for (size_t i = 0; i < (2 * n); ++i) {
        if (extract_bank_addr(prod_m0_dst[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair\n";
            return {};
        }
    }

    uint32_t src_mat0 = get_mat_id_from_row(extract_row_addr(srcA[0]));
    uint32_t src_mat1 = src_mat0 + 1;

    for (size_t i = 0; i < n; ++i) {
        if (get_mat_id_from_row(extract_row_addr(srcA[i])) != src_mat0 ||
            get_mat_id_from_row(extract_row_addr(srcB[i])) != src_mat0) {
            std::cout << "[ERROR]: Mult_n_bit expects srcA/srcB in same MAT\n";
            return {};
        }
    }
    for (size_t i = 0; i < (2 * n); ++i) {
        if (get_mat_id_from_row(extract_row_addr(prod_m0_dst[i])) != src_mat0) {
            std::cout << "[ERROR]: Mult_n_bit expects outputs in source MAT\n";
            return {};
        }
    }

    const uint64_t zero_m0 = make_addr(bank, make_global_row(src_mat0, temp_row_0));
    const uint64_t zero_m1 = make_addr(bank, make_global_row(src_mat1, temp_row_0));

    std::set<uint32_t> used_rows;
    for (auto x : srcA) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : srcB) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : prod_m0_dst) reserve_addr(used_rows, x);
    reserve_row(used_rows, make_global_row(src_mat0, temp_row_0));
    reserve_row(used_rows, make_global_row(src_mat0, temp_row_1));
    reserve_row(used_rows, make_global_row(src_mat1, temp_row_0));
    reserve_row(used_rows, make_global_row(src_mat1, temp_row_1));

    // n reusable 2RA groups for partial-product generation in MAT0 + MAT1.
    std::vector<uint64_t> pp_lhs_m0(n), pp_rhs_m0(n);
    std::vector<uint64_t> pp_lhs_m1(n), pp_rhs_m1(n);
    for (size_t i = 0; i < n; ++i) {
        auto g0_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
        auto g1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);
        if (!g0_opt || !g1_opt) {
            std::cout << "[ERROR]: No free 2RA scratch groups for Mult_n_bit partial products\n";
            return {};
        }
        pp_lhs_m0[i] = (*g0_opt)[0];
        pp_rhs_m0[i] = (*g0_opt)[1];
        pp_lhs_m1[i] = (*g1_opt)[0];
        pp_rhs_m1[i] = (*g1_opt)[1];
    }

    // Mirrored scratch rows used by iterative accumulation.
    // acc_a(2n) + acc_b(2n) + term(2n) + carry ping/pong(2).
    const size_t mirrored_needed = (6 * n) + 2;
    auto local_scratch_opt = allocate_mirrored_local_rows(src_mat0, src_mat1, mirrored_needed, used_rows);
    if (!local_scratch_opt) {
        std::cout << "[ERROR]: No free mirrored scratch rows for Mult_n_bit\n";
        return {};
    }

    const auto& local_scratch = *local_scratch_opt;
    size_t scratch_idx = 0;
    auto next_row_m0 = [&]() -> uint64_t {
        uint32_t local_row = local_scratch[scratch_idx++];
        return make_addr(bank, make_global_row(src_mat0, local_row));
    };

    std::vector<uint64_t> acc_a_m0(2 * n);
    std::vector<uint64_t> acc_b_m0(2 * n);
    std::vector<uint64_t> term_m0(2 * n);
    for (size_t k = 0; k < (2 * n); ++k) acc_a_m0[k] = next_row_m0();
    for (size_t k = 0; k < (2 * n); ++k) acc_b_m0[k] = next_row_m0();
    for (size_t k = 0; k < (2 * n); ++k) term_m0[k] = next_row_m0();
    uint64_t carry0_m0 = next_row_m0();
    uint64_t carry1_m0 = next_row_m0();

    if (scratch_idx != mirrored_needed) {
        std::cout << "[ERROR]: Internal scratch allocation mismatch in Mult_n_bit\n";
        return {};
    }

    auto to_m1 = [&](uint64_t addr_m0) -> uint64_t {
        return make_addr(bank, extract_row_addr(addr_m0) + ROWS_PER_MAT);
    };

    std::vector<uint64_t> acc_a_m1(2 * n), acc_b_m1(2 * n), term_m1(2 * n);
    for (size_t k = 0; k < (2 * n); ++k) {
        acc_a_m1[k] = to_m1(acc_a_m0[k]);
        acc_b_m1[k] = to_m1(acc_b_m0[k]);
        term_m1[k] = to_m1(term_m0[k]);
    }
    uint64_t carry0_m1 = to_m1(carry0_m0);
    uint64_t carry1_m1 = to_m1(carry1_m0);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    // Prevent ADD_new scratch allocation from clobbering Mult_n_bit state rows.
    ScopedAddNewReservedRows multn_reserved_scope(&used_rows);

    // acc_a starts at zero in both MAT0 and MAT1.
    for (size_t k = 0; k < (2 * n); ++k) {
        append(single_Row_Copy(zero_m0, acc_a_m0[k]));
        append(single_Row_Copy(zero_m1, acc_a_m1[k]));
    }

    for (size_t j = 0; j < n; ++j) {
        // Build all n partial products for multiplier bit j in MAT0 + MAT1.
        for (size_t i = 0; i < n; ++i) {
            append(single_Row_Copy(srcA[i], pp_lhs_m0[i]));
            append(single_Row_Copy(srcB[j], pp_rhs_m0[i]));
            append(AND(pp_lhs_m0[i], pp_rhs_m0[i]));

            append(single_Row_Copy(srcA[i] + ROWS_PER_MAT, pp_lhs_m1[i]));
            append(single_Row_Copy(srcB[j] + ROWS_PER_MAT, pp_rhs_m1[i]));
            append(AND(pp_lhs_m1[i], pp_rhs_m1[i]));
        }

        // term[] = 0 in both MAT0 and MAT1.
        for (size_t k = 0; k < (2 * n); ++k) {
            append(single_Row_Copy(zero_m0, term_m0[k]));
            append(single_Row_Copy(zero_m1, term_m1[k]));
        }

        // Scatter shifted partial products into term[] in both MATs.
        for (size_t i = 0; i < n; ++i) {
            const size_t k = i + j;
            append(single_Row_Copy(pp_lhs_m0[i], term_m0[k]));
            append(single_Row_Copy(pp_lhs_m1[i], term_m1[k]));
        }

        // Ripple add: acc_a + term -> acc_b (sum/carry both MAT0+MAT1 via ADD_new).
        uint64_t carry_in_m0 = zero_m0;
        bool use_carry0 = true;

        for (size_t k = 0; k < (2 * n); ++k) {
            uint64_t carry_out_m0 = use_carry0 ? carry0_m0 : carry1_m0;
            uint64_t carry_out_m1 = use_carry0 ? carry0_m1 : carry1_m1;

            auto add_stage = ADD_new(
                acc_a_m0[k],
                term_m0[k],
                carry_in_m0,
                acc_b_m0[k],
                acc_b_m1[k],
                carry_out_m0,
                carry_out_m1
            );
            if (add_stage.empty()) {
                std::cout << "[ERROR]: Mult_n_bit failed while invoking ADD_new stage\n";
                return {};
            }
            append(add_stage);

            carry_in_m0 = carry_out_m0;
            use_carry0 = !use_carry0;
        }

        std::swap(acc_a_m0, acc_b_m0);
        std::swap(acc_a_m1, acc_b_m1);
    }

    // Write final accumulator bits to user destination rows in MAT0.
    for (size_t k = 0; k < (2 * n); ++k) {
        append(single_Row_Copy(acc_a_m0[k], prod_m0_dst[k]));
    }

    return inst_list;
}

std::vector<uint32_t> ADD_4_bit(std::vector<uint64_t> src1, std::vector<uint64_t> src2, uint64_t src3, std::vector<uint64_t> sum, uint64_t c_out) {
    std::vector<uint32_t> inst_list;

    if (src1.size() != 4 || src2.size() != 4 || sum.size() != 4) {
        std::cout << "[ERROR]: ADD_4_bit expects 4-bit vectors" << std::endl;
        return {};
    }
    uint32_t bank = extract_bank_addr(src1[0]);

    for (int i = 0; i < 4; i++) {
        if (extract_bank_addr(src1[i]) != bank ||
            extract_bank_addr(src2[i]) != bank ||
            extract_bank_addr(sum[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
            return {};
        }
    }
    if (extract_bank_addr(src3) != bank || extract_bank_addr(c_out) != bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return {};
    }

    uint32_t src_mat0 = get_mat_id_from_row(extract_row_addr(src1[0]));
    uint32_t src_mat1 = src_mat0 + 1;

    for (int i = 0; i < 4; i++) {
        if (get_mat_id_from_row(extract_row_addr(src1[i])) != src_mat0 ||
            get_mat_id_from_row(extract_row_addr(src2[i])) != src_mat0) {
            std::cout << "[ERROR]: ADD_4_bit expects src vectors in same MAT" << std::endl;
            return {};
        }
    }
    if (get_mat_id_from_row(extract_row_addr(src3)) != src_mat0) {
        std::cout << "[ERROR]: ADD_4_bit expects src3 in same MAT as src vectors" << std::endl;
        return {};
    }

    std::set<uint32_t> used_rows;
    for (auto x : src1) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : src2) reserve_addr_with_next_mat(used_rows, x);
    reserve_addr_with_next_mat(used_rows, src3);
    for (auto x : sum) reserve_addr(used_rows, x);
    reserve_addr(used_rows, c_out);
    if (g_add_new_extra_reserved_rows != nullptr) {
        used_rows.insert(g_add_new_extra_reserved_rows->begin(),
                         g_add_new_extra_reserved_rows->end());
    }

    auto cout_group_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
    auto sum_group_opt = allocate_group_rows(bank, src_mat1, three_ra_group_indices(), used_rows);
    auto cout_group_mat1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);

    if (!cout_group_opt || !sum_group_opt || !cout_group_mat1_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for ADD_4_bit" << std::endl;
        return {};
    }

    std::vector<uint64_t> cout_group = *cout_group_opt;              // MAT0 carry path
    std::vector<uint64_t> sum_group = *sum_group_opt;                // MAT1 sum path (first 5 used)
    std::vector<uint64_t> cout_group_MAT1 = *cout_group_mat1_opt;    // MAT1 carry path
    ScopedAddNewReservedRows add4_internal_scope(&used_rows);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    const uint32_t c_out_mat = get_mat_id_from_row(extract_row_addr(c_out));
    const bool use_mat0_final_cout = (c_out_mat == src_mat0);

    auto add_stage = [&](int bit_index, bool final_stage) {
        const uint64_t a0 = src1[bit_index];
        const uint64_t b0 = src2[bit_index];
        const uint64_t a1 = a0 + ROWS_PER_MAT;
        const uint64_t b1 = b0 + ROWS_PER_MAT;
        const bool skip_mat1_final_carry = final_stage && use_mat0_final_cout;

        // MAT0 carry path: [A, B, Cin]
        append(single_Row_Copy(a0, cout_group[0]));
        append(single_Row_Copy(b0, cout_group[1]));

        // MAT1 fanout for sum and (optionally) carry computation.
        if (skip_mat1_final_carry) {
            append(single_Row_Copy(a1, sum_group[0]));
            append(single_Row_Copy(b1, sum_group[1]));
        } else {
            append(Multi_Row_Copy(a1, sum_group[0], cout_group_MAT1[0]));
            append(Multi_Row_Copy(b1, sum_group[1], cout_group_MAT1[1]));
        }

        if (bit_index == 0) {
            // Seed initial carry-in.
            append(single_Row_Copy(src3, cout_group[2]));
            if (skip_mat1_final_carry) {
                append(single_Row_Copy(src3 + ROWS_PER_MAT, sum_group[2]));
            } else {
                append(Multi_Row_Copy(src3 + ROWS_PER_MAT, sum_group[2], cout_group_MAT1[2]));
            }
        }
        // For later bits:
        // - cout_group[2] already holds prior carry in MAT0.
        // - sum_group[2] and cout_group_MAT1[2] hold prior carry in MAT1.

        append(Maj3(cout_group[0], cout_group[1], cout_group[2]));

        // Produce two copies of ~carry directly in MAT1.
        append(Multi_Row_Copy(cout_group[0], sum_group[3], sum_group[4]));

        append(Maj5(sum_group[0], sum_group[1], sum_group[2], sum_group[3], sum_group[4]));
        append(single_Row_Copy(sum_group[0], sum[bit_index]));

        if (skip_mat1_final_carry) {
            append(single_Row_Copy(cout_group[0], c_out));
            return;
        }

        append(Maj3(cout_group_MAT1[0], cout_group_MAT1[1], cout_group_MAT1[2]));

        if (!final_stage) {
            append(single_Row_Copy(cout_group_MAT1[0], sum_group[2]));
        } else {
            append(single_Row_Copy(cout_group_MAT1[0], c_out));
        }
    };

    //instead of add_stage(0,false);...add_stage(3,true);
    //using loop 
    for (int i = 0; i < int(src1.size()); ++i) {
        add_stage(i, i == int(src1.size()) - 1);
    }

    return inst_list;
}

std::vector<uint32_t> ADD_4_bit_or_sum(std::vector<uint64_t> src1, std::vector<uint64_t> src2, uint64_t src3, std::vector<uint64_t> sum, uint64_t c_out) {
    std::vector<uint32_t> inst_list;

    if (src1.size() != 4 || src2.size() != 4 || sum.size() != 4) {
        std::cout << "[ERROR]: ADD_4_bit_or_sum expects 4-bit vectors" << std::endl;
        return {};
    }
    uint32_t bank = extract_bank_addr(src1[0]);

    for (int i = 0; i < 4; i++) {
        if (extract_bank_addr(src1[i]) != bank ||
            extract_bank_addr(src2[i]) != bank ||
            extract_bank_addr(sum[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
            return {};
        }
    }
    if (extract_bank_addr(src3) != bank || extract_bank_addr(c_out) != bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return {};
    }

    uint32_t src_mat0 = get_mat_id_from_row(extract_row_addr(src1[0]));
    uint32_t src_mat1 = src_mat0 + 1;
    const uint64_t one_m1 = make_addr(bank, make_global_row(src_mat1, temp_row_1));

    for (int i = 0; i < 4; i++) {
        if (get_mat_id_from_row(extract_row_addr(src1[i])) != src_mat0 ||
            get_mat_id_from_row(extract_row_addr(src2[i])) != src_mat0) {
            std::cout << "[ERROR]: ADD_4_bit_or_sum expects src vectors in same MAT" << std::endl;
            return {};
        }
    }
    if (get_mat_id_from_row(extract_row_addr(src3)) != src_mat0) {
        std::cout << "[ERROR]: ADD_4_bit_or_sum expects src3 in same MAT as src vectors" << std::endl;
        return {};
    }

    std::set<uint32_t> used_rows;
    for (auto x : src1) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : src2) reserve_addr_with_next_mat(used_rows, x);
    reserve_addr_with_next_mat(used_rows, src3);
    for (auto x : sum) reserve_addr(used_rows, x);
    reserve_addr(used_rows, c_out);

    auto cout_group_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
    auto sum_group_opt = allocate_group_rows(bank, src_mat1, three_ra_group_indices(), used_rows);
    auto cout_group_mat1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);

    if (!cout_group_opt || !sum_group_opt || !cout_group_mat1_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for ADD_4_bit_or_sum" << std::endl;
        return {};
    }

    std::vector<uint64_t> cout_group = *cout_group_opt;              // MAT0 carry path
    std::vector<uint64_t> sum_group = *sum_group_opt;                // MAT1 sum path (first 5 used)
    std::vector<uint64_t> cout_group_MAT1 = *cout_group_mat1_opt;    // MAT1 carry path
    ScopedAddNewReservedRows add4_internal_scope(&used_rows);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    const uint32_t c_out_mat = get_mat_id_from_row(extract_row_addr(c_out));
    const bool use_mat0_final_cout = (c_out_mat == src_mat0);

    auto add_stage = [&](int bit_index, bool final_stage) {
        const uint64_t a0 = src1[bit_index];
        const uint64_t b0 = src2[bit_index];
        const uint64_t a1 = a0 + ROWS_PER_MAT;
        const uint64_t b1 = b0 + ROWS_PER_MAT;
        const bool skip_mat1_final_carry = final_stage && use_mat0_final_cout;

        // MAT0 carry path: [A, B, Cin]
        append(single_Row_Copy(a0, cout_group[0]));
        append(single_Row_Copy(b0, cout_group[1]));

        // MAT1 fanout for sum and (optionally) carry computation.
        if (skip_mat1_final_carry) {
            append(single_Row_Copy(a1, sum_group[0]));
            append(single_Row_Copy(b1, sum_group[1]));
        } else {
            append(Multi_Row_Copy(a1, sum_group[0], cout_group_MAT1[0]));
            append(Multi_Row_Copy(b1, sum_group[1], cout_group_MAT1[1]));
        }

        if (bit_index == 0) {
            // Seed initial carry-in.
            append(single_Row_Copy(src3, cout_group[2]));
            if (skip_mat1_final_carry) {
                append(single_Row_Copy(src3 + ROWS_PER_MAT, sum_group[2]));
            } else {
                append(Multi_Row_Copy(src3 + ROWS_PER_MAT, sum_group[2], cout_group_MAT1[2]));
            }
        }
        // For later bits:
        // - cout_group[2] already holds prior carry in MAT0.
        // - sum_group[2] and cout_group_MAT1[2] hold prior carry in MAT1.

        // COUT = MAJ3(A, B, Cin) in MAT0 path.
        append(Maj3(cout_group[0], cout_group[1], cout_group[2]));

        // Approximate SUM = A | B | Cin.
        // Implement OR3 as MAJ5(A, B, Cin, 1, 1) in MAT1 sum group.
        append(Multi_Row_Copy(one_m1, sum_group[3], sum_group[4]));
        append(Maj5(sum_group[0], sum_group[1], sum_group[2], sum_group[3], sum_group[4]));
        append(single_Row_Copy(sum_group[0], sum[bit_index]));

        if (skip_mat1_final_carry) {
            append(single_Row_Copy(cout_group[0], c_out));
            return;
        }

        append(Maj3(cout_group_MAT1[0], cout_group_MAT1[1], cout_group_MAT1[2]));

        if (!final_stage) {
            append(single_Row_Copy(cout_group_MAT1[0], sum_group[2]));
        } else {
            append(single_Row_Copy(cout_group_MAT1[0], c_out));
        }
    };

    for (int i = 0; i < int(src1.size()); ++i) {
        add_stage(i, i == int(src1.size()) - 1);
    }

    return inst_list;
}

std::vector<uint32_t> ADD_n_bit(std::vector<uint64_t> src1, std::vector<uint64_t> src2, uint64_t src3, std::vector<uint64_t> sum, uint64_t c_out) {
    std::vector<uint32_t> inst_list;

    const size_t n = src1.size();
    if (n == 0 || src2.size() != n || sum.size() != n) {
        std::cout << "[ERROR]: ADD_n_bit expects non-empty equal-size vectors" << std::endl;
        return {};
    }

    uint32_t bank = extract_bank_addr(src1[0]);

    for (size_t i = 0; i < n; i++) {
        if (extract_bank_addr(src1[i]) != bank ||
            extract_bank_addr(src2[i]) != bank ||
            extract_bank_addr(sum[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
            return {};
        }
    }
    if (extract_bank_addr(src3) != bank || extract_bank_addr(c_out) != bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return {};
    }

    uint32_t src_mat0 = get_mat_id_from_row(extract_row_addr(src1[0]));
    uint32_t src_mat1 = src_mat0 + 1;

    for (size_t i = 0; i < n; i++) {
        if (get_mat_id_from_row(extract_row_addr(src1[i])) != src_mat0 ||
            get_mat_id_from_row(extract_row_addr(src2[i])) != src_mat0) {
            std::cout << "[ERROR]: ADD_n_bit expects src vectors in same MAT" << std::endl;
            return {};
        }
    }
    if (get_mat_id_from_row(extract_row_addr(src3)) != src_mat0) {
        std::cout << "[ERROR]: ADD_n_bit expects src3 in same MAT as src vectors" << std::endl;
        return {};
    }

    std::set<uint32_t> used_rows;
    for (auto x : src1) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : src2) reserve_addr_with_next_mat(used_rows, x);
    reserve_addr_with_next_mat(used_rows, src3);
    for (auto x : sum) reserve_addr(used_rows, x);
    reserve_addr(used_rows, c_out);

    auto cout_group_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
    auto sum_group_opt = allocate_group_rows(bank, src_mat1, three_ra_group_indices(), used_rows);
    auto cout_group_mat1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);

    if (!cout_group_opt || !sum_group_opt || !cout_group_mat1_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for ADD_n_bit" << std::endl;
        return {};
    }

    std::vector<uint64_t> cout_group = *cout_group_opt;              // MAT0 carry path
    std::vector<uint64_t> sum_group = *sum_group_opt;                // MAT1 sum path (first 5 used)
    std::vector<uint64_t> cout_group_MAT1 = *cout_group_mat1_opt;    // MAT1 carry path
    ScopedAddNewReservedRows addn_internal_scope(&used_rows);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    const uint32_t c_out_mat = get_mat_id_from_row(extract_row_addr(c_out));
    const bool use_mat0_final_cout = (c_out_mat == src_mat0);

    auto add_stage = [&](size_t bit_index, bool final_stage) {
        const uint64_t a0 = src1[bit_index];
        const uint64_t b0 = src2[bit_index];
        const uint64_t a1 = a0 + ROWS_PER_MAT;
        const uint64_t b1 = b0 + ROWS_PER_MAT;
        const bool skip_mat1_final_carry = final_stage && use_mat0_final_cout;

        // MAT0 carry path: [A, B, Cin]
        append(single_Row_Copy(a0, cout_group[0]));
        append(single_Row_Copy(b0, cout_group[1]));

        // MAT1 fanout for sum and (optionally) carry computation.
        if (skip_mat1_final_carry) {
            append(single_Row_Copy(a1, sum_group[0]));
            append(single_Row_Copy(b1, sum_group[1]));
        } else {
            append(Multi_Row_Copy(a1, sum_group[0], cout_group_MAT1[0]));
            append(Multi_Row_Copy(b1, sum_group[1], cout_group_MAT1[1]));
        }

        if (bit_index == 0) {
            // Seed initial carry-in.
            append(single_Row_Copy(src3, cout_group[2]));
            if (skip_mat1_final_carry) {
                append(single_Row_Copy(src3 + ROWS_PER_MAT, sum_group[2]));
            } else {
                append(Multi_Row_Copy(src3 + ROWS_PER_MAT, sum_group[2], cout_group_MAT1[2]));
            }
        }
        // For later bits:
        // - cout_group[2] already holds prior carry in MAT0.
        // - sum_group[2] and cout_group_MAT1[2] hold prior carry in MAT1.

        append(Maj3(cout_group[0], cout_group[1], cout_group[2]));

        // Produce two copies of ~carry directly in MAT1.
        append(Multi_Row_Copy(cout_group[0], sum_group[3], sum_group[4]));

        append(Maj5(sum_group[0], sum_group[1], sum_group[2], sum_group[3], sum_group[4]));
        append(single_Row_Copy(sum_group[0], sum[bit_index]));

        if (skip_mat1_final_carry) {
            append(single_Row_Copy(cout_group[0], c_out));
            return;
        }

        append(Maj3(cout_group_MAT1[0], cout_group_MAT1[1], cout_group_MAT1[2]));

        if (!final_stage) {
            append(single_Row_Copy(cout_group_MAT1[0], sum_group[2]));
        } else {
            append(single_Row_Copy(cout_group_MAT1[0], c_out));
        }
    };

    for (size_t i = 0; i < n; ++i) {
        add_stage(i, i == n - 1);
    }

    return inst_list;
}

std::vector<uint32_t> ADD_n_bit_via_Maj3(std::vector<uint64_t> src1, std::vector<uint64_t> src2, uint64_t src3, std::vector<uint64_t> sum, uint64_t c_out) {
    std::vector<uint32_t> inst_list;

    const size_t n = src1.size();
    if (n == 0 || src2.size() != n || sum.size() != n) {
        std::cout << "[ERROR]: ADD_n_bit_via_Maj3 expects non-empty equal-size vectors" << std::endl;
        return {};
    }

    uint32_t bank = extract_bank_addr(src1[0]);

    for (size_t i = 0; i < n; i++) {
        if (extract_bank_addr(src1[i]) != bank ||
            extract_bank_addr(src2[i]) != bank ||
            extract_bank_addr(sum[i]) != bank) {
            std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
            return {};
        }
    }
    if (extract_bank_addr(src3) != bank || extract_bank_addr(c_out) != bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return {};
    }

    uint32_t src_mat0 = get_mat_id_from_row(extract_row_addr(src1[0]));
    uint32_t src_mat1 = src_mat0 + 1;

    for (size_t i = 0; i < n; i++) {
        if (get_mat_id_from_row(extract_row_addr(src1[i])) != src_mat0 ||
            get_mat_id_from_row(extract_row_addr(src2[i])) != src_mat0) {
            std::cout << "[ERROR]: ADD_n_bit_via_Maj3 expects src vectors in same MAT" << std::endl;
            return {};
        }
    }
    if (get_mat_id_from_row(extract_row_addr(src3)) != src_mat0) {
        std::cout << "[ERROR]: ADD_n_bit_via_Maj3 expects src3 in same MAT as src vectors" << std::endl;
        return {};
    }

    std::set<uint32_t> used_rows;
    for (auto x : src1) reserve_addr_with_next_mat(used_rows, x);
    for (auto x : src2) reserve_addr_with_next_mat(used_rows, x);
    reserve_addr_with_next_mat(used_rows, src3);
    for (auto x : sum) reserve_addr(used_rows, x);
    reserve_addr(used_rows, c_out);

    auto cout_group_opt = allocate_group_rows(bank, src_mat0, two_ra_group_indices(), used_rows);
    auto sum_group_opt = allocate_group_rows(bank, src_mat1, three_ra_group_indices(), used_rows);
    auto cout_group_mat1_opt = allocate_group_rows(bank, src_mat1, two_ra_group_indices(), used_rows);

    if (!cout_group_opt || !sum_group_opt || !cout_group_mat1_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for ADD_n_bit_via_Maj3" << std::endl;
        return {};
    }

    std::vector<uint64_t> cout_group = *cout_group_opt;              // MAT0 carry path
    std::vector<uint64_t> sum_group = *sum_group_opt;                // MAT1 sum path (first 5 used)
    std::vector<uint64_t> cout_group_MAT1 = *cout_group_mat1_opt;    // MAT1 carry path
    ScopedAddNewReservedRows addn_internal_scope(&used_rows);

    auto append = [&](const std::vector<uint32_t>& v) {
        inst_list.insert(inst_list.end(), v.begin(), v.end());
    };

    const uint32_t c_out_mat = get_mat_id_from_row(extract_row_addr(c_out));
    const bool use_mat0_final_cout = (c_out_mat == src_mat0);

    auto add_stage = [&](size_t bit_index, bool final_stage) {
        const uint64_t a0 = src1[bit_index];
        const uint64_t b0 = src2[bit_index];
        const uint64_t a1 = a0 + ROWS_PER_MAT;
        const uint64_t b1 = b0 + ROWS_PER_MAT;
        const bool skip_mat1_final_carry = final_stage && use_mat0_final_cout;

        // MAT0 carry path: [A, B, Cin]
        append(single_Row_Copy(a0, cout_group[0]));
        append(single_Row_Copy(b0, cout_group[1]));

        // MAT1 fanout for sum and (optionally) carry computation.
        if (skip_mat1_final_carry) {
            append(single_Row_Copy(a1, sum_group[0]));
            append(single_Row_Copy(b1, sum_group[1]));
        } else {
            append(Multi_Row_Copy(a1, sum_group[0], cout_group_MAT1[0]));
            append(Multi_Row_Copy(b1, sum_group[1], cout_group_MAT1[1]));
        }

        if (bit_index == 0) {
            // Seed initial carry-in.
            append(single_Row_Copy(src3, cout_group[2]));
            if (skip_mat1_final_carry) {
                append(single_Row_Copy(src3 + ROWS_PER_MAT, sum_group[2]));
            } else {
                append(Multi_Row_Copy(src3 + ROWS_PER_MAT, sum_group[2], cout_group_MAT1[2]));
            }
        }
        // For later bits:
        // - cout_group[2] already holds prior carry in MAT0.
        // - sum_group[2] and cout_group_MAT1[2] hold prior carry in MAT1.

        append(Maj3(cout_group[0], cout_group[1], cout_group[2]));

        // Produce two copies of ~carry directly in MAT1.
        append(Multi_Row_Copy(cout_group[0], sum_group[3], sum_group[4]));

        append(Maj5_via_Maj3(sum_group[0], sum_group[1], sum_group[2], sum_group[3], sum_group[4], sum_group[0]));
        append(single_Row_Copy(sum_group[0], sum[bit_index]));

        if (skip_mat1_final_carry) {
            append(single_Row_Copy(cout_group[0], c_out));
            return;
        }

        append(Maj3(cout_group_MAT1[0], cout_group_MAT1[1], cout_group_MAT1[2]));

        if (!final_stage) {
            append(single_Row_Copy(cout_group_MAT1[0], sum_group[2]));
        } else {
            append(single_Row_Copy(cout_group_MAT1[0], c_out));
        }
    };

    for (size_t i = 0; i < n; ++i) {
        add_stage(i, i == n - 1);
    }

    return inst_list;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//This add doesnt store the results in separate rows
std::vector<uint32_t> ADD(uint64_t src1, uint64_t src2, uint64_t src3) {
    std::vector<uint32_t> inst_list;

    uint32_t src1_bank = extract_bank_addr(src1);
    uint32_t src2_bank = extract_bank_addr(src2);
    uint32_t src3_bank = extract_bank_addr(src3);

    if (src1_bank != src2_bank || src2_bank != src3_bank) {
        std::cout << "[ERROR]: Invalid Addr Pair" << std::endl;
        return inst_list;
    }

    uint32_t src1_row = extract_row_addr(src1);
    uint32_t src2_row = extract_row_addr(src2);
    uint32_t src3_row = extract_row_addr(src3);

    uint32_t src_mat0 = get_mat_id_from_row(src1_row);
    uint32_t src_mat1 = src_mat0 + 1;

    if (get_mat_id_from_row(src2_row) != src_mat0 ||
        get_mat_id_from_row(src3_row) != src_mat0) {
        std::cout << "[ERROR]: ADD expects all sources in same MAT" << std::endl;
        return inst_list;
    }

    std::set<uint32_t> used_rows;
    reserve_addr_with_next_mat(used_rows, src1);
    reserve_addr_with_next_mat(used_rows, src2);
    reserve_addr_with_next_mat(used_rows, src3);

    auto cout_group_opt = allocate_group_rows(src1_bank, src_mat0, two_ra_group_indices(), used_rows);
    auto sum_group_opt = allocate_group_rows(src1_bank, src_mat1, three_ra_group_indices(), used_rows);

    if (!cout_group_opt || !sum_group_opt) {
        std::cout << "[ERROR]: No free scratch RA groups for ADD" << std::endl;
        return inst_list;
    }

    std::vector<uint64_t> cout_group = *cout_group_opt;
    std::vector<uint64_t> sum_group = *sum_group_opt;
    
    
    //RowCopy A to compute row of sum and cout
    auto temp_const = single_Row_Copy(src1 + ROWS_PER_MAT, sum_group[0]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());
    temp_const = single_Row_Copy(src1, cout_group[0]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());
    
    //RowCopy B to compute row of sum and cout
    temp_const = single_Row_Copy(src2 + ROWS_PER_MAT, sum_group[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());
    temp_const = single_Row_Copy(src2, cout_group[1]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    //RowCopy Cin to compute row of sum and cout
    temp_const = single_Row_Copy(src3 + ROWS_PER_MAT, sum_group[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());
    temp_const = single_Row_Copy(src3, cout_group[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    //MAJ3 in Cout group to get COUT
    temp_const = Maj3(cout_group[0], cout_group[1], cout_group[2]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    //RowCopy ~COUT into compute row of SUM
    temp_const = NOT(cout_group[0], sum_group[3]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    //RowCopy again for ~COUT
    temp_const = single_Row_Copy(sum_group[3], sum_group[4]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    //MAJ5 to get SUM
    temp_const = Maj5(sum_group[0], sum_group[1], sum_group[2], sum_group[3], sum_group[4]);
    inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    // MAJ5 emulated using only MAJ3
    // temp_const = Maj5_via_Maj3(sum_group[0], sum_group[1], sum_group[2],sum_group[3], sum_group[4], sum_group[0]);
    // inst_list.insert(inst_list.end(), temp_const.begin(), temp_const.end());

    return inst_list;
}