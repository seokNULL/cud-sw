#include "../../../include/cud/compute_lib/inst_gen.h"
#include "../../../include/cud/compute_lib/scratch.h"
#include "../cud_inst_helpers.h"
#include "fa6.h"

#include <cassert>
#include <vector>

// GEMV = repeated gen_mul + a ripple-carry accumulate, nothing new.
//
// gen_add (add.cpp) can't be reused directly for the accumulate step: it only
// produces sum, not its complement, so a second accumulate step couldn't
// chain off it without a NOT round-trip. gen_fa6 (shared with mul.cpp's own
// CPA) already produces sum AND ~sum per bit, so the accumulate step below is
// just that same per-bit full-adder chain applied to two DRAM operands
// (running accumulator, latest product) instead of Wallace-tree column wires.

// out/not_out := a + b, bit-by-bit, in place into out/not_out (which may
// alias a/not_a for an in-place update — see gen_fa6's aliasing note).
static void gen_ripple_add(std::vector<CudInst>& insts, ScratchAllocator& scratch,
                            const BitSerialLayout& a, const BitSerialLayout& not_a,
                            const BitSerialLayout& b, const BitSerialLayout& not_b,
                            const BitSerialLayout& out, const BitSerialLayout& not_out,
                            uint32_t width) {
    const Wire zw = zero_wire(scratch);
    Wire carry = alloc_wire(scratch);
    for (uint32_t k = 0; k < width; ++k) {
        const Wire ak  = {a.plane_row(k), not_a.plane_row(k)};
        const Wire bk  = {b.plane_row(k), not_b.plane_row(k)};
        const Wire sk  = {out.plane_row(k), not_out.plane_row(k)};
        const Wire cin = (k == 0) ? zw : carry;
        gen_fa6(insts, scratch, ak, bk, cin, carry, sk);
    }
}

std::vector<CudInst> gen_gemv(
    const std::vector<BitSerialLayout>& a,
    const std::vector<BitSerialLayout>& not_a,
    const std::vector<BitSerialLayout>& b,
    const std::vector<BitSerialLayout>& not_b,
    const BitSerialLayout& prod,
    const BitSerialLayout& not_prod,
    const BitSerialLayout& out,
    const BitSerialLayout& not_out,
    uint8_t W,
    ScratchAllocator& scratch)
{
    const size_t len = a.size();
    assert(len >= 1);
    assert(not_a.size() == len && b.size() == len && not_b.size() == len);
    assert(out.bit_width == 2u * W && not_out.bit_width == 2u * W);
    assert(prod.bit_width == 2u * W && not_prod.bit_width == 2u * W);

    std::vector<CudInst> insts;

    // i = 0: acc := a[0] * b[0]  (gen_mul writes the product's complement
    // straight into not_out too, so it's ready for the next accumulate step).
    {
        auto v = gen_mul(a[0], not_a[0], b[0], not_b[0], out, W, scratch, &not_out);
        v.pop_back();  // drop this call's END; one END covers the whole stream
        insts.insert(insts.end(), v.begin(), v.end());
    }

    // Scratch used by the first mul is dead once consumed above; every later
    // iteration rewinds to this point so scratch use doesn't grow with len.
    const uint32_t iter_base = scratch.next;

    for (size_t i = 1; i < len; ++i) {
        scratch.next = iter_base;

        auto vm = gen_mul(a[i], not_a[i], b[i], not_b[i], prod, W, scratch, &not_prod);
        vm.pop_back();
        insts.insert(insts.end(), vm.begin(), vm.end());

        gen_ripple_add(insts, scratch, out, not_out, prod, not_prod, out, not_out, 2u * W);
    }

    insts.push_back(cud_make_end());
    return insts;
}
