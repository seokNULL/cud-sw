#include "../../../include/cud/compute_lib/data_copy.h"
#include "../../../include/cxl/address_map.h"
#include "../cud_inst_helpers.h"

#include <cassert>

std::vector<CudInst> cud_data_copy(uint64_t src_pa, uint64_t dst_pa,
                                    size_t size_bytes) {
    assert(size_bytes > 0 && size_bytes % CUD_ROW_SIZE_BYTES == 0);

    const DramAddress src = decode_physical_addr(src_pa);
    const DramAddress dst = decode_physical_addr(dst_pa);
    assert(src.bank == dst.bank && "src and dst must be in the same bank");

    const size_t n_rows = size_bytes / CUD_ROW_SIZE_BYTES;

    std::vector<CudInst> insts;
    for (size_t i = 0; i < n_rows; ++i) {
        const uint64_t s = encode_dram_addr({0, src.bank, src.row + (uint32_t)i, 0});
        const uint64_t d = encode_dram_addr({0, dst.bank, dst.row + (uint32_t)i, 0});
        insts.push_back(cud_make_rowcopy_src(s));
        insts.push_back(cud_make_rowcopy_dst(d));
    }
    insts.push_back(cud_make_end());
    return insts;
}
