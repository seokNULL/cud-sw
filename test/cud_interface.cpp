#include "../include/cxl/address_map.h"
#include "../include/cud/interface.h"
#include "../include/cud/instruction.h"
#include "utils.h"

#include <iostream>
#include <vector>

// ── Shared test constants ─────────────────────────────────────────────────────

static constexpr uint32_t kBank      = 0;
static constexpr size_t   kN         = NUM_COL;   // 1024 × uint64_t = 8 KiB
static constexpr uint64_t kInstBase  = 0x0000;
static constexpr uint64_t kStatusReg = 0x0000;
static constexpr uint32_t kDoneMask  = 0x1;

// ── CUD operation wrappers ────────────────────────────────────────────────────
// Each wrapper: write input(s) to CXL.mem → execute → read result back.

static bool cud_copy_op(CxlMem& mem, CxlIo& io,
                        const std::vector<uint64_t>& src,
                        std::vector<uint64_t>& dst) {
    const uint32_t src_row = 0, dst_row = 1;
    CudWriteRow(mem, kBank, src_row, src);
    const uint64_t src_pa = encode_dram_addr({0, kBank, src_row, 0});
    const uint64_t dst_pa = encode_dram_addr({0, kBank, dst_row, 0});
    const auto insts = CudDataCopy(src_pa, dst_pa, CUD_ROW_SIZE_BYTES);
    if (!CudExecute(io, insts, kInstBase, kStatusReg, kDoneMask)) return false;
    dst = CudReadRow(mem, kBank, dst_row);
    return true;
}

static bool cud_and_op(CxlMem& mem, CxlIo& io,
                       const std::vector<uint64_t>& a,
                       const std::vector<uint64_t>& b,
                       std::vector<uint64_t>& dst) {
    const uint32_t row_a = 0, row_b = 1, row_zero = 2, row_dst = 3;
    CudWriteRow(mem, kBank, row_a,    a);
    CudWriteRow(mem, kBank, row_b,    b);
    CudWriteRow(mem, kBank, row_zero, std::vector<uint64_t>(kN, 0ULL));
    const uint64_t a_pa    = encode_dram_addr({0, kBank, row_a,    0});
    const uint64_t b_pa    = encode_dram_addr({0, kBank, row_b,    0});
    const uint64_t zero_pa = encode_dram_addr({0, kBank, row_zero, 0});
    const uint64_t dst_pa  = encode_dram_addr({0, kBank, row_dst,  0});
    const auto insts = CudAnd(a_pa, b_pa, zero_pa, dst_pa);
    if (!CudExecute(io, insts, kInstBase, kStatusReg, kDoneMask)) return false;
    dst = CudReadRow(mem, kBank, row_dst);
    return true;
}

static bool cud_or_op(CxlMem& mem, CxlIo& io,
                      const std::vector<uint64_t>& a,
                      const std::vector<uint64_t>& b,
                      std::vector<uint64_t>& dst) {
    const uint32_t row_a = 0, row_b = 1, row_one = 2, row_dst = 3;
    CudWriteRow(mem, kBank, row_a,   a);
    CudWriteRow(mem, kBank, row_b,   b);
    CudWriteRow(mem, kBank, row_one, std::vector<uint64_t>(kN, ~0ULL));
    const uint64_t a_pa   = encode_dram_addr({0, kBank, row_a,   0});
    const uint64_t b_pa   = encode_dram_addr({0, kBank, row_b,   0});
    const uint64_t one_pa = encode_dram_addr({0, kBank, row_one, 0});
    const uint64_t dst_pa = encode_dram_addr({0, kBank, row_dst, 0});
    const auto insts = CudOr(a_pa, b_pa, one_pa, dst_pa);
    if (!CudExecute(io, insts, kInstBase, kStatusReg, kDoneMask)) return false;
    dst = CudReadRow(mem, kBank, row_dst);
    return true;
}

// ── Test cases ────────────────────────────────────────────────────────────────

static void test_data_copy(CxlMem& mem, CxlIo& io) {
    std::cout << "\n[DataCopy] src[i] = 0xDEADBEEFCAFEBABE ^ i\n";

    std::vector<uint64_t> src(kN), dst_cpu(kN), dst_cud(kN);
    for (size_t i = 0; i < kN; ++i)
        src[i] = 0xDEADBEEFCAFEBABEULL ^ static_cast<uint64_t>(i);

    dst_cpu = src;

    if (!cud_copy_op(mem, io, src, dst_cud)) {
        std::cout << "[FAIL] CUD timed out\n";
        return;
    }

    print_row("[src    ]", src);
    print_row("[dst_cpu]", dst_cpu);
    print_row("[dst_cud]", dst_cud);
    std::cout << (rows_equal(dst_cpu, dst_cud) ? "[PASS]" : "[FAIL]") << " DataCopy\n";
}

static void test_and(CxlMem& mem, CxlIo& io) {
    std::cout << "\n[AND] a=0xAAAA..., b=0xCCCC..., expected=0x8888...\n";

    std::vector<uint64_t> a(kN, 0xAAAAAAAAAAAAAAAAULL);
    std::vector<uint64_t> b(kN, 0xCCCCCCCCCCCCCCCCULL);
    std::vector<uint64_t> dst_cpu(kN), dst_cud(kN);

    for (size_t i = 0; i < kN; ++i)
        dst_cpu[i] = a[i] & b[i];

    if (!cud_and_op(mem, io, a, b, dst_cud)) {
        std::cout << "[FAIL] CUD timed out\n";
        return;
    }

    print_row("[a      ]", a);
    print_row("[b      ]", b);
    print_row("[cpu AND]", dst_cpu);
    print_row("[cud AND]", dst_cud);
    std::cout << (rows_equal(dst_cpu, dst_cud) ? "[PASS]" : "[FAIL]") << " AND\n";
}

static void test_or(CxlMem& mem, CxlIo& io) {
    std::cout << "\n[OR] a=0xAAAA..., b=0x5555..., expected=0xFFFF...\n";

    std::vector<uint64_t> a(kN, 0xAAAAAAAAAAAAAAAAULL);
    std::vector<uint64_t> b(kN, 0x5555555555555555ULL);
    std::vector<uint64_t> dst_cpu(kN), dst_cud(kN);

    for (size_t i = 0; i < kN; ++i)
        dst_cpu[i] = a[i] | b[i];

    if (!cud_or_op(mem, io, a, b, dst_cud)) {
        std::cout << "[FAIL] CUD timed out\n";
        return;
    }

    print_row("[a     ]", a);
    print_row("[b     ]", b);
    print_row("[cpu OR]", dst_cpu);
    print_row("[cud OR]", dst_cud);
    std::cout << (rows_equal(dst_cpu, dst_cud) ? "[PASS]" : "[FAIL]") << " OR\n";
}

// ── Sub-menu ──────────────────────────────────────────────────────────────────

static void print_cud_menu() {
    std::cout << "\n  -- CUD API Verification --\n"
              << "  [1]  DataCopy\n"
              << "  [2]  AND\n"
              << "  [3]  OR\n"
              << "  [0]  Back\n"
              << "  Select: ";
}

void run_cud_tests() {
    CxlMem mem;
    CxlIo  io;
    if (!CxlInit(mem, io)) return;

    int choice;
    do {
        print_cud_menu();
        std::cin >> choice;
        switch (choice) {
        case 1: test_data_copy(mem, io); break;
        case 2: test_and(mem, io);       break;
        case 3: test_or(mem, io);        break;
        case 0: break;
        default: std::cout << "  Invalid selection.\n"; break;
        }
    } while (choice != 0);
}
