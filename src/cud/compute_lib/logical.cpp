#include "../../../include/cud/compute_lib/logical.h"
#include "../../../include/cxl/address_map.h"
#include "../cud_inst_helpers.h"

// ── Fixed compute-row configuration ──────────────────────────────────────────
// 4-row set follows the CUD DRAM grouping pattern {n, n+1, n+8, n+9}.
static constexpr uint32_t kCmpRow0    = 0x0010u;
static constexpr uint32_t kCmpRow1    = 0x0011u;
static constexpr uint32_t kCmpRow2    = 0x0018u;
static constexpr uint32_t kCmpRowFrac = 0x0019u;
static constexpr uint32_t kFracPos    = 3u;

// AND and OR differ only in the bias row (zero for AND, one for OR).
// zero_pa is a pre-zeroed row used to initialise the frac compute row.
// No validation — caller's responsibility (see CudAnd / CudOr in instruction.cpp).
static std::vector<CudInst> build_maj3_op(uint64_t a_pa, uint64_t b_pa,
                                           uint64_t bias_pa, uint64_t zero_pa,
                                           uint64_t dst_pa) {
    // mode 0: RA3 and RA0 are don't-care → group {n, n+1, n+8, n+9}
    static constexpr uint32_t kMode = 0u;

    const uint32_t bank    = decode_physical_addr(a_pa).bank;
    const uint64_t cmp0    = encode_dram_addr({0, bank, kCmpRow0,    0});
    const uint64_t cmp1    = encode_dram_addr({0, bank, kCmpRow1,    0});
    const uint64_t cmp2    = encode_dram_addr({0, bank, kCmpRow2,    0});
    const uint64_t cmpFrac = encode_dram_addr({0, bank, kCmpRowFrac, 0});

    std::vector<CudInst> insts;

    // Copy operands into the three non-frac compute rows
    insts.push_back(cud_make_rowcopy_src(a_pa));
    insts.push_back(cud_make_rowcopy_dst(cmp0));

    insts.push_back(cud_make_rowcopy_src(b_pa));
    insts.push_back(cud_make_rowcopy_dst(cmp1));

    insts.push_back(cud_make_rowcopy_src(bias_pa));
    insts.push_back(cud_make_rowcopy_dst(cmp2));

    // Initialise frac row to 0
    insts.push_back(cud_make_rowcopy_src(zero_pa));
    insts.push_back(cud_make_rowcopy_dst(cmpFrac));

    // MAJ3 on each of the three non-frac rows
    insts.push_back(cud_make_maj3(cmp0, kFracPos, kMode));

    // Copy result (held in frac row after MAJ3) to destination
    insts.push_back(cud_make_rowcopy_src(cmpFrac));
    insts.push_back(cud_make_rowcopy_dst(dst_pa));

    insts.push_back(cud_make_end());
    return insts;
}

// ── Public kernels ────────────────────────────────────────────────────────────

std::vector<CudInst> cud_and(uint64_t a_pa, uint64_t b_pa,
                              uint64_t zero_pa, uint64_t dst_pa) {
    // AND = MAJ3(a, b, 0); zero_pa serves as both bias and frac-row initialiser
    return build_maj3_op(a_pa, b_pa, zero_pa, zero_pa, dst_pa);
}

std::vector<CudInst> cud_or(uint64_t a_pa, uint64_t b_pa,
                             uint64_t one_pa, uint64_t zero_pa, uint64_t dst_pa) {
    // OR = MAJ3(a, b, 1); one_pa is bias, zero_pa initialises frac row
    return build_maj3_op(a_pa, b_pa, one_pa, zero_pa, dst_pa);
}
