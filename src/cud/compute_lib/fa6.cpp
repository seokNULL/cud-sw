#include "fa6.h"
#include "../../../include/cud/compute_lib/compute_rows.h"
#include "../../../include/cxl/address_map.h"
#include "../cud_inst_helpers.h"

// ROWCOPY_DST without LAST bit: intermediate destination in a fan-out chain.
static inline CudInst rowcopy_dst_cont(uint64_t p) {
    return CUD_FIELD_OPCODE(CUD_OP_ROWCOPY_DST) | cud_addr_fields(p);
}

uint64_t abs_pa(uint32_t bank, uint32_t abs_row) {
    return encode_dram_addr({0, bank, abs_row, 0});
}

Wire alloc_wire(ScratchAllocator& sc) {
    const uint32_t r  = sc.alloc(1);
    const uint32_t nr = sc.alloc(1);
    return {sc.abs_row(r), sc.abs_row(nr)};
}

Wire zero_wire(const ScratchAllocator& sc) {
    return {sc.abs_row(kZeroRow), sc.abs_row(kOnesRow)};
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

void gen_fa6(std::vector<CudInst>& v, const ScratchAllocator& sc,
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
