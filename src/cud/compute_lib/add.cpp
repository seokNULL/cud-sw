#include "../../../include/cud/compute_lib/inst_gen.h"
#include "../../../include/cud/compute_lib/compute_rows.h"
#include "../../../include/cxl/address_map.h"
#include "../cud_inst_helpers.h"

#include <cassert>
#include <vector>

// ROWCOPY_DST without LAST bit: intermediate destination in a fan-out chain.
static inline CudInst cud_make_rowcopy_dst_cont(uint64_t pa) {
    return CUD_FIELD_OPCODE(CUD_OP_ROWCOPY_DST) | cud_addr_fields(pa);
}

// ── ADD (Ripple Carry Adder via MAJ3 decomposition) ──────────────────────────
//
// 4 independent mode-0 compute groups at fixed mat offsets:
//   kInstGenCmpBase + g * kFAGrpStride  for g = 0 .. 3
//
// Group slot size = 16 (keeps dc-bits 0 and 3 = 0 in every slot base).
// Mode-0 member offsets within a slot: cmp0=+0, cmp1=+1, cmp2=+8, frac=+9.
//
// Slot layout (absolute mat offsets):
//   g=0  kGe    112: MAJ3(a, b, cin)              = carry
//   g=1  kGf    128: MAJ3(~a, ~b, ~cin)            = ~carry
//   g=2  kGis   144: MAJ3(b, cin, ~carry)           = inner_sum
//   g=3  kGsum  160: MAJ3(a, inner_sum, ~carry)     = sum
//
// sum = MAJ3(a, MAJ3(b, cin, ~carry), ~carry)   [MAJ5 decomposition of XOR3]
// carry = MAJ3(a, b, cin)
//
// Per-step instruction counts:
//   Pre-load: 20 insts   Execute: 14 insts   Total per bit: 34 insts
//   Carry-out + END: 3 insts
//   Total: 34*W + 3  (W=1: 37  W=4: 139  W=8: 275)

static constexpr uint32_t kFAGrpStride = 16u;
static constexpr uint32_t kFANumGroups = 4u;
static constexpr uint32_t kFAGroupEnd  = kInstGenCmpBase + kFANumGroups * kFAGrpStride; // 176

// Group indices
static constexpr uint32_t kGe   = 0;  // carry      = MAJ3(a, b, cin)
static constexpr uint32_t kGf   = 1;  // ~carry     = MAJ3(~a, ~b, ~cin)
static constexpr uint32_t kGis  = 2;  // inner_sum  = MAJ3(b, cin, ~carry)
static constexpr uint32_t kGsum = 3;  // sum        = MAJ3(a, inner_sum, ~carry)

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

