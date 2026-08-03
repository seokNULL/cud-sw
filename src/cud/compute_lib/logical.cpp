#include "../../../include/cud/compute_lib/logical.h"
#include "../../../include/cud/compute_lib/inst_gen.h"
#include "../../../include/cud/compute_lib/compute_rows.h"
#include "../../../include/cxl/address_map.h"
#include "../cud_inst_helpers.h"

#include <cassert>
#include <vector>

// ── cud_and / cud_or (legacy single-row API) ──────────────────────────────────

static std::vector<CudInst> build_maj3_op(uint64_t a_pa, uint64_t b_pa,
                                           uint64_t bias_pa, uint64_t zero_pa,
                                           uint64_t dst_pa) {
    const uint32_t bank    = decode_physical_addr(a_pa).bank;
    const auto     rows    = cmp_group_rows(0);
    const uint64_t cmp0    = encode_dram_addr({0, bank, rows[0], 0});
    const uint64_t cmp1    = encode_dram_addr({0, bank, rows[1], 0});
    const uint64_t cmp2    = encode_dram_addr({0, bank, rows[2], 0});
    const uint64_t cmpFrac = encode_dram_addr({0, bank, rows[3], 0});

    std::vector<CudInst> insts;
    insts.push_back(cud_make_rowcopy_src(a_pa));
    insts.push_back(cud_make_rowcopy_dst(cmp0));
    insts.push_back(cud_make_rowcopy_src(b_pa));
    insts.push_back(cud_make_rowcopy_dst(cmp1));
    insts.push_back(cud_make_rowcopy_src(bias_pa));
    insts.push_back(cud_make_rowcopy_dst(cmp2));
    insts.push_back(cud_make_rowcopy_src(zero_pa));
    insts.push_back(cud_make_rowcopy_dst(cmpFrac));
    insts.push_back(cud_make_maj3(cmp0, kCmpFracPos, 0u));
    insts.push_back(cud_make_rowcopy_src(cmpFrac));
    insts.push_back(cud_make_rowcopy_dst(dst_pa));
    insts.push_back(cud_make_end());
    return insts;
}

std::vector<CudInst> cud_and(uint64_t a_pa, uint64_t b_pa,
                              uint64_t zero_pa, uint64_t dst_pa) {
    return build_maj3_op(a_pa, b_pa, zero_pa, zero_pa, dst_pa);
}

std::vector<CudInst> cud_or(uint64_t a_pa, uint64_t b_pa,
                             uint64_t one_pa, uint64_t zero_pa, uint64_t dst_pa) {
    return build_maj3_op(a_pa, b_pa, one_pa, zero_pa, dst_pa);
}

// ── Internal helpers: single bit-plane AND / OR (11 insts each, no END) ───────

static void gen_and_plane(std::vector<CudInst>& v,
                           uint64_t x_pa, uint64_t y_pa,
                           const ScratchAllocator& sc, uint32_t dst_abs_row) {
    const auto   cmp = cmp_group_rows(0, kInstGenCmpBase);
    const uint64_t c0 = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[0]), 0});
    const uint64_t c1 = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[1]), 0});
    const uint64_t c2 = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[2]), 0});
    const uint64_t cf = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[3]), 0});
    const uint64_t zp = encode_dram_addr({0, sc.bank, sc.abs_row(kZeroRow), 0});
    const uint64_t dp = encode_dram_addr({0, sc.bank, dst_abs_row, 0});
    v.push_back(cud_make_rowcopy_src(x_pa));  v.push_back(cud_make_rowcopy_dst(c0));
    v.push_back(cud_make_rowcopy_src(y_pa));  v.push_back(cud_make_rowcopy_dst(c1));
    v.push_back(cud_make_rowcopy_src(zp));    v.push_back(cud_make_rowcopy_dst(c2));
    v.push_back(cud_make_rowcopy_src(zp));    v.push_back(cud_make_rowcopy_dst(cf));
    v.push_back(cud_make_maj3(c0, kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(cf));    v.push_back(cud_make_rowcopy_dst(dp));
}

