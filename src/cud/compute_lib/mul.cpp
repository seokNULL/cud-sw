#include "../../../include/cud/compute_lib/inst_gen.h"
#include "../../../include/cud/compute_lib/compute_rows.h"
#include "../../../include/cxl/address_map.h"
#include "../cud_inst_helpers.h"

#include <cassert>
#include <vector>

// ── Internal helpers ──────────────────────────────────────────────────────────

// ROWCOPY_DST without LAST bit.
static inline CudInst rowcopy_dst_cont(uint64_t p) {
    return CUD_FIELD_OPCODE(CUD_OP_ROWCOPY_DST) | cud_addr_fields(p);
}

// Bit-plane wire: absolute DRAM rows for a value and its complement.
struct Wire { uint32_t row, nrow; };

static inline uint64_t abs_pa(uint32_t bank, uint32_t abs_row) {
    return encode_dram_addr({0, bank, abs_row, 0});
}

// Allocate two consecutive scratch rows and return as a Wire.
static Wire alloc_wire(ScratchAllocator& sc) {
    const uint32_t r  = sc.alloc(1);
    const uint32_t nr = sc.alloc(1);
    return {sc.abs_row(r), sc.abs_row(nr)};
}

// Wire representing the constant 0 / 1 (pre-initialised constant rows).
static Wire zero_wire(const ScratchAllocator& sc) {
    return {sc.abs_row(kZeroRow), sc.abs_row(kOnesRow)};
}

// ── Partial product generation ────────────────────────────────────────────────
// Uses the single mode-0 group at kInstGenCmpBase (same as xor.cpp / add.cpp).

