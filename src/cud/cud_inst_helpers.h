#pragma once
#include "../../include/cud/instruction.h"
#include "../../include/cxl/address_map.h"

// Internal instruction-build helpers shared by src/cud/ and src/cud/compute_lib/.
// Not part of the public API.

static inline uint32_t cud_addr_fields(uint64_t pa) {
    const DramAddress a = decode_physical_addr(pa);
    return CUD_FIELD_BA(CUD_BANK_TO_BA(a.bank)) |
           CUD_FIELD_BG(CUD_BANK_TO_BG(a.bank)) |
           CUD_FIELD_ROW(a.row);
}

static inline CudInst cud_make_rowcopy_src(uint64_t pa) {
    return CUD_FIELD_OPCODE(CUD_OP_ROWCOPY_SRC) | cud_addr_fields(pa);
}

static inline CudInst cud_make_rowcopy_dst(uint64_t pa) {
    return CUD_FIELD_OPCODE(CUD_OP_ROWCOPY_DST) | CUD_FIELD_LAST | cud_addr_fields(pa);
}

static inline CudInst cud_make_maj3(uint64_t pa, uint32_t frac_pos) {
    return CUD_FIELD_OPCODE(CUD_OP_MAJ3) | CUD_FIELD_FRAC(frac_pos) | cud_addr_fields(pa);
}

static inline CudInst cud_make_end() {
    return CUD_FIELD_OPCODE(CUD_OP_END);
}
