#include "../../include/cud/instruction.h"
#include "../../include/cud/compute_lib/data_copy.h"
#include "../../include/cud/compute_lib/logical.h"
#include "../../include/cxl/address_map.h"
#include "cud_inst_helpers.h"

#include <cassert>

std::vector<CudInst> CudDataCopy(uint64_t src_pa, uint64_t dst_pa, size_t size_bytes) {
    assert(size_bytes > 0 && size_bytes % CUD_ROW_SIZE_BYTES == 0);
    const DramAddress src = decode_physical_addr(src_pa);
    const DramAddress dst = decode_physical_addr(dst_pa);
    assert(src.bank == dst.bank && "src and dst must be in the same bank");

    const size_t n_rows = size_bytes / CUD_ROW_SIZE_BYTES;
    std::vector<CudInst> insts;
    for (size_t i = 0; i < n_rows; ++i) {
        const uint64_t s = encode_dram_addr({0, src.bank, src.row + (uint32_t)i, 0});
        const uint64_t d = encode_dram_addr({0, dst.bank, dst.row + (uint32_t)i, 0});
        const auto row = cud_data_copy(s, d);
        insts.insert(insts.end(), row.begin(), row.end());
    }
    insts.push_back(cud_make_end());
    return insts;
}

std::vector<CudInst> CudAnd(uint64_t a_pa, uint64_t b_pa,
                             uint64_t zero_pa, uint64_t dst_pa) {
    const uint32_t bank = decode_physical_addr(a_pa).bank;
    assert(decode_physical_addr(b_pa).bank    == bank && "b must be in same bank as a");
    assert(decode_physical_addr(zero_pa).bank == bank && "zero must be in same bank as a");
    assert(decode_physical_addr(dst_pa).bank  == bank && "dst must be in same bank as a");
    return cud_and(a_pa, b_pa, zero_pa, dst_pa);
}

std::vector<CudInst> CudOr(uint64_t a_pa, uint64_t b_pa,
                            uint64_t one_pa, uint64_t zero_pa, uint64_t dst_pa) {
    const uint32_t bank = decode_physical_addr(a_pa).bank;
    assert(decode_physical_addr(b_pa).bank    == bank && "b must be in same bank as a");
    assert(decode_physical_addr(one_pa).bank  == bank && "one must be in same bank as a");
    assert(decode_physical_addr(zero_pa).bank == bank && "zero must be in same bank as a");
    assert(decode_physical_addr(dst_pa).bank  == bank && "dst must be in same bank as a");
    return cud_or(a_pa, b_pa, one_pa, zero_pa, dst_pa);
}
