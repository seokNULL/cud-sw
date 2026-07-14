#include "../../../include/cud/compute_lib/logical.h"
#include "../../../include/cxl/address_map.h"
#include "../cud_inst_helpers.h"

// ── Fixed compute-row configuration ──────────────────────────────────────────
// 4-row set follows the CUD DRAM grouping pattern {n, n+1, n+8, n+9}.
static constexpr uint32_t kCmpRow0    = 0xFF00u;
static constexpr uint32_t kCmpRow1    = 0xFF01u;
static constexpr uint32_t kCmpRow2    = 0xFF08u;
static constexpr uint32_t kCmpRowFrac = 0xFF09u;
static constexpr uint32_t kFracPos    = 3u;

// AND and OR differ only in the bias row (zero for AND, one for OR).
// No validation — caller's responsibility (see CudAnd / CudOr in instruction.cpp).
static std::vector<CudInst> build_maj3_op(uint64_t a_pa, uint64_t b_pa,
                                           uint64_t bias_pa, uint64_t dst_pa) {
    const uint32_t bank = decode_physical_addr(a_pa).bank;
    const uint64_t cmp0 = encode_dram_addr({0, bank, kCmpRow0, 0});
    const uint64_t cmp1 = encode_dram_addr({0, bank, kCmpRow1, 0});
    const uint64_t cmp2 = encode_dram_addr({0, bank, kCmpRow2, 0});

    std::vector<CudInst> insts;

    insts.push_back(cud_make_rowcopy_src(a_pa));
    insts.push_back(cud_make_rowcopy_dst(cmp0));

    insts.push_back(cud_make_rowcopy_src(b_pa));
    insts.push_back(cud_make_rowcopy_dst(cmp1));

    insts.push_back(cud_make_rowcopy_src(bias_pa));
    insts.push_back(cud_make_rowcopy_dst(cmp2));

    insts.push_back(cud_make_maj3(cmp0, kFracPos));
    insts.push_back(cud_make_maj3(cmp1, kFracPos));
    insts.push_back(cud_make_maj3(cmp2, kFracPos));

    insts.push_back(cud_make_rowcopy_src(cmp0));
    insts.push_back(cud_make_rowcopy_dst(dst_pa));

    insts.push_back(cud_make_end());
    return insts;
}

// ── Public kernels ────────────────────────────────────────────────────────────

std::vector<CudInst> cud_and(uint64_t a_pa, uint64_t b_pa,
                              uint64_t zero_pa, uint64_t dst_pa) {
    return build_maj3_op(a_pa, b_pa, zero_pa, dst_pa);
}

std::vector<CudInst> cud_or(uint64_t a_pa, uint64_t b_pa,
                             uint64_t one_pa, uint64_t dst_pa) {
    return build_maj3_op(a_pa, b_pa, one_pa, dst_pa);
}