static void gen_or_plane(std::vector<CudInst>& v,
                          uint64_t x_pa, uint64_t y_pa,
                          const ScratchAllocator& sc, uint32_t dst_abs_row) {
    const auto   cmp = cmp_group_rows(0, kInstGenCmpBase);
    const uint64_t c0 = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[0]), 0});
    const uint64_t c1 = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[1]), 0});
    const uint64_t c2 = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[2]), 0});
    const uint64_t cf = encode_dram_addr({0, sc.bank, sc.abs_row(cmp[3]), 0});
    const uint64_t op = encode_dram_addr({0, sc.bank, sc.abs_row(kOnesRow), 0});
    const uint64_t zp = encode_dram_addr({0, sc.bank, sc.abs_row(kZeroRow), 0});
    const uint64_t dp = encode_dram_addr({0, sc.bank, dst_abs_row, 0});
    v.push_back(cud_make_rowcopy_src(x_pa));  v.push_back(cud_make_rowcopy_dst(c0));
    v.push_back(cud_make_rowcopy_src(y_pa));  v.push_back(cud_make_rowcopy_dst(c1));
    v.push_back(cud_make_rowcopy_src(op));    v.push_back(cud_make_rowcopy_dst(c2));
    v.push_back(cud_make_rowcopy_src(zp));    v.push_back(cud_make_rowcopy_dst(cf));
    v.push_back(cud_make_maj3(c0, kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(cf));    v.push_back(cud_make_rowcopy_dst(dp));
}

// ── Public W-bit generators: gen_and, gen_or ─────────────────────────────────

std::vector<CudInst> gen_and(
    const BitSerialLayout& a,
    const BitSerialLayout& b,
    const BitSerialLayout& out,
    ScratchAllocator& scratch)
{
    assert(a.bit_width == b.bit_width && a.bit_width == out.bit_width);
    assert(a.bank == b.bank && a.bank == out.bank);
    const uint32_t W = a.bit_width;
    std::vector<CudInst> insts;
    for (uint32_t i = 0; i < W; ++i)
        gen_and_plane(insts, a.plane_pa(i), b.plane_pa(i), scratch, out.plane_row(i));
    insts.push_back(cud_make_end());
    return insts;
}

std::vector<CudInst> gen_or(
    const BitSerialLayout& a,
    const BitSerialLayout& b,
    const BitSerialLayout& out,
    ScratchAllocator& scratch)
{
    assert(a.bit_width == b.bit_width && a.bit_width == out.bit_width);
    assert(a.bank == b.bank && a.bank == out.bank);
    const uint32_t W = a.bit_width;
    std::vector<CudInst> insts;
    for (uint32_t i = 0; i < W; ++i)
        gen_or_plane(insts, a.plane_pa(i), b.plane_pa(i), scratch, out.plane_row(i));
    insts.push_back(cud_make_end());
    return insts;
}

// ── gen_xor (moved from xor.cpp) ─────────────────────────────────────────────
//
// XOR = OR(AND(a, ~b), AND(~a, b))  — 33 insts/bit-plane, 2 scratch rows.

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

    const uint32_t W      = a.bit_width;
    const uint32_t t1_off = scratch.alloc(1);
    const uint32_t t2_off = scratch.alloc(1);
    const uint64_t t1_pa  = encode_dram_addr({0, scratch.bank, scratch.abs_row(t1_off), 0});
    const uint64_t t2_pa  = encode_dram_addr({0, scratch.bank, scratch.abs_row(t2_off), 0});

    std::vector<CudInst> insts;
    for (uint32_t i = 0; i < W; ++i) {
        gen_and_plane(insts, a.plane_pa(i),     not_b.plane_pa(i), scratch, scratch.abs_row(t1_off));
        gen_and_plane(insts, not_a.plane_pa(i), b.plane_pa(i),     scratch, scratch.abs_row(t2_off));
        gen_or_plane (insts, t1_pa, t2_pa, scratch, out.plane_row(i));
    }
    insts.push_back(cud_make_end());
    return insts;
}
