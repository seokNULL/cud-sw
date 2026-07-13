#include "../../include/cud/instruction.h"
#include "../../include/cxl/address_map.h"

// Decode a PA and pack its BG, BA, ROW into the common instruction fields.
static uint32_t addr_fields(uint64_t pa) {
    const DramAddress a = decode_physical_addr(pa);
    return CUD_FIELD_BG(CUD_BANK_TO_BG(a.bank)) |
           CUD_FIELD_BA(CUD_BANK_TO_BA(a.bank)) |
           CUD_FIELD_ROW(a.row);
}

void append_end(std::vector<CudInst>& insts) {
    insts.push_back(CUD_FIELD_OPCODE(CUD_OP_END));
}

void append_rowcopy(uint64_t src_pa, uint64_t dst_pa,
                    std::vector<CudInst>& insts) {
    insts.push_back(CUD_FIELD_OPCODE(CUD_OP_ROWCOPY_SRC) | addr_fields(src_pa));
    insts.push_back(CUD_FIELD_OPCODE(CUD_OP_ROWCOPY_DST) |
                    CUD_FIELD_LAST                        |
                    addr_fields(dst_pa));
}

void append_maj3(uint64_t pa, uint32_t frac_pos,
                 std::vector<CudInst>& insts) {
    insts.push_back(CUD_FIELD_OPCODE(CUD_OP_MAJ3) |
                    CUD_FIELD_FRAC(frac_pos)       |
                    addr_fields(pa));
}

void append_mb_entry(uint32_t num_banks, std::vector<CudInst>& insts) {
    insts.push_back(CUD_FIELD_OPCODE(CUD_OP_MB_ENTRY) | CUD_FIELD_MB_NUM(num_banks));
}

void append_mb_exit(std::vector<CudInst>& insts) {
    insts.push_back(CUD_FIELD_OPCODE(CUD_OP_MB_EXIT));
}