// ── Single adder step (half or full adder) — 34 instructions ─────────────────
//
// cin_pa:  PA of carry-in  row (kZeroRow PA for bit 0; scratch c_off for bits 1+)
// ncin_pa: PA of ~carry-in row (kOnesRow PA for bit 0; scratch nc_off for bits 1+)
// c_off, nc_off: scratch offsets written with carry/~carry for the next step
// sum_row: absolute row address to receive the sum output
static void gen_adder_step(std::vector<CudInst>& v,
                            const ScratchAllocator& sc,
                            uint64_t a_pa, uint64_t na_pa,
                            uint64_t b_pa, uint64_t nb_pa,
                            uint64_t cin_pa, uint64_t ncin_pa,
                            uint32_t c_off, uint32_t nc_off,
                            uint32_t sum_row)
{
    const uint64_t zp        = encode_dram_addr({0, sc.bank, sc.abs_row(kZeroRow), 0});
    const uint64_t c_abs_pa  = encode_dram_addr({0, sc.bank, sc.abs_row(c_off),   0});
    const uint64_t nc_abs_pa = encode_dram_addr({0, sc.bank, sc.abs_row(nc_off),  0});
    const uint64_t sum_pa    = encode_dram_addr({0, sc.bank, sum_row,              0});

    // ── Pre-load: 20 insts ────────────────────────────────────────────────────
    // a → kGe.cmp0, kGsum.cmp0
    v.push_back(cud_make_rowcopy_src(a_pa));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGe,   kCmp0)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGsum, kCmp0)));

    // ~a → kGf.cmp0
    v.push_back(cud_make_rowcopy_src(na_pa));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGf, kCmp0)));

    // b → kGe.cmp1, kGis.cmp0
    v.push_back(cud_make_rowcopy_src(b_pa));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGe,  kCmp1)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGis, kCmp0)));

    // ~b → kGf.cmp1
    v.push_back(cud_make_rowcopy_src(nb_pa));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGf, kCmp1)));

    // cin → kGe.cmp2, kGis.cmp1
    v.push_back(cud_make_rowcopy_src(cin_pa));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGe,  kCmp2)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGis, kCmp1)));

    // ~cin → kGf.cmp2
    v.push_back(cud_make_rowcopy_src(ncin_pa));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGf, kCmp2)));

    // zeros → kGe.frac, kGf.frac, kGis.frac, kGsum.frac  (output slots cleared)
    v.push_back(cud_make_rowcopy_src(zp));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGe,   kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGf,   kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGis,  kFrac)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGsum, kFrac)));

    // ── Execute: 14 insts ─────────────────────────────────────────────────────
    // carry = MAJ3(a, b, cin)
    v.push_back(cud_make_maj3(fa_pa(sc, kGe, kCmp0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGe, kFrac)));
    v.push_back(cud_make_rowcopy_dst(c_abs_pa));

    // ~carry = MAJ3(~a, ~b, ~cin); fan-out → nc_off, kGis.cmp2, kGsum.cmp2
    v.push_back(cud_make_maj3(fa_pa(sc, kGf, kCmp0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGf, kFrac)));
    v.push_back(cud_make_rowcopy_dst_cont(nc_abs_pa));
    v.push_back(cud_make_rowcopy_dst_cont(fa_pa(sc, kGis,  kCmp2)));
    v.push_back(cud_make_rowcopy_dst(     fa_pa(sc, kGsum, kCmp2)));

    // inner_sum = MAJ3(b, cin, ~carry)
    v.push_back(cud_make_maj3(fa_pa(sc, kGis, kCmp0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGis, kFrac)));
    v.push_back(cud_make_rowcopy_dst(fa_pa(sc, kGsum, kCmp1)));

    // sum = MAJ3(a, inner_sum, ~carry)
    v.push_back(cud_make_maj3(fa_pa(sc, kGsum, kCmp0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(fa_pa(sc, kGsum, kFrac)));
    v.push_back(cud_make_rowcopy_dst(sum_pa));
}

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

    // Reserve [kInstGenCmpBase, kFAGroupEnd) so no scratch alloc lands in the
    // fixed group rows.
    if (scratch.next < kFAGroupEnd) scratch.next = kFAGroupEnd;

    const uint32_t c_off  = scratch.alloc(1);  // carry   c[i], persists across bits
    const uint32_t nc_off = scratch.alloc(1);  // ~carry ~c[i]

    const uint64_t zp    = encode_dram_addr({0, scratch.bank, scratch.abs_row(kZeroRow), 0});
    const uint64_t op    = encode_dram_addr({0, scratch.bank, scratch.abs_row(kOnesRow), 0});
    const uint64_t c_pa  = encode_dram_addr({0, scratch.bank, scratch.abs_row(c_off),   0});
    const uint64_t nc_pa = encode_dram_addr({0, scratch.bank, scratch.abs_row(nc_off),  0});

    std::vector<CudInst> insts;

    // Bit 0: half adder (cin = 0, ~cin = 1)
    gen_adder_step(insts, scratch,
                   a.plane_pa(0), not_a.plane_pa(0),
                   b.plane_pa(0), not_b.plane_pa(0),
                   zp, op, c_off, nc_off, out.plane_row(0));

    // Bits 1..W-1: full adder
    for (uint8_t i = 1; i < W; ++i) {
        gen_adder_step(insts, scratch,
                       a.plane_pa(i), not_a.plane_pa(i),
                       b.plane_pa(i), not_b.plane_pa(i),
                       c_pa, nc_pa, c_off, nc_off, out.plane_row(i));
    }

    // Carry out (bit W): c[W-1] already in c_off
    insts.push_back(cud_make_rowcopy_src(c_pa));
    insts.push_back(cud_make_rowcopy_dst(encode_dram_addr({0, out.bank, out.plane_row(W), 0})));

    insts.push_back(cud_make_end());
    return insts;
}
