#include "../../../include/cud/compute_lib/inst_gen.h"
#include "../../../include/cud/compute_lib/compute_rows.h"
#include "../../../include/cxl/address_map.h"
#include "../cud_inst_helpers.h"

#include <cassert>
#include <vector>

// ── Internal helpers ──────────────────────────────────────────────────────────

// dst_row = AND(x_pa, y_pa)  [MAJ3 with bias=0]
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

// dst_row = OR(x_pa, y_pa)  [MAJ3 with bias=1]
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

// dst_row = MAJ3(x_pa, y_pa, z_pa)  — general 3-input majority.
// gen_and = gen_maj3(x, y, zero_pa), gen_or = gen_maj3(x, y, ones_pa).
// NOT(MAJ3(a,b,c)) == MAJ3(~a,~b,~c), so complement of carry is free.
static void gen_maj3(std::vector<CudInst>& v,
                     uint64_t x_pa, uint64_t y_pa, uint64_t z_pa,
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
    v.push_back(cud_make_rowcopy_src(z_pa));  v.push_back(cud_make_rowcopy_dst(c2));
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
        // t1 = AND(a[i], ~b[i]),  t2 = AND(~a[i], b[i]),  out[i] = OR(t1, t2)
        gen_and(insts, a.plane_pa(i), not_b.plane_pa(i), scratch, scratch.abs_row(t1_off));
        gen_and(insts, not_a.plane_pa(i), b.plane_pa(i), scratch, scratch.abs_row(t2_off));
        gen_or(insts, t1_pa, t2_pa, scratch, out.plane_row(i));
    }
    insts.push_back(cud_make_end());
    return insts;
}

// ── ADD (Ripple Carry Adder) ──────────────────────────────────────────────────
//
// Per-bit cost:
//   bit 0  (half adder): 2 AND + 1 OR (carry)  +  2 AND + 1 OR (sum XOR2)  = 55 insts
//   bit i≥1 (full adder):
//     s[i] = XOR3(a[i], b[i], c[i-1])
//              via  t  = XOR2(a[i],b[i])    (3 AND/OR = 33 insts)
//                   ~t = XNOR(a[i],b[i])    (3 AND/OR = 33 insts)
//                   s  = XOR2(t, c[i-1])    (2 AND + 1 OR = 33 insts)  → 99 insts
//     c[i]  = MAJ3(a[i], b[i], c[i-1])     → 11 insts   (direct HW carry)
//     ~c[i] = MAJ3(~a[i], ~b[i], ~c[i-1])  → 11 insts
//                                                          → 121 insts/bit
//   carry-out: 1 ROWCOPY pair = 2 insts
//
// Total: 55 + (W-1)*121 + 2 + 1(END)  →  W=1:58  W=4:421  W=8:905

