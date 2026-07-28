#include "../../../include/cud/compute_lib/inst_gen.h"
#include "../../../include/cud/compute_lib/compute_rows.h"
#include "../../../include/cxl/address_map.h"
#include "../cud_inst_helpers.h"

#include <cassert>
#include <vector>

// ── Internal helpers ──────────────────────────────────────────────────────────

// Appends instructions for: dst_row = AND(x_pa, y_pa)
//   ROWCOPY x,y,zero into compute group → MAJ3(bias=0) → ROWCOPY frac to dst
static void gen_and(std::vector<CudInst>& v,
                    uint64_t x_pa, uint64_t y_pa,
                    const ScratchAllocator& sc, uint32_t dst_row) {
    const auto   cmp  = cmp_group_rows(0, kInstGenCmpBase);
    const uint64_t c0 = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[0]), 0});
    const uint64_t c1 = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[1]), 0});
    const uint64_t c2 = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[2]), 0});
    const uint64_t cf = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[3]), 0});
    const uint64_t zp = encode_dram_addr({0, sc.bank, sc.abs_row(kZeroRow), 0});
    const uint64_t dp = encode_dram_addr({0, sc.bank, dst_row, 0});

    v.push_back(cud_make_rowcopy_src(x_pa));  v.push_back(cud_make_rowcopy_dst(c0));
    v.push_back(cud_make_rowcopy_src(y_pa));  v.push_back(cud_make_rowcopy_dst(c1));
    v.push_back(cud_make_rowcopy_src(zp));    v.push_back(cud_make_rowcopy_dst(c2));  // bias = 0
    v.push_back(cud_make_rowcopy_src(zp));    v.push_back(cud_make_rowcopy_dst(cf));  // init frac
    v.push_back(cud_make_maj3(c0, kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(cf));    v.push_back(cud_make_rowcopy_dst(dp));
}

// Appends instructions for: dst_row = OR(x_pa, y_pa)
//   ROWCOPY x,y,ones into compute group → MAJ3(bias=1) → ROWCOPY frac to dst
static void gen_or(std::vector<CudInst>& v,
                   uint64_t x_pa, uint64_t y_pa,
                   const ScratchAllocator& sc, uint32_t dst_row) {
    const auto   cmp  = cmp_group_rows(0, kInstGenCmpBase);
    const uint64_t c0 = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[0]), 0});
    const uint64_t c1 = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[1]), 0});
    const uint64_t c2 = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[2]), 0});
    const uint64_t cf = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[3]), 0});
    const uint64_t op = encode_dram_addr({0, sc.bank, sc.abs_row(kOnesRow), 0});
    const uint64_t zp = encode_dram_addr({0, sc.bank, sc.abs_row(kZeroRow), 0});
    const uint64_t dp = encode_dram_addr({0, sc.bank, dst_row, 0});

    v.push_back(cud_make_rowcopy_src(x_pa));  v.push_back(cud_make_rowcopy_dst(c0));
    v.push_back(cud_make_rowcopy_src(y_pa));  v.push_back(cud_make_rowcopy_dst(c1));
    v.push_back(cud_make_rowcopy_src(op));    v.push_back(cud_make_rowcopy_dst(c2));  // bias = 1
    v.push_back(cud_make_rowcopy_src(zp));    v.push_back(cud_make_rowcopy_dst(cf));  // init frac
    v.push_back(cud_make_maj3(c0, kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(cf));    v.push_back(cud_make_rowcopy_dst(dp));
}

// ── Public generators ─────────────────────────────────────────────────────────

std::vector<CudInst> gen_xor(
    const BitSerialLayout& a,
    const BitSerialLayout& not_a,
    const BitSerialLayout& b,
    const BitSerialLayout& not_b,
    const BitSerialLayout& out,
    ScratchAllocator& scratch)
{
    assert(a.bit_width == b.bit_width && a.bit_width == out.bit_width);
    assert(a.bank == b.bank && a.bank == out.bank);

    const uint32_t W = a.bit_width;

    // Two scratch rows reused across all bit-planes
    const uint32_t t1_off = scratch.alloc(1);
    const uint32_t t2_off = scratch.alloc(1);
    const uint64_t t1_pa  = encode_dram_addr({0, scratch.bank, scratch.abs_row(t1_off), 0});
    const uint64_t t2_pa  = encode_dram_addr({0, scratch.bank, scratch.abs_row(t2_off), 0});

    std::vector<CudInst> insts;
    for (uint32_t i = 0; i < W; ++i) {
        // t1 = AND(a[i], ~b[i])
        gen_and(insts, a.plane_pa(i), not_b.plane_pa(i), scratch, scratch.abs_row(t1_off));
        // t2 = AND(~a[i], b[i])
        gen_and(insts, not_a.plane_pa(i), b.plane_pa(i),  scratch, scratch.abs_row(t2_off));
        // out[i] = OR(t1, t2)
        gen_or(insts, t1_pa, t2_pa, scratch, out.plane_row(i));
    }
    insts.push_back(cud_make_end());
    return insts;
}
