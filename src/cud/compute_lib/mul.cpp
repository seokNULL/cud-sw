#include "../../../include/cud/compute_lib/inst_gen.h"
#include "../../../include/cud/compute_lib/compute_rows.h"
#include "../../../include/cxl/address_map.h"
#include "../cud_inst_helpers.h"
#include "fa6.h"

#include <cassert>
#include <vector>

// Wire, alloc_wire, zero_wire, abs_pa, gen_fa6, kMGroupEnd: shared with
// gemv.cpp, defined in fa6.h/fa6.cpp.

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

// ── gen_mul: W × W → 2W multiplication (Wallace tree + CPA) ─────────────────
// Uses the shared gen_fa6 (fa6.h) for both the Wallace-tree reduction and the
// final carry-propagate addition.

std::vector<CudInst> gen_mul(
    const BitSerialLayout& a, const BitSerialLayout& not_a,
    const BitSerialLayout& b, const BitSerialLayout& not_b,
    const BitSerialLayout& out,
    uint8_t W,
    ScratchAllocator& scratch,
    const BitSerialLayout* not_out)
{
    assert(W >= 1 && W <= 8);
    assert(a.bit_width == W && b.bit_width == W);
    assert(a.bank == b.bank && a.bank == out.bank && a.bank == scratch.bank);

    // W=1: result is AND(a,b), fits in 1 bit — skip the full multiplier entirely.
    if (W == 1) {
        assert(out.bit_width >= 1);
        std::vector<CudInst> insts;
        pp_and(insts, scratch, a.plane_pa(0), b.plane_pa(0), out.plane_row(0));
        if (not_out) {
            assert(not_out->bit_width >= 1);
            pp_or(insts, scratch, not_a.plane_pa(0), not_b.plane_pa(0), not_out->plane_row(0));
        }
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

    auto is_zw = [&](const Wire& w) { return w.row == zw.row; };

    // Single carry wire reused across all CPA bits (in-place updated by gen_fa6).
    Wire c_wire = alloc_wire(scratch);
    const uint64_t cwp  = abs_pa(scratch.bank, c_wire.row);
    const uint64_t ncwp = abs_pa(scratch.bank, c_wire.nrow);

    // Write 0/1 into c_wire to propagate carry = 0 after a trivial FA step (4 insts).
    auto zero_c = [&]() {
        insts.push_back(cud_make_rowcopy_src(abs_pa(scratch.bank, zw.row)));
        insts.push_back(cud_make_rowcopy_dst(cwp));
        insts.push_back(cud_make_rowcopy_src(abs_pa(scratch.bank, zw.nrow)));
        insts.push_back(cud_make_rowcopy_dst(ncwp));
    };

    auto emit_out = [&](uint32_t src_row, uint32_t src_nrow, uint32_t bit) {
        insts.push_back(cud_make_rowcopy_src(abs_pa(scratch.bank, src_row)));
        insts.push_back(cud_make_rowcopy_dst(
            encode_dram_addr({0, out.bank, out.plane_row(bit), 0})));
        if (not_out) {
            insts.push_back(cud_make_rowcopy_src(abs_pa(scratch.bank, src_nrow)));
            insts.push_back(cud_make_rowcopy_dst(
                encode_dram_addr({0, not_out->bank, not_out->plane_row(bit), 0})));
        }
    };

    // Bit 0: cin = zero.  Trivial when wA or wB is zero: sum = the other, carry = 0.
    {
        const Wire wA = col_wire(0, 0);
        const Wire wB = col_wire(0, 1);
        if (is_zw(wA) && is_zw(wB)) {
            emit_out(zw.row, zw.nrow, 0);
            zero_c();
        } else if (is_zw(wB)) {
            emit_out(wA.row, wA.nrow, 0);
            zero_c();
        } else if (is_zw(wA)) {
            emit_out(wB.row, wB.nrow, 0);
            zero_c();
        } else {
            Wire s = alloc_wire(scratch);
            gen_fa6(insts, scratch, wA, wB, zw, c_wire, s);
            emit_out(s.row, s.nrow, 0);
        }
    }

    // Bits 1..N-1: cin = c_wire.  Trivial when both columns are zero: sum = cin, carry = 0.
    for (uint32_t k = 1; k < N; ++k) {
        const Wire wA   = col_wire(k, 0);
        const Wire wB   = col_wire(k, 1);
        const bool last = (k == N - 1);

        if (is_zw(wA) && is_zw(wB)) {
            emit_out(c_wire.row, c_wire.nrow, k);
            if (!last) zero_c();
        } else {
            Wire s = alloc_wire(scratch);
            gen_fa6(insts, scratch, wA, wB, c_wire, c_wire, s);
            emit_out(s.row, s.nrow, k);
        }
    }

    insts.push_back(cud_make_end());
    return insts;
}
