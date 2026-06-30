#include "../../include/cud/types.h"
#include <iostream>

uint32_t encode_uop(AddrMode mode,
                    uint32_t bank,
                    uint32_t row,
                    uint32_t last,
                    uint32_t maj3_frac,
                    uint32_t maj5_frac,
                    uint32_t dont_care) {
    uint32_t addr = 0;

    switch (mode) {
        case AddrMode::END:
            addr = 0;
            break;

        case AddrMode::ROW_COPY:
            addr |= (row & 0x1FFFF);
            addr |= (bank & 0xF) << 17;
            addr |= (0x1) << 29;
            break;

        case AddrMode::ROW_COPY_1:
            addr |= (row & 0x1FFFF);
            addr |= (bank & 0xF) << 17;
            addr |= (last & 0x1) << 21;
            addr |= (0x2) << 29;
            break;

        case AddrMode::MAJ3:
            addr |= (row & 0x1FFFF);
            addr |= (bank & 0xF) << 17;
            addr |= (dont_care & 0x7) << 21;
            addr |= (maj3_frac & 0x3) << 24;
            addr |= (0x3) << 29;
            break;

        case AddrMode::MAJ5_1:
            addr |= (row & 0x1FFFF);
            addr |= (bank & 0xF) << 17;
            addr |= (dont_care & 0x7) << 21;
            addr |= (0x4) << 29;
            break;

        case AddrMode::MAJ5_2:
            addr |= (maj3_frac & 0x7) << 0;
            addr |= (maj5_frac & 0x7) << 3;
            addr |= (dont_care & 0x7) << 6;
            addr |= (0x5) << 29;
            break;

        case AddrMode::MULTI_BANK_ENTRY:
            addr |= (last & 0x3) << 0;
            addr |= (0x6) << 29;
            break;

        case AddrMode::MULTI_BANK_EXIT:
            addr |= (0x7) << 29;
            break;
    }

    return addr;
}

uint32_t dont_care_pos_maj3(uint32_t ra_group_index) {
    switch (ra_group_index) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 4;
        case 5: return 5;
        case 6: return 6;
        default:
            std::cout << "[ERROR]: Invalid MAJ3 group index for dont care mapping" << std::endl;
            return 0;
    }
}

uint32_t dont_care_pos_maj5(uint32_t ra_group_index) {
    switch (ra_group_index) {
        case 7:  return 0;
        case 8:  return 1;
        case 9:  return 2;
        case 10: return 3;
        case 11: return 4;
        case 12: return 5;
        case 13: return 6;
        case 14: return 7;
        default:
            std::cout << "[ERROR]: Invalid MAJ5 group index for dont care mapping" << std::endl;
            return 0;
    }
}

uint32_t extract_row(uint64_t addr) {
    return static_cast<uint32_t>(addr & 0x1FFFF);
}

uint32_t extract_bank(uint64_t addr) {
    return static_cast<uint32_t>((addr >> 17) & 0xF);
}

bool row_in_list(uint32_t row, const std::vector<uint64_t>& rows) {
    for (auto r : rows) {
        if (extract_row(r) == row) return true;
    }
    return false;
}

uint32_t mat_id_from_row(uint32_t global_row) {
    return global_row / ROWS_PER_MAT;
}

uint32_t local_row_from_global(uint32_t global_row) {
    return global_row % ROWS_PER_MAT;
}

uint32_t global_row_from_mat(uint32_t mat_id, uint32_t local_row) {
    return mat_id * ROWS_PER_MAT + local_row;
}

uint64_t make_row_addr(uint32_t bank, uint32_t row) {
    return (static_cast<uint64_t>(bank & 0xF) << 17) |
           static_cast<uint64_t>(row & 0x1FFFF);
}
