#include "../../../include/cud/compute_lib/logical.h"
#include "../../../include/cxl/address_map.h"
#include "../cud_inst_helpers.h"

#include <cassert>

// ── Fixed compute-row configuration ──────────────────────────────────────────
//
// MAJ3 operates on a 4-row set {kCmpRow0, kCmpRow1, kCmpRow2, kCmpRowFrac}.
// The set follows the CUD DRAM grouping pattern {n, n+1, n+8, n+9}, where
// the frac row (result) is at position kFracPos within the set.
//
// After MAJ3, the result is shared across all 4 rows; any may be used as
// the source for the subsequent ROWCOPY to dst.
//
// Adjust these constants to match the actual DRAM subarray layout.
static constexpr uint32_t kCmpRow0    = 0xFF00u;  // copy of input a
static constexpr uint32_t kCmpRow1    = 0xFF01u;  // copy of input b
static constexpr uint32_t kCmpRow2    = 0xFF08u;  // copy of bias (0 or 1)
static constexpr uint32_t kCmpRowFrac = 0xFF09u;  // frac row (result sink)
static constexpr uint32_t kFracPos    = 3u;       // position of frac in {FF00,FF01,FF08,FF09}

// ── Shared kernel builder ─────────────────────────────────────────────────────
//
// AND and OR differ only in the bias row (zero for AND, one for OR).
// Steps:
//   1. ROWCOPY a     → compute_row_0
//   2. ROWCOPY b     → compute_row_1
//   3. ROWCOPY bias  → compute_row_2
//   4. MAJ3 on (compute_row_0, compute_row_1, compute_row_2)
//      → result shared in all 4 rows of the MAJ3 set
//   5. ROWCOPY compute_row_0 (holds result) → dst
static std::vector<CudInst> build_maj3_op(uint64_t a_pa, uint64_t b_pa,
                                           uint64_t bias_pa, uint64_t dst_pa) {
    const DramAddress a_addr = decode_physical_addr(a_pa);
    assert(decode_physical_addr(b_pa).bank    == a_addr.bank && "b must be in same bank as a");
    assert(decode_physical_addr(bias_pa).bank == a_addr.bank && "bias must be in same bank as a");
    assert(decode_physical_addr(dst_pa).bank  == a_addr.bank && "dst must be in same bank as a");

    const uint32_t bank = a_addr.bank;
    const uint64_t cmp0 = encode_dram_addr({0, bank, kCmpRow0,    0});
    const uint64_t cmp1 = encode_dram_addr({0, bank, kCmpRow1,    0});
    const uint64_t cmp2 = encode_dram_addr({0, bank, kCmpRow2,    0});

    std::vector<CudInst> insts;

    // Step 1: a → compute_row_0
    insts.push_back(cud_make_rowcopy_src(a_pa));
    insts.push_back(cud_make_rowcopy_dst(cmp0));

    // Step 2: b → compute_row_1
    insts.push_back(cud_make_rowcopy_src(b_pa));
    insts.push_back(cud_make_rowcopy_dst(cmp1));

    // Step 3: bias → compute_row_2
    insts.push_back(cud_make_rowcopy_src(bias_pa));
    insts.push_back(cud_make_rowcopy_dst(cmp2));

    // Step 4: MAJ3 — issue one instruction per input row, same frac_pos for all
    insts.push_back(cud_make_maj3(cmp0, kFracPos));
    insts.push_back(cud_make_maj3(cmp1, kFracPos));
    insts.push_back(cud_make_maj3(cmp2, kFracPos));

    // Step 5: copy result → dst  (cmp0 holds result after MAJ3)
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
