#include "../../../include/cud/compute_lib/inst_gen.h"
#include "../../../include/cud/compute_lib/compute_rows.h"
#include "../../../include/cxl/address_map.h"
#include "../cud_inst_helpers.h"

#include <cassert>
#include <vector>

// ── Internal helpers (reused by gen_xor) ─────────────────────────────────────

// ROWCOPY_DST without LAST bit: intermediate destination in a fan-out chain.
static inline CudInst cud_make_rowcopy_dst_cont(uint64_t pa) {
    return CUD_FIELD_OPCODE(CUD_OP_ROWCOPY_DST) | cud_addr_fields(pa);
}

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
    v.push_back(cud_make_rowcopy_src(zp));    v.push_back(cud_make_rowcopy_dst(c2));
    v.push_back(cud_make_rowcopy_src(zp));    v.push_back(cud_make_rowcopy_dst(cf));
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
    v.push_back(cud_make_rowcopy_src(op));    v.push_back(cud_make_rowcopy_dst(c2));
    v.push_back(cud_make_rowcopy_src(zp));    v.push_back(cud_make_rowcopy_dst(cf));
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

    const uint32_t t1_off = scratch.alloc(1);
    const uint32_t t2_off = scratch.alloc(1);
    const uint64_t t1_pa  = encode_dram_addr({0, scratch.bank, scratch.abs_row(t1_off), 0});
    const uint64_t t2_pa  = encode_dram_addr({0, scratch.bank, scratch.abs_row(t2_off), 0});

    std::vector<CudInst> insts;
    for (uint32_t i = 0; i < W; ++i) {
        gen_and(insts, a.plane_pa(i), not_b.plane_pa(i), scratch, scratch.abs_row(t1_off));
        gen_and(insts, not_a.plane_pa(i), b.plane_pa(i), scratch, scratch.abs_row(t2_off));
        gen_or(insts, t1_pa, t2_pa, scratch, out.plane_row(i));
    }
    insts.push_back(cud_make_end());
    return insts;
}

// ── Fan-out ADD (Ripple Carry Adder) ─────────────────────────────────────────
//
// 11 independent mode-0 compute groups at fixed mat offsets:
//   kInstGenCmpBase + g * kFAGrpStride  for g = 0 .. 10
//
// Group slot size = 16 (keeps dc-bits 0 and 3 = 0 in every slot base).
// Mode-0 member offsets within a slot: cmp0=+0, cmp1=+1, cmp2=+8, frac=+9.
//
// Slot layout (absolute mat offsets):
//   g=0  kGa    112: AND(a, ~b)
//   g=1  kGb    128: AND(~a, b)
//   g=2  kGor3  144: OR(kGa, kGb) = xab
//   g=3  kGc    160: AND(a, b)
//   g=4  kGd    176: AND(~a, ~b)   [half adder: OR(~a,~b) = ~carry]
//   g=5  kGor6  192: OR(kGc, kGd) = nxab
//   g=6  kGand7 208: AND(xab, ~c)
//   g=7  kGand8 224: AND(nxab, c)
//   g=8  kGor9  240: OR(kGand7, kGand8) = sum
//   g=9  kGe    256: MAJ3(a, b, c)   = carry
//   g=10 kGf    272: MAJ3(~a,~b,~c)  = ~carry
//
// Carry rows (c_off, nc_off) are allocated from scratch after reserving [112,288).
//
// Per-bit instruction counts:
//   Half adder (bit 0): 24 pre-load + 15 exec = 39 insts
//   Full adder (bit i): 44 pre-load + 33 exec = 77 insts
//   Carry-out + END: 3 insts
//   Total: 39 + (W-1)*77 + 3

static constexpr uint32_t kFAGrpStride = 16u;
static constexpr uint32_t kFANumGroups = 11u;
static constexpr uint32_t kFAGroupEnd  = kInstGenCmpBase + kFANumGroups * kFAGrpStride; // 288

// Group indices
static constexpr uint32_t kGa    = 0;   // AND(a, ~b)
static constexpr uint32_t kGb    = 1;   // AND(~a, b)
static constexpr uint32_t kGor3  = 2;   // OR(kGa, kGb) = xab
static constexpr uint32_t kGc    = 3;   // AND(a, b)
static constexpr uint32_t kGd    = 4;   // AND(~a, ~b) / OR(~a,~b) for half adder
static constexpr uint32_t kGor6  = 5;   // OR(kGc, kGd) = nxab
static constexpr uint32_t kGand7 = 6;   // AND(xab, ~c)
static constexpr uint32_t kGand8 = 7;   // AND(nxab, c)
static constexpr uint32_t kGor9  = 8;   // OR(kGand7, kGand8) = sum[i]
static constexpr uint32_t kGe    = 9;   // MAJ3(a, b, c) = carry
static constexpr uint32_t kGf    = 10;  // MAJ3(~a,~b,~c) = ~carry

