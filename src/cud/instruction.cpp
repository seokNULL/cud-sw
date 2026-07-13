#include "../../include/cud/instruction.h"
#include "../../include/cxl/address_map.h"

// ── Internal helpers ──────────────────────────────────────────────────────────

// Decode a PA and pack BA, BG, ROW into the common instruction sub-fields.
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

static CudInst make_maj3(uint64_t pa, uint32_t frac_pos) {
    return CUD_FIELD_OPCODE(CUD_OP_MAJ3) | CUD_FIELD_FRAC(frac_pos) | addr_fields(pa);
}

static CudInst make_end() {
    return CUD_FIELD_OPCODE(CUD_OP_END);
}

// ── CUD kernel instruction generators ────────────────────────────────────────

std::vector<CudInst> cud_data_copy(uint64_t src_pa, uint64_t dst_pa) {
    return {
        make_rowcopy_src(src_pa),
        make_rowcopy_dst(dst_pa, /*last=*/true),
        make_end(),
    };
}

std::vector<CudInst> cud_maj3(uint64_t row0_pa, uint64_t row1_pa,
                               uint64_t row2_pa, uint32_t frac_pos) {
    return {
        make_maj3(row0_pa, frac_pos),
        make_maj3(row1_pa, frac_pos),
        make_maj3(row2_pa, frac_pos),
        make_end(),
    };
}