static void pp_and(std::vector<CudInst>& v, const ScratchAllocator& sc,
                   uint64_t x_pa, uint64_t y_pa, uint32_t dst_abs_row) {
    const auto   cmp = cmp_group_rows(0, kInstGenCmpBase);
    const uint64_t c0 = abs_pa(sc.bank, sc.abs_row(cmp[0]));
    const uint64_t c1 = abs_pa(sc.bank, sc.abs_row(cmp[1]));
    const uint64_t c2 = abs_pa(sc.bank, sc.abs_row(cmp[2]));
    const uint64_t cf = abs_pa(sc.bank, sc.abs_row(cmp[3]));
    const uint64_t zp = abs_pa(sc.bank, sc.abs_row(kZeroRow));
    const uint64_t dp = abs_pa(sc.bank, dst_abs_row);
    v.push_back(cud_make_rowcopy_src(x_pa));  v.push_back(cud_make_rowcopy_dst(c0));
    v.push_back(cud_make_rowcopy_src(y_pa));  v.push_back(cud_make_rowcopy_dst(c1));
    v.push_back(cud_make_rowcopy_src(zp));    v.push_back(cud_make_rowcopy_dst(c2));
    v.push_back(cud_make_rowcopy_src(zp));    v.push_back(cud_make_rowcopy_dst(cf));
    v.push_back(cud_make_maj3(c0, kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(cf));    v.push_back(cud_make_rowcopy_dst(dp));
}

static void pp_or(std::vector<CudInst>& v, const ScratchAllocator& sc,
                  uint64_t x_pa, uint64_t y_pa, uint32_t dst_abs_row) {
    const auto   cmp = cmp_group_rows(0, kInstGenCmpBase);
    const uint64_t c0 = abs_pa(sc.bank, sc.abs_row(cmp[0]));
    const uint64_t c1 = abs_pa(sc.bank, sc.abs_row(cmp[1]));
    const uint64_t c2 = abs_pa(sc.bank, sc.abs_row(cmp[2]));
    const uint64_t cf = abs_pa(sc.bank, sc.abs_row(cmp[3]));
    const uint64_t op = abs_pa(sc.bank, sc.abs_row(kOnesRow));
    const uint64_t zp = abs_pa(sc.bank, sc.abs_row(kZeroRow));
    const uint64_t dp = abs_pa(sc.bank, dst_abs_row);
    v.push_back(cud_make_rowcopy_src(x_pa));  v.push_back(cud_make_rowcopy_dst(c0));
    v.push_back(cud_make_rowcopy_src(y_pa));  v.push_back(cud_make_rowcopy_dst(c1));
    v.push_back(cud_make_rowcopy_src(op));    v.push_back(cud_make_rowcopy_dst(c2));
    v.push_back(cud_make_rowcopy_src(zp));    v.push_back(cud_make_rowcopy_dst(cf));
    v.push_back(cud_make_maj3(c0, kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(cf));    v.push_back(cud_make_rowcopy_dst(dp));
}

// Computes p = AND(a,b) and np = OR(~a,~b) [= ~AND(a,b)].  22 insts.
static Wire gen_partial_product(std::vector<CudInst>& v, ScratchAllocator& sc,
                                 uint64_t a_pa, uint64_t na_pa,
                                 uint64_t b_pa, uint64_t nb_pa) {
    Wire w = alloc_wire(sc);
    pp_and(v, sc, a_pa,  b_pa,  w.row);   //  p = AND(a,b)
    pp_or (v, sc, na_pa, nb_pa, w.nrow);  // np = OR(~a,~b) = ~AND(a,b)
    return w;
}

// ── 6-group Full Adder — 47 instructions ─────────────────────────────────────
//
// Produces sum = XOR(a,b,cin) and carry = MAJ3(a,b,cin), plus their complements.
//
// Mode-0 compute groups (stride=16 from kInstGenCmpBase=112):
//   g=0 kMGe    112  carry      = MAJ3(a, b, cin)
//   g=1 kMGf    128  ~carry     = MAJ3(~a, ~b, ~cin)
//   g=2 kMGis   144  inner_sum  = MAJ3(b, cin, ~carry)
//   g=3 kMGsum  160  sum        = MAJ3(a, inner_sum, ~carry)
//   g=4 kMGnis  176  ~inner_sum = MAJ3(~b, ~cin, carry)
//   g=5 kMGnsum 192  ~sum       = MAJ3(~a, ~inner_sum, carry)
//
// Caller pre-allocates s_wire and c_wire (c_wire may equal cin for RCA).
// Pre-load: 25 insts.  Execute: 22 insts.  Total: 47 insts.

static constexpr uint32_t kMGrpStride = 16u;
static constexpr uint32_t kMNumGroups = 6u;
static constexpr uint32_t kMGroupEnd  = kInstGenCmpBase + kMNumGroups * kMGrpStride; // 208

static constexpr uint32_t kMGe    = 0;
static constexpr uint32_t kMGf    = 1;
static constexpr uint32_t kMGis   = 2;
static constexpr uint32_t kMGsum  = 3;
static constexpr uint32_t kMGnis  = 4;
static constexpr uint32_t kMGnsum = 5;

// Mode-0 member offsets: cmp0=+0, cmp1=+1, cmp2=+8, frac=+9.
static constexpr uint32_t kMMOff[4] = {0u, 1u, 8u, 9u};

static uint64_t mg(const ScratchAllocator& sc, uint32_t g, uint32_t m) {
    return abs_pa(sc.bank, sc.abs_row(kInstGenCmpBase + g * kMGrpStride + kMMOff[m]));
}

static void gen_fa6(std::vector<CudInst>& v, const ScratchAllocator& sc,
                    Wire a, Wire b, Wire cin, Wire c_wire, Wire s_wire) {
    const uint64_t zp  = abs_pa(sc.bank, sc.abs_row(kZeroRow));
    const uint64_t ap  = abs_pa(sc.bank, a.row);
    const uint64_t nap = abs_pa(sc.bank, a.nrow);
    const uint64_t bp  = abs_pa(sc.bank, b.row);
    const uint64_t nbp = abs_pa(sc.bank, b.nrow);
    const uint64_t cp  = abs_pa(sc.bank, cin.row);
    const uint64_t ncp = abs_pa(sc.bank, cin.nrow);
    const uint64_t cwp = abs_pa(sc.bank, c_wire.row);
    const uint64_t ncwp= abs_pa(sc.bank, c_wire.nrow);
    const uint64_t sp  = abs_pa(sc.bank, s_wire.row);
    const uint64_t nsp = abs_pa(sc.bank, s_wire.nrow);

    // ── Pre-load: 25 insts ────────────────────────────────────────────────────
    // a → kGe.cmp0, kGsum.cmp0
    v.push_back(cud_make_rowcopy_src(ap));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGe,    0)));
    v.push_back(cud_make_rowcopy_dst(mg(sc, kMGsum, 0)));

    // ~a → kGf.cmp0, kGnsum.cmp0
    v.push_back(cud_make_rowcopy_src(nap));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGf,    0)));
    v.push_back(cud_make_rowcopy_dst(mg(sc, kMGnsum, 0)));

    // b → kGe.cmp1, kGis.cmp0
    v.push_back(cud_make_rowcopy_src(bp));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGe,  1)));
    v.push_back(cud_make_rowcopy_dst(mg(sc, kMGis, 0)));

    // ~b → kGf.cmp1, kGnis.cmp0
    v.push_back(cud_make_rowcopy_src(nbp));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGf,   1)));
    v.push_back(cud_make_rowcopy_dst(mg(sc, kMGnis, 0)));

    // cin → kGe.cmp2, kGis.cmp1
    v.push_back(cud_make_rowcopy_src(cp));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGe,  2)));
    v.push_back(cud_make_rowcopy_dst(mg(sc, kMGis, 1)));

    // ~cin → kGf.cmp2, kGnis.cmp1
    v.push_back(cud_make_rowcopy_src(ncp));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGf,   2)));
    v.push_back(cud_make_rowcopy_dst(mg(sc, kMGnis, 1)));

    // zeros → all 6 frac rows (output slots cleared)
    v.push_back(cud_make_rowcopy_src(zp));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGe,    3)));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGf,    3)));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGis,   3)));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGsum,  3)));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGnis,  3)));
    v.push_back(cud_make_rowcopy_dst(mg(sc, kMGnsum, 3)));

    // ── Execute: 22 insts ─────────────────────────────────────────────────────
    // carry = MAJ3(a,b,cin); fan-out → c_wire, kGnis.cmp2, kGnsum.cmp2
    v.push_back(cud_make_maj3(mg(sc, kMGe, 0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(mg(sc, kMGe, 3)));
    v.push_back(rowcopy_dst_cont(cwp));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGnis,  2)));
    v.push_back(cud_make_rowcopy_dst(mg(sc, kMGnsum, 2)));

    // ~carry = MAJ3(~a,~b,~cin); fan-out → nc_wire, kGis.cmp2, kGsum.cmp2
    v.push_back(cud_make_maj3(mg(sc, kMGf, 0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(mg(sc, kMGf, 3)));
    v.push_back(rowcopy_dst_cont(ncwp));
    v.push_back(rowcopy_dst_cont(mg(sc, kMGis,  2)));
    v.push_back(cud_make_rowcopy_dst(mg(sc, kMGsum, 2)));

    // inner_sum = MAJ3(b, cin, ~carry) → kGsum.cmp1
    v.push_back(cud_make_maj3(mg(sc, kMGis, 0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(mg(sc, kMGis, 3)));
    v.push_back(cud_make_rowcopy_dst(mg(sc, kMGsum, 1)));

    // ~inner_sum = MAJ3(~b, ~cin, carry) → kGnsum.cmp1
    v.push_back(cud_make_maj3(mg(sc, kMGnis, 0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(mg(sc, kMGnis, 3)));
    v.push_back(cud_make_rowcopy_dst(mg(sc, kMGnsum, 1)));

    // sum = MAJ3(a, inner_sum, ~carry) → s_wire.row
    v.push_back(cud_make_maj3(mg(sc, kMGsum, 0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(mg(sc, kMGsum, 3)));
    v.push_back(cud_make_rowcopy_dst(sp));

    // ~sum = MAJ3(~a, ~inner_sum, carry) → s_wire.nrow
    v.push_back(cud_make_maj3(mg(sc, kMGnsum, 0), kCmpFracPos, 0u));
    v.push_back(cud_make_rowcopy_src(mg(sc, kMGnsum, 3)));
    v.push_back(cud_make_rowcopy_dst(nsp));
}

// ── gen_mul: W × W → 2W multiplication (Wallace tree + CPA) ─────────────────

std::vector<CudInst> gen_mul(
    const BitSerialLayout& a, const BitSerialLayout& not_a,
    const BitSerialLayout& b, const BitSerialLayout& not_b,
    const BitSerialLayout& out,
    uint8_t W,
    ScratchAllocator& scratch)
{
    assert(W >= 1 && W <= 8);
    assert(a.bit_width == W && b.bit_width == W);
    assert(a.bank == b.bank && a.bank == out.bank && a.bank == scratch.bank);

    // W=1: result is AND(a,b), fits in 1 bit — skip the full multiplier entirely.
    if (W == 1) {
        assert(out.bit_width >= 1);
        std::vector<CudInst> insts;
        pp_and(insts, scratch, a.plane_pa(0), b.plane_pa(0), out.plane_row(0));
        insts.push_back(cud_make_end());
        return insts;
    }

    assert(out.bit_width == 2u * W);

    // Reserve [kInstGenCmpBase, kMGroupEnd) for the 6-group FA area.
    if (scratch.next < kMGroupEnd) scratch.next = kMGroupEnd;

    std::vector<CudInst> insts;
    const uint32_t N = 2u * W;  // output bit width

    // ── Step 1: Partial products ──────────────────────────────────────────────
    // pp[i*W+j] = Wire for a[i] AND b[j].  W² × 22 insts.
    std::vector<Wire> pp(static_cast<size_t>(W) * W);
    for (uint32_t i = 0; i < W; ++i)
        for (uint32_t j = 0; j < W; ++j)
            pp[i * W + j] = gen_partial_product(
                insts, scratch,
                a.plane_pa(i), not_a.plane_pa(i),
                b.plane_pa(j), not_b.plane_pa(j));

    // ── Step 2: Column lists ──────────────────────────────────────────────────
    std::vector<std::vector<Wire>> cols(N);
    for (uint32_t i = 0; i < W; ++i)
        for (uint32_t j = 0; j < W; ++j)
            cols[i + j].push_back(pp[i * W + j]);

    // ── Step 3: Wallace tree reduction ────────────────────────────────────────
    // Greedily reduce left-to-right until every column has ≤ 2 wires.
    // Carries from col k go to col k+1 in the same pass (correct because
    // instructions are sequential: FA_k's execute precedes FA_{k+1}'s preload).
    bool changed = true;
    while (changed) {
        changed = false;
        for (uint32_t k = 0; k < N; ++k) {
            while (cols[k].size() >= 3) {
                changed = true;
                const Wire wa  = cols[k][cols[k].size() - 3];
                const Wire wb  = cols[k][cols[k].size() - 2];
                const Wire wci = cols[k][cols[k].size() - 1];
                cols[k].resize(cols[k].size() - 3);

                Wire c_wire = alloc_wire(scratch);
                Wire s_wire = alloc_wire(scratch);
                gen_fa6(insts, scratch, wa, wb, wci, c_wire, s_wire);

                cols[k].push_back(s_wire);
                if (k + 1 < N) cols[k + 1].push_back(c_wire);
                // carry beyond bit N-1 is discarded (W×W fits in 2W bits)
            }
        }
    }

    // ── Step 4: Final carry-propagate addition ────────────────────────────────
    // Each column now has 0, 1, or 2 wires.  Add the two implied rows with RCA.
    const Wire zw = zero_wire(scratch);

    auto col_wire = [&](uint32_t k, uint32_t idx) -> Wire {
        return (idx < cols[k].size()) ? cols[k][idx] : zw;
    };

    // Single carry wire reused across all CPA bits.
    // Bit 0: cin = zero (HA); bits 1+: cin = carry from previous step.
    Wire c_wire = alloc_wire(scratch);

    // Bit 0: half adder (cin = zero)
    {
        Wire s = alloc_wire(scratch);
        gen_fa6(insts, scratch, col_wire(0, 0), col_wire(0, 1), zw, c_wire, s);
        insts.push_back(cud_make_rowcopy_src(abs_pa(scratch.bank, s.row)));
        insts.push_back(cud_make_rowcopy_dst(
            encode_dram_addr({0, out.bank, out.plane_row(0), 0})));
    }

    // Bits 1..N-1: full adder (cin = c_wire from previous bit; also writes to c_wire)
    for (uint32_t k = 1; k < N; ++k) {
        Wire s = alloc_wire(scratch);
        gen_fa6(insts, scratch, col_wire(k, 0), col_wire(k, 1), c_wire, c_wire, s);
        insts.push_back(cud_make_rowcopy_src(abs_pa(scratch.bank, s.row)));
        insts.push_back(cud_make_rowcopy_dst(
            encode_dram_addr({0, out.bank, out.plane_row(k), 0})));
    }

    insts.push_back(cud_make_end());
    return insts;
}
