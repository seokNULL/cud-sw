#include "../../include/cud/ops.h"
#include "../../include/cud/types.h"
#include <iostream>

uint32_t encode_end() {
    return 0x00000000;
}

uint32_t encode_multi_bank_entry(uint32_t open_banks_mode) {
    if (open_banks_mode > 2) {
        std::cout << "[ERROR]: encode_multi_bank_entry open_banks_mode must be 0(4), 1(8), or 2(16)" << std::endl;
        return 0;
    }
    return encode_uop(AddrMode::MULTI_BANK_ENTRY, 0, 0, open_banks_mode, 0, 0, 0);
}

uint32_t encode_multi_bank_exit() {
    return encode_uop(AddrMode::MULTI_BANK_EXIT, 0, 0, 0, 0, 0, 0);
}

std::vector<uint32_t> row_copy(uint64_t src, uint64_t dst) {
    std::vector<uint32_t> inst_list;

    uint32_t src_bank = extract_bank(src);
    uint32_t dst_bank = extract_bank(dst);

    if (src_bank != dst_bank) {
        std::cout << "[ERROR]: Invalid Bank Addr Pair within row_copy" << std::endl;
        return inst_list;
    }

    uint32_t src_row = extract_row(src);
    uint32_t dst_row = extract_row(dst);

    inst_list.push_back(encode_uop(AddrMode::ROW_COPY, src_bank, src_row, 0, 0, 0));
    inst_list.push_back(encode_uop(AddrMode::ROW_COPY_1, dst_bank, dst_row, 1, 0, 0));
    return inst_list;
}

std::vector<uint32_t> row_copy_fan(uint64_t src, uint64_t dst1, uint64_t dst2) {
    std::vector<uint32_t> inst_list;

    uint32_t src_bank  = extract_bank(src);
    uint32_t dst1_bank = extract_bank(dst1);
    uint32_t dst2_bank = extract_bank(dst2);

    if (src_bank != dst1_bank || src_bank != dst2_bank) {
        std::cout << "[ERROR]: Invalid Addr Pair within row_copy_fan" << std::endl;
        return inst_list;
    }

    uint32_t src_row  = extract_row(src);
    uint32_t dst1_row = extract_row(dst1);
    uint32_t dst2_row = extract_row(dst2);

    inst_list.push_back(encode_uop(AddrMode::ROW_COPY,   src_bank,  src_row,  0, 0, 0));
    inst_list.push_back(encode_uop(AddrMode::ROW_COPY_1, dst1_bank, dst1_row, 0, 0, 0));
    inst_list.push_back(encode_uop(AddrMode::ROW_COPY_1, dst2_bank, dst2_row, 1, 0, 0));
    return inst_list;
}