// Group member indices
static constexpr uint32_t kCmp0 = 0;
static constexpr uint32_t kCmp1 = 1;
static constexpr uint32_t kCmp2 = 2;
static constexpr uint32_t kFrac = 3;

// Physical-address offsets of mode-0 members within a group slot.
static constexpr uint32_t kFAMemberOff[4] = {0u, 1u, 8u, 9u};

// Physical address of member m of group g.
static uint64_t fa_pa(const ScratchAllocator& sc, uint32_t g, uint32_t m) {
    return encode_dram_addr(
        {0, sc.bank,
         sc.abs_row(kInstGenCmpBase + g * kFAGrpStride + kFAMemberOff[m]),
         0});
}

// ── Half adder (bit 0, carry-in = 0) — 39 instructions ───────────────────────
//
// Groups used: kGa(AND a,~b), kGb(AND ~a,b), kGor3(OR→sum),
//              kGc(AND a,b → carry), kGd(OR ~a,~b → ~carry)
static void gen_half_adder(std::vector<CudInst>& v,
                            const ScratchAllocator& sc,
                            uint64_t a0, uint64_t na0,
                            uint64_t b0, uint64_t nb0,
                            uint32_t c_off, uint32_t nc_off,
                            uint32_t sum_row)
{
    const uint64_t zp        = encode_dram_addr({0, sc.bank, sc.abs_row(kZeroRow), 0});
    const uint64_t op        = encode_dram_addr({0, sc.bank, sc.abs_row(kOnesRow), 0});
    const uint64_t c_abs_pa  = encode_dram_addr({0, sc.bank, sc.abs_row(c_off),   0});
    const uint64_t nc_abs_pa = encode_dram_addr({0, sc.bank, sc.abs_row(nc_off),  0});

    // ── Pre-load: 24 insts ────────────────────────────────────────────────────
    // a[0] → kGa.cmp0, kGc.cmp0   (2 dsts)
    v.push_back(cud_make_rowcopy_src(a0));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGa, kCmp0)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGc, kCmp0)));

    // ~a[0] → kGb.cmp0, kGd.cmp0
    v.push_back(cud_make_rowcopy_src(na0));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGb, kCmp0)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGd, kCmp0)));

    // b[0] → kGb.cmp1 (AND ~a,b), kGc.cmp1 (AND a,b)
    v.push_back(cud_make_rowcopy_src(b0));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGb, kCmp1)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGc, kCmp1)));

    // ~b[0] → kGa.cmp1 (AND a,~b), kGd.cmp1 (OR ~a,~b)
    v.push_back(cud_make_rowcopy_src(nb0));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGa, kCmp1)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGd, kCmp1)));

    // zero → AND bias (kGa,kGb,kGc cmp2) + all 5 fracs   (8 dsts)
    v.push_back(cud_make_rowcopy_src(zp));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGa,   kCmp2)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGb,   kCmp2)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGc,   kCmp2)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGa,   kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGb,   kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGc,   kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGd,   kFrac)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGor3, kFrac)));

    // ones → kGd.cmp2 (OR bias), kGor3.cmp2 (OR bias)   (2 dsts)
    v.push_back(cud_make_rowcopy_src(op));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGd,   kCmp2)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGor3, kCmp2)));

    // ── Execute: 15 insts ─────────────────────────────────────────────────────
    v.push_back(cud_make_maj3(fa_pa(sc, kGa, kCmp0), kCmpFracPos, 0u));  // AND(a,~b)
    v.push_back(cud_make_maj3(fa_pa(sc, kGb, kCmp0), kCmpFracPos, 0u));  // AND(~a,b)
    v.push_back(cud_make_maj3(fa_pa(sc, kGc, kCmp0), kCmpFracPos, 0u));  // AND(a,b) = carry
    v.push_back(cud_make_maj3(fa_pa(sc, kGd, kCmp0), kCmpFracPos, 0u));  // OR(~a,~b) = ~carry

    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGc, kFrac)));
    v.push_back(cud_make_rowcopy_dst(c_abs_pa));                          // carry → c_off

    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGd, kFrac)));
    v.push_back(cud_make_rowcopy_dst(nc_abs_pa));                         // ~carry → nc_off

    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGa, kFrac)));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGor3, kCmp0)));           // AND(a,~b) → OR3.cmp0

    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGb, kFrac)));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGor3, kCmp1)));           // AND(~a,b) → OR3.cmp1

    v.push_back(cud_make_maj3(fa_pa(sc, kGor3, kCmp0), kCmpFracPos, 0u)); // OR → sum[0]

    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGor3, kFrac)));
    v.push_back(cud_make_rowcopy_dst(encode_dram_addr({0, sc.bank, sum_row, 0})));
}

