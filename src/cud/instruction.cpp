#include "../../include/cud/instruction.h"
#include "../../include/cxl/address_map.h"

// ── Internal helpers ──────────────────────────────────────────────────────────

static uint32_t addr_fields(uint64_t pa) {
    const DramAddress a = decode_physical_addr(pa);
    return CUD_FIELD_BA(CUD_BANK_TO_BA(a.bank)) |
           CUD_FIELD_BG(CUD_BANK_TO_BG(a.bank)) |
           CUD_FIELD_ROW(a.row);
}

static CudInst make_rowcopy_src(uint64_t pa) {
    return CUD_FIELD_OPCODE(CUD_OP_ROWCOPY_SRC) | addr_fields(pa);
}

static CudInst make_rowcopy_dst(uint64_t pa, bool last) {
    return CUD_FIELD_OPCODE(CUD_OP_ROWCOPY_DST) |
           (last ? CUD_FIELD_LAST : 0u)          |
           addr_fields(pa);
}

static CudInst make_end() {
    return CUD_FIELD_OPCODE(CUD_OP_END);
}

// ── CUD kernel instruction generators ────────────────────────────────────────

std::vector<CudInst> cud_data_copy(uint64_t src_pa, uint64_t dst_pa) {
    std::vector<CudInst> insts;
    insts.push_back(make_rowcopy_src(src_pa));
    insts.push_back(make_rowcopy_dst(dst_pa, /*last=*/true));
    insts.push_back(make_end());
    return insts;
}