std::vector<CudInst> gen_add(
    const BitSerialLayout& a, const BitSerialLayout& not_a,
    const BitSerialLayout& b, const BitSerialLayout& not_b,
    const BitSerialLayout& out,
    uint8_t W,
    ScratchAllocator& scratch)
{
    assert(W >= 1 && W <= 8);
    assert(a.bit_width == W && b.bit_width == W && out.bit_width == W + 1u);
    assert(a.bank == b.bank && a.bank == out.bank && a.bank == scratch.bank);

    // 5 scratch rows, reused across all bits
    const uint32_t c_off    = scratch.alloc(1);  // carry  c[i]
    const uint32_t nc_off   = scratch.alloc(1);  // ~carry ~c[i]
    const uint32_t xab_off  = scratch.alloc(1);  // XOR(a[i],b[i]) / reused as 2nd AND temp
    const uint32_t nxab_off = scratch.alloc(1);  // XNOR(a[i],b[i])
    const uint32_t tmp_off  = scratch.alloc(1);  // 1st AND temp

    const uint64_t c_pa    = encode_dram_addr({0, scratch.bank, scratch.abs_row(c_off),    0});
    const uint64_t nc_pa   = encode_dram_addr({0, scratch.bank, scratch.abs_row(nc_off),   0});
    const uint64_t xab_pa  = encode_dram_addr({0, scratch.bank, scratch.abs_row(xab_off),  0});
    const uint64_t nxab_pa = encode_dram_addr({0, scratch.bank, scratch.abs_row(nxab_off), 0});
    const uint64_t tmp_pa  = encode_dram_addr({0, scratch.bank, scratch.abs_row(tmp_off),  0});

    std::vector<CudInst> insts;

    // ── Bit 0: half adder (carry-in = 0) ──────────────────────────────────────
    {
        const uint64_t a0  = a.plane_pa(0);
        const uint64_t na0 = not_a.plane_pa(0);
        const uint64_t b0  = b.plane_pa(0);
        const uint64_t nb0 = not_b.plane_pa(0);

        // c[0]  = AND(a[0], b[0])
        gen_and(insts, a0, b0, scratch, scratch.abs_row(c_off));
        // ~c[0] = OR(~a[0], ~b[0])  [= NOT(a[0] AND b[0]) by De Morgan]
        gen_or(insts, na0, nb0, scratch, scratch.abs_row(nc_off));
        // s[0]  = XOR2(a[0], b[0]) = OR(AND(a,~b), AND(~a,b))
        gen_and(insts, a0, nb0, scratch, scratch.abs_row(xab_off));  // xab = AND(a,~b)
        gen_and(insts, na0, b0, scratch, scratch.abs_row(tmp_off));  // tmp = AND(~a,b)
        gen_or(insts, xab_pa, tmp_pa, scratch, out.plane_row(0));
    }

    // ── Bits 1..W-1: full adder ───────────────────────────────────────────────
    for (uint8_t i = 1; i < W; ++i) {
        const uint64_t ai  = a.plane_pa(i);
        const uint64_t nai = not_a.plane_pa(i);
        const uint64_t bi  = b.plane_pa(i);
        const uint64_t nbi = not_b.plane_pa(i);

        // s[i] = XOR3(a[i], b[i], c[i-1])
        // --- t = XOR(a[i], b[i]) ---
        gen_and(insts, ai,  nbi, scratch, scratch.abs_row(xab_off));        // xab = AND(a,~b)
        gen_and(insts, nai, bi,  scratch, scratch.abs_row(tmp_off));         // tmp = AND(~a,b)
        gen_or(insts, xab_pa, tmp_pa, scratch, scratch.abs_row(xab_off));   // xab = XOR(a,b)
        // --- ~t = XNOR(a[i], b[i]) ---
        gen_and(insts, ai,  bi,  scratch, scratch.abs_row(nxab_off));        // nxab = AND(a,b)
        gen_and(insts, nai, nbi, scratch, scratch.abs_row(tmp_off));         // tmp = AND(~a,~b)
        gen_or(insts, nxab_pa, tmp_pa, scratch, scratch.abs_row(nxab_off)); // nxab = XNOR(a,b)
        // --- s[i] = XOR(t, c[i-1]) ---
        gen_and(insts, xab_pa,  nc_pa, scratch, scratch.abs_row(tmp_off));  // tmp  = AND(t, ~c)
        gen_and(insts, nxab_pa, c_pa,  scratch, scratch.abs_row(xab_off));  // xab  = AND(~t, c)
        gen_or(insts, tmp_pa, xab_pa, scratch, out.plane_row(i));            // s[i] = OR(...)

        // c[i]  = MAJ3(a[i], b[i], c[i-1])   — HW carry, reads c_pa then overwrites c_off
        gen_maj3(insts, ai, bi, c_pa, scratch, scratch.abs_row(c_off));
        // ~c[i] = MAJ3(~a[i], ~b[i], ~c[i-1]) — reads nc_pa then overwrites nc_off
        gen_maj3(insts, nai, nbi, nc_pa, scratch, scratch.abs_row(nc_off));
    }

    // ── Carry out (bit W): c[W-1] already in c_off ────────────────────────────
    const uint64_t out_carry_pa = encode_dram_addr({0, out.bank, out.plane_row(W), 0});
    insts.push_back(cud_make_rowcopy_src(c_pa));
    insts.push_back(cud_make_rowcopy_dst(out_carry_pa));

    insts.push_back(cud_make_end());
    return insts;
}