// ── Full adder (bits 1..W-1) — 77 instructions ────────────────────────────────
//
// Pre-loads all 11 groups' cmp rows via fan-out chains, then fires MAJ3s in
// dependency order, routing each result directly into downstream groups.
//
// NOTE: c_off/nc_off still hold c[i-1]/~c[i-1] on entry; they are overwritten
// with c[i]/~c[i] in Phase 3.  AND7/AND8.cmp1 receive c[i-1]/~c[i-1] during
// pre-load (before any MAJ3 fires), so the overwrite doesn't affect their inputs.
static void gen_full_adder(std::vector<CudInst>& v,
                            const ScratchAllocator& sc,
                            uint64_t ai, uint64_t nai,
                            uint64_t bi, uint64_t nbi,
                            uint32_t c_off, uint32_t nc_off,
                            uint32_t sum_row)
{
    const uint64_t zp        = encode_dram_addr({0, sc.bank, sc.abs_row(kZeroRow), 0});
    const uint64_t op        = encode_dram_addr({0, sc.bank, sc.abs_row(kOnesRow), 0});
    const uint64_t c_pa      = encode_dram_addr({0, sc.bank, sc.abs_row(c_off),   0});
    const uint64_t nc_pa     = encode_dram_addr({0, sc.bank, sc.abs_row(nc_off),  0});

    // ── Pre-load: 44 insts ────────────────────────────────────────────────────
    // a[i] → kGa.cmp0, kGc.cmp0, kGe.cmp0
    v.push_back(cud_make_rowcopy_src(ai));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGa, kCmp0)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGc, kCmp0)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGe, kCmp0)));

    // ~a[i] → kGb.cmp0, kGd.cmp0, kGf.cmp0
    v.push_back(cud_make_rowcopy_src(nai));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGb, kCmp0)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGd, kCmp0)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGf, kCmp0)));

    // b[i] → kGb.cmp1 (AND ~a,b), kGc.cmp1 (AND a,b), kGe.cmp1 (MAJ3 a,b,c)
    v.push_back(cud_make_rowcopy_src(bi));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGb, kCmp1)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGc, kCmp1)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGe, kCmp1)));

    // ~b[i] → kGa.cmp1 (AND a,~b), kGd.cmp1 (AND ~a,~b), kGf.cmp1 (MAJ3 ~a,~b,~c)
    v.push_back(cud_make_rowcopy_src(nbi));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGa, kCmp1)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGd, kCmp1)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGf, kCmp1)));

    // c[i-1] → kGe.cmp2 (MAJ3 carry), kGand8.cmp1 (AND nxab,c)
    v.push_back(cud_make_rowcopy_src(c_pa));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGe,    kCmp2)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGand8, kCmp1)));

    // ~c[i-1] → kGf.cmp2 (MAJ3 ~carry), kGand7.cmp1 (AND xab,~c)
    v.push_back(cud_make_rowcopy_src(nc_pa));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGf,    kCmp2)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGand7, kCmp1)));

    // zero → AND bias (kGa,kGb,kGc,kGd,kGand7,kGand8 cmp2) + all 11 fracs  (17 dsts)
    v.push_back(cud_make_rowcopy_src(zp));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGa,    kCmp2)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGb,    kCmp2)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGc,    kCmp2)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGd,    kCmp2)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGand7, kCmp2)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGand8, kCmp2)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGa,    kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGb,    kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGc,    kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGd,    kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGe,    kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGf,    kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGor3,  kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGor6,  kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGand7, kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGand8, kFrac)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGor9,  kFrac)));

    // ones → OR bias (kGor3, kGor6, kGor9 cmp2)  (3 dsts)
    v.push_back(cud_make_rowcopy_src(op));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGor3, kCmp2)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGor6, kCmp2)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGor9, kCmp2)));

    // ── Execute: 33 insts ─────────────────────────────────────────────────────
    // Phase 2: fire 6 base ops
    v.push_back(cud_make_maj3(fa_pa(sc, kGa, kCmp0), kCmpFracPos, 0u));  // AND(a,~b)
    v.push_back(cud_make_maj3(fa_pa(sc, kGb, kCmp0), kCmpFracPos, 0u));  // AND(~a,b)
    v.push_back(cud_make_maj3(fa_pa(sc, kGc, kCmp0), kCmpFracPos, 0u));  // AND(a,b)
    v.push_back(cud_make_maj3(fa_pa(sc, kGd, kCmp0), kCmpFracPos, 0u));  // AND(~a,~b)
    v.push_back(cud_make_maj3(fa_pa(sc, kGe, kCmp0), kCmpFracPos, 0u));  // MAJ3(a,b,c)
    v.push_back(cud_make_maj3(fa_pa(sc, kGf, kCmp0), kCmpFracPos, 0u));  // MAJ3(~a,~b,~c)

    // Phase 3: route results to downstream groups; update carry
    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGa, kFrac)));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGor3, kCmp0)));   // AND(a,~b) → OR3.cmp0

    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGb, kFrac)));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGor3, kCmp1)));   // AND(~a,b) → OR3.cmp1

    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGc, kFrac)));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGor6, kCmp0)));   // AND(a,b) → OR6.cmp0

    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGd, kFrac)));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGor6, kCmp1)));   // AND(~a,~b) → OR6.cmp1

    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGe, kFrac)));
    v.push_back(cud_make_rowcopy_dst(c_pa));                       // carry → c_off

    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGf, kFrac)));
    v.push_back(cud_make_rowcopy_dst(nc_pa));                      // ~carry → nc_off

    // Phase 4: XOR(a,b) and XNOR(a,b); route to AND groups
    v.push_back(cud_make_maj3(fa_pa(sc, kGor3, kCmp0), kCmpFracPos, 0u));  // OR → xab
    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGor3, kFrac)));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGand7, kCmp0)));   // xab → AND7.cmp0

    v.push_back(cud_make_maj3(fa_pa(sc, kGor6, kCmp0), kCmpFracPos, 0u));  // OR → nxab
    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGor6, kFrac)));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGand8, kCmp0)));   // nxab → AND8.cmp0

    // Phase 5: AND(xab,~c) and AND(nxab,c); route to OR9
    v.push_back(cud_make_maj3(fa_pa(sc, kGand7, kCmp0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGand7, kFrac)));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGor9, kCmp0)));

    v.push_back(cud_make_maj3(fa_pa(sc, kGand8, kCmp0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGand8, kFrac)));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGor9, kCmp1)));

    // Phase 6: sum[i]
    v.push_back(cud_make_maj3(fa_pa(sc, kGor9, kCmp0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGor9, kFrac)));
    v.push_back(cud_make_rowcopy_dst(encode_dram_addr({0, sc.bank, sum_row, 0})));
}

