#include "include/instruction.h"
#include <cstdint>
#include <iostream>
#include <vector>
#include <algorithm>

uint32_t build_address(AddrMode mode,
                       uint32_t bank,
                       uint32_t row,
                       uint32_t last,
                       uint32_t maj3_frac,
                       uint32_t maj5_frac,
                       uint32_t dont_care) {
    uint32_t addr = 0;

    switch (mode) {
        case AddrMode::END:
            addr = 0; // all 32 bits zero
            break;

        case AddrMode::ROW_COPY:
            // 0-16: row, 17-20: bank, 21: last row copy, 29-31: topBits
            addr |= (row & 0x1FFFF);
            addr |= (bank & 0xF) << 17;
            addr |= (0x1) << 29;
            break;

        case AddrMode::ROW_COPY_1:
            // 0-16: row, 17-20: bank, 21: last row copy, 29-31: topBits
            addr |= (row & 0x1FFFF);
            addr |= (bank & 0xF) << 17;
            addr |= (last & 0x1) << 21;
            addr |= (0x2) << 29;
            break;

        case AddrMode::MAJ3:
            // 0-16: row, 17-20: bank, 21-23: dont_care, 24-25: maj3_frac, 29-31: topBits
            addr |= (row & 0x1FFFF);
            addr |= (bank & 0xF) << 17;
            addr |= (dont_care & 0x7) << 21;   // bits 21-23
            addr |= (maj3_frac & 0x3) << 24;   // bits 24-25
            addr |= (0x3) << 29;
            break;

        case AddrMode::MAJ5_1:
            // 0-16: row, 17-20: bank, 21-23: dont_care, 29-31: topBits
            addr |= (row & 0x1FFFF);
            addr |= (bank & 0xF) << 17;
            addr |= (dont_care & 0x7) << 21;   // bits 21-23
            addr |= (0x4) << 29;
            break;

        case AddrMode::MAJ5_2:
            // 0-8: maj5_frac, 21-23: dont_care, 29-31: topBits
            addr |= (maj3_frac & 0x7) << 0;   // frac0
            addr |= (maj5_frac & 0x7) << 3;   // frac1
            addr |= (dont_care & 0x7) << 6;   // frac2
            addr |= (0x5) << 29;
            break;

        case AddrMode::MULTI_BANK_ENTRY:
            // 0-1: # of open banks mode (0:4, 1:8, 2:16), 29-31: topBits
            addr |= (last & 0x3) << 0;
            addr |= (0x6) << 29;
            break;

        case AddrMode::MULTI_BANK_EXIT:
            // Reserved payload, opcode only.
            addr |= (0x7) << 29;
            break;
    }

   return addr;
}

uint32_t get_dont_care_pos_maj3(uint32_t ra_group_index) {
    switch (ra_group_index) {
        case 0: return 0; // RA3,RA0
        case 1: return 1; // RA4,RA0
        case 2: return 2; // RA5,RA0
        case 3: return 3; // RA4,RA3
        case 4: return 4; // RA5,RA3
        case 5: return 5; // RA5,RA4
        case 6: return 6; // RA2,RA1
        default:
            std::cout << "[ERROR]: Invalid MAJ3 group index for dont care mapping" << std::endl;
            return 0;
    }
}

uint32_t get_dont_care_pos_maj5(uint32_t ra_group_index) {
    switch (ra_group_index) {
        case 7:  return 0; // RA4,RA3,RA0
        case 8:  return 1; // RA5,RA3,RA0
        case 9:  return 2; // RA5,RA4,RA0
        case 10: return 3; // RA5,RA4,RA3
        case 11: return 4; // RA2,RA1,RA0
        case 12: return 5; // RA3,RA2,RA1
        case 13: return 6; // RA4,RA2,RA1
        case 14: return 7; // RA5,RA2,RA1
        default:
            std::cout << "[ERROR]: Invalid MAJ5 group index for dont care mapping" << std::endl;
            return 0;
    }
}


// Extract row address (bits 0-16)
uint32_t extract_row_addr(uint64_t addr) {
    return addr & 0x1FFFF; // mask lower 17 bits
}

// Extract bank address (bits 17-20)
uint32_t extract_bank_addr(uint64_t addr) {
    return (addr >> 17) & 0xF; // shift right 17 bits and mask 4 bits
}

bool row_matches_any(uint32_t row, const std::vector<uint64_t>& rows) {
    for (auto r : rows) {
        if (extract_row_addr(r) == row) return true;
    }
    return false;
}

uint32_t get_mat_id_from_row(uint32_t global_row) {
    return global_row / ROWS_PER_MAT;
}

uint32_t get_local_row_from_global(uint32_t global_row) {
    return global_row % ROWS_PER_MAT;
}

uint32_t make_global_row(uint32_t mat_id, uint32_t local_row) {
    return mat_id * ROWS_PER_MAT + local_row;
}

uint64_t make_addr(uint32_t bank, uint32_t row) {
    return ((uint64_t)(bank & 0xF) << 17) | (uint64_t)(row & 0x1FFFF);
}
