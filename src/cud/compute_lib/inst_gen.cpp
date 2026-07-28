#include "../../../include/cud/compute_lib/inst_gen.h"
#include "../../../include/cud/compute_lib/compute_rows.h"
#include "../../../include/cud/compute_lib/add_table.h"
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

// ── ADD ───────────────────────────────────────────────────────────────────────

std::vector<CudInst> gen_add(
    const BitSerialLayout& a, const BitSerialLayout& not_a,
    const BitSerialLayout& b, const BitSerialLayout& not_b,
    const BitSerialLayout& out,
    uint8_t W,
    ScratchAllocator& scratch)
{
    assert(W >= 1 && W <= 4);
    assert(a.bit_width == W && b.bit_width == W && out.bit_width == W + 1u);
    assert(a.bank == b.bank && a.bank == out.bank);

    const AddBitExpr* sop = kAdderSop[W - 1];

    // Two scratch rows reused across all output bits
    const uint32_t acc_off = scratch.alloc(1);  // running OR accumulator
    const uint32_t tmp_off = scratch.alloc(1);  // AND chain temp

    const uint64_t acc_pa = encode_dram_addr({0, scratch.bank, scratch.abs_row(acc_off), 0});
    const uint64_t tmp_pa = encode_dram_addr({0, scratch.bank, scratch.abs_row(tmp_off), 0});
    const uint64_t zpa    = encode_dram_addr({0, scratch.bank, scratch.abs_row(kZeroRow), 0});

    // Resolve a literal to its physical row address
    auto get_lit_pa = [&](const AddLit& l) -> uint64_t {
        bool    is_b = l.var >= W;
        uint8_t bit  = is_b ? l.var - W : l.var;
        if (l.neg) return is_b ? not_b.plane_pa(bit) : not_a.plane_pa(bit);
        else       return is_b ? b.plane_pa(bit)     : a.plane_pa(bit);
    };

    std::vector<CudInst> insts;

    for (uint8_t k = 0; k <= W; ++k) {
        const AddBitExpr& expr = sop[k];
        const uint64_t out_pa = encode_dram_addr({0, out.bank, out.plane_row(k), 0});

        // acc = 0
        insts.push_back(cud_make_rowcopy_src(zpa));
        insts.push_back(cud_make_rowcopy_dst(acc_pa));

        for (uint8_t ti = 0; ti < expr.n; ++ti) {
            const AddTerm& term = expr.terms[ti];
            if (term.n == 0) continue;

            // Build AND chain of all literals into tmp
            if (term.n == 1) {
                insts.push_back(cud_make_rowcopy_src(get_lit_pa(term.lits[0])));
                insts.push_back(cud_make_rowcopy_dst(tmp_pa));
            } else {
                gen_and(insts, get_lit_pa(term.lits[0]), get_lit_pa(term.lits[1]),
                        scratch, scratch.abs_row(tmp_off));
                for (uint8_t li = 2; li < term.n; ++li)
                    gen_and(insts, tmp_pa, get_lit_pa(term.lits[li]),
                            scratch, scratch.abs_row(tmp_off));
            }

            // acc = OR(acc, tmp)
            gen_or(insts, acc_pa, tmp_pa, scratch, scratch.abs_row(acc_off));
        }

        // Write accumulated result to output bit-plane k
        insts.push_back(cud_make_rowcopy_src(acc_pa));
        insts.push_back(cud_make_rowcopy_dst(out_pa));
    }

    insts.push_back(cud_make_end());
    return insts;
}