// ── ADD (public) ──────────────────────────────────────────────────────────────

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

    // Reserve the group area [kInstGenCmpBase, kFAGroupEnd) from the scratch
    // allocator so no other alloc lands in the fixed group rows (112-281).
    if (scratch.next < kFAGroupEnd) scratch.next = kFAGroupEnd;

    const uint32_t c_off  = scratch.alloc(1);  // carry   c[i], persists across bits
    const uint32_t nc_off = scratch.alloc(1);  // ~carry ~c[i]

    std::vector<CudInst> insts;

    // ── Bit 0: half adder ─────────────────────────────────────────────────────
    gen_half_adder(insts, scratch,
                   a.plane_pa(0), not_a.plane_pa(0),
                   b.plane_pa(0), not_b.plane_pa(0),
                   c_off, nc_off, out.plane_row(0));

    // ── Bits 1..W-1: full adder ───────────────────────────────────────────────
    for (uint8_t i = 1; i < W; ++i) {
        gen_full_adder(insts, scratch,
                       a.plane_pa(i), not_a.plane_pa(i),
                       b.plane_pa(i), not_b.plane_pa(i),
                       c_off, nc_off, out.plane_row(i));
    }

    // ── Carry out (bit W): c[W-1] already in c_off ───────────────────────────
    const uint64_t c_pa         = encode_dram_addr({0, scratch.bank, scratch.abs_row(c_off), 0});
    const uint64_t out_carry_pa = encode_dram_addr({0, out.bank, out.plane_row(W), 0});
    insts.push_back(cud_make_rowcopy_src(c_pa));
    insts.push_back(cud_make_rowcopy_dst(out_carry_pa));

    insts.push_back(cud_make_end());
    return insts;
}
