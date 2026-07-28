#include "cud_compute.h"
#include "cxl/address_map.h"
#include "cud/instruction.h"
#include "cud/compute_lib/compute_rows.h"
#include "cud/compute_lib/data_mapper.h"
#include "cud/compute_lib/inst_gen.h"
#include "cud/compute_lib/scratch.h"
#include "cud/compute_lib/add_table.h"
#include "../src/cud/cud_inst_helpers.h"
#include "inst_trace.h"
#include "utils.h"

#include <cstdio>
#include <iostream>
#include <vector>

// ── ROWCOPY ──────────────────────────────────────────────────────────────────

void run_rowcopy_test(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg) {
    std::cout << "\n[Instruction: ROWCOPY]\n";

    CudWriteRow(mem, cfg.bank, cfg.src_row, cfg.copy_pattern);
    CudWriteRow(mem, cfg.bank, cfg.dst_row, 0ULL);

    const uint64_t src_pa = encode_dram_addr({0, cfg.bank, cfg.src_row, 0});
    const uint64_t dst_pa = encode_dram_addr({0, cfg.bank, cfg.dst_row, 0});

    const std::vector<CudInst> insts = {
        cud_make_rowcopy_src(src_pa),
        cud_make_rowcopy_dst(dst_pa),
        cud_make_end(),
    };
    std::cout << "[inst] count=" << insts.size() << "\n";

    if (!CudExecute(io, insts)) { std::cout << "[FAIL] timeout\n"; return; }

    const std::vector<uint64_t> expected(NUM_COL, cfg.copy_pattern);
    const auto result = CudReadRow(mem, cfg.bank, cfg.dst_row);

    print_row("[src   ]", expected);
    print_row("[result]", result);
    const size_t errs = check_rows(expected, result);
    std::cout << (errs == 0 ? "[PASS]" : "[FAIL]") << " ROWCOPY\n";
}

// ── MAJ3 ─────────────────────────────────────────────────────────────────────

static void test_maj3_case(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg,
                            uint32_t mode, const char* label,
                            uint64_t bias, uint64_t expected_word) {
    const auto rows = cmp_group_rows(mode);
    std::cout << "\n[Instruction: MAJ3 mode=" << mode
              << " dc=(" << kCmpModes[mode].dc0 << "," << kCmpModes[mode].dc1
              << ") — " << label << "]\n";

    CudWriteRow(mem, cfg.bank, rows[0], cfg.pattern_a);
    CudWriteRow(mem, cfg.bank, rows[1], cfg.pattern_b);
    CudWriteRow(mem, cfg.bank, rows[2], bias);
    CudWriteRow(mem, cfg.bank, rows[3], 0ULL);

    const uint64_t cmp0_pa = encode_dram_addr({0, cfg.bank, rows[0], 0});

    const std::vector<CudInst> insts = {
        cud_make_maj3(cmp0_pa, kCmpFracPos, mode),
        cud_make_end(),
    };
    std::cout << "[inst] count=" << insts.size() << "\n";

    if (!CudExecute(io, insts)) { std::cout << "[FAIL] timeout\n"; return; }

    const std::vector<uint64_t> expected(NUM_COL, expected_word);
    const auto result = CudReadRow(mem, cfg.bank, rows[3]);

    print_row("[a     ]", std::vector<uint64_t>(NUM_COL, cfg.pattern_a));
    print_row("[b     ]", std::vector<uint64_t>(NUM_COL, cfg.pattern_b));
    print_row("[expect]", expected);
    print_row("[result]", result);
    const size_t errs = check_rows(expected, result);
    std::cout << (errs == 0 ? "[PASS]" : "[FAIL]")
              << " MAJ3 mode=" << mode << " " << label << "\n";
}

void run_maj3_test(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg) {
    for (uint32_t mode = 0; mode < static_cast<uint32_t>(kCmpModes.size()); ++mode) {
        test_maj3_case(mem, io, cfg, mode, "AND", 0ULL,  cfg.pattern_a & cfg.pattern_b);
        test_maj3_case(mem, io, cfg, mode, "OR",  ~0ULL, cfg.pattern_a | cfg.pattern_b);
    }
}

// ── Logical AND / OR ─────────────────────────────────────────────────────────

static void test_and(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg) {
    std::cout << "\n[Logical: AND]\n";

    const size_t N = NUM_COL;
    const std::vector<uint64_t> a(N, cfg.pattern_a);
    const std::vector<uint64_t> b(N, cfg.pattern_b);
    std::vector<uint64_t> dst_cpu(N);
    for (size_t i = 0; i < N; ++i) dst_cpu[i] = a[i] & b[i];

    CudWriteRow(mem, cfg.bank, cfg.row_a,    a);
    CudWriteRow(mem, cfg.bank, cfg.row_b,    b);
    CudWriteRow(mem, cfg.bank, cfg.row_bias, std::vector<uint64_t>(N, 0ULL));

    const uint64_t a_pa    = encode_dram_addr({0, cfg.bank, cfg.row_a,    0});
    const uint64_t b_pa    = encode_dram_addr({0, cfg.bank, cfg.row_b,    0});
    const uint64_t bias_pa = encode_dram_addr({0, cfg.bank, cfg.row_bias, 0});
    const uint64_t dst_pa  = encode_dram_addr({0, cfg.bank, cfg.row_dst,  0});

    const auto insts = CudAnd(a_pa, b_pa, bias_pa, dst_pa);
    if (!CudExecute(io, insts, cfg.inst_base, cfg.status_reg, cfg.done_mask)) {
        std::cout << "[FAIL] timeout\n"; return;
    }
    const auto dst_cud = CudReadRow(mem, cfg.bank, cfg.row_dst);

    print_row("[a      ]", a);
    print_row("[b      ]", b);
    print_row("[cpu AND]", dst_cpu);
    print_row("[cud AND]", dst_cud);
    const size_t errs = check_rows(dst_cpu, dst_cud);
    std::cout << (errs == 0 ? "[PASS]" : "[FAIL]") << " AND\n";
}

static void test_or(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg) {
    std::cout << "\n[Logical: OR]\n";

    const size_t N = NUM_COL;
    const std::vector<uint64_t> a(N, cfg.pattern_a);
    const std::vector<uint64_t> b(N, cfg.pattern_b);
    std::vector<uint64_t> dst_cpu(N);
    for (size_t i = 0; i < N; ++i) dst_cpu[i] = a[i] | b[i];

    CudWriteRow(mem, cfg.bank, cfg.row_a,    a);
    CudWriteRow(mem, cfg.bank, cfg.row_b,    b);
    CudWriteRow(mem, cfg.bank, cfg.row_bias, std::vector<uint64_t>(N, ~0ULL));
    CudWriteRow(mem, cfg.bank, cfg.row_zero, std::vector<uint64_t>(N, 0ULL));

    const uint64_t a_pa    = encode_dram_addr({0, cfg.bank, cfg.row_a,    0});
    const uint64_t b_pa    = encode_dram_addr({0, cfg.bank, cfg.row_b,    0});
    const uint64_t bias_pa = encode_dram_addr({0, cfg.bank, cfg.row_bias, 0});
    const uint64_t zero_pa = encode_dram_addr({0, cfg.bank, cfg.row_zero, 0});
    const uint64_t dst_pa  = encode_dram_addr({0, cfg.bank, cfg.row_dst,  0});

    const auto insts = CudOr(a_pa, b_pa, bias_pa, zero_pa, dst_pa);
    if (!CudExecute(io, insts, cfg.inst_base, cfg.status_reg, cfg.done_mask)) {
        std::cout << "[FAIL] timeout\n"; return;
    }
    const auto dst_cud = CudReadRow(mem, cfg.bank, cfg.row_dst);

    print_row("[a     ]", a);
    print_row("[b     ]", b);
    print_row("[cpu OR]", dst_cpu);
    print_row("[cud OR]", dst_cud);
    const size_t errs = check_rows(dst_cpu, dst_cud);
    std::cout << (errs == 0 ? "[PASS]" : "[FAIL]") << " OR\n";
}

void run_logical_tests(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg) {
    test_and(mem, io, cfg);
    test_or(mem, io, cfg);
}

// ── XOR ──────────────────────────────────────────────────────────────────────

void run_xor_test(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg) {
    std::cout << "\n[Logical: XOR]\n";

    constexpr uint32_t W    = 8;
    const uint32_t     bank = cfg.bank;

    // Use the lower 8 bits of each random pattern as the test value
    const uint8_t va = static_cast<uint8_t>(cfg.pattern_a);
    const uint8_t vb = static_cast<uint8_t>(cfg.pattern_b);
    const uint8_t vx = va ^ vb;

    std::cout << "  a=0x"      << std::hex << static_cast<uint32_t>(va)
              << "  b=0x"      << static_cast<uint32_t>(vb)
              << "  expect=0x" << static_cast<uint32_t>(vx) << std::dec << "\n";

    // Bit-serial layouts in the data zone (rows 0-100)
    const BitSerialLayout la   = {bank,  0, W, NUM_COL};
    const BitSerialLayout lna  = {bank,  8, W, NUM_COL};
    const BitSerialLayout lb   = {bank, 16, W, NUM_COL};
    const BitSerialLayout lnb  = {bank, 24, W, NUM_COL};
    const BitSerialLayout lout = {bank, 32, W, NUM_COL};

    // Write each bit-plane; CPU computes ~a and ~b
    for (uint32_t b = 0; b < W; ++b) {
        const uint64_t aw = ((va >> b) & 1u) ? ~0ULL : 0ULL;
        const uint64_t bw = ((vb >> b) & 1u) ? ~0ULL : 0ULL;
        CudWriteRow(mem, bank, la.plane_row(b),   aw);
        CudWriteRow(mem, bank, lna.plane_row(b), ~aw);
        CudWriteRow(mem, bank, lb.plane_row(b),   bw);
        CudWriteRow(mem, bank, lnb.plane_row(b), ~bw);
        CudWriteRow(mem, bank, lout.plane_row(b), 0ULL);
    }

    ScratchAllocator scratch(bank, row_to_mat(la.base_row));

    // Constant rows required by inst_gen — written to their absolute addresses
    CudWriteRow(mem, bank, scratch.abs_row(kZeroRow),  0ULL);
    CudWriteRow(mem, bank, scratch.abs_row(kOnesRow), ~0ULL);

    const auto insts = gen_xor(la, lna, lb, lnb, lout, scratch);
    std::cout << "[inst] count=" << insts.size() << "\n";
    print_inst_trace(insts, "XOR");

    if (!CudExecute(io, insts)) { std::cout << "[FAIL] timeout\n"; return; }

    size_t total_errs = 0;
    for (uint32_t b = 0; b < W; ++b) {
        const uint64_t expect_word = ((vx >> b) & 1u) ? ~0ULL : 0ULL;
        const std::vector<uint64_t> expect(NUM_COL, expect_word);
        const auto result = CudReadRow(mem, bank, lout.plane_row(b));
        total_errs += check_rows(expect, result);
    }
    std::cout << (total_errs == 0 ? "[PASS]" : "[FAIL]") << " XOR\n";
}

// ── ADD ───────────────────────────────────────────────────────────────────────

static void test_add_width(CxlMem& mem, CxlIo& io,
                            uint32_t bank, uint64_t pattern_a, uint64_t pattern_b,
                            uint8_t W) {
    // Extract W-bit values (one per column: broadcast same value across all columns)
    const uint32_t mask = (1u << W) - 1u;
    const uint32_t va   = static_cast<uint32_t>(pattern_a & mask);
    const uint32_t vb   = static_cast<uint32_t>(pattern_b & mask);
    const uint32_t vout = (va + vb) & ((1u << (W + 1)) - 1u);  // W+1 result bits

    std::cout << "\n[ADD " << static_cast<int>(W) << "-bit]"
              << "  a=0x" << std::hex << va
              << "  b=0x" << vb
              << "  expect=0x" << vout << std::dec << "\n";

    // Bit-serial layouts in user data zone (rows 0-100)
    //   la:0, lna:8, lb:16, lnb:24, lout:32  — fits W<=8 (max row 40 for lout)
    const BitSerialLayout la   = {bank,  0, W,     NUM_COL};
    const BitSerialLayout lna  = {bank,  8, W,     NUM_COL};
    const BitSerialLayout lb   = {bank, 16, W,     NUM_COL};
    const BitSerialLayout lnb  = {bank, 24, W,     NUM_COL};
    const BitSerialLayout lout = {bank, 32, W + 1u, NUM_COL};

    // Write bit-planes; CPU computes ~a and ~b
    for (uint32_t bit = 0; bit < W; ++bit) {
        const uint64_t aw = ((va >> bit) & 1u) ? ~0ULL : 0ULL;
        const uint64_t bw = ((vb >> bit) & 1u) ? ~0ULL : 0ULL;
        CudWriteRow(mem, bank, la.plane_row(bit),   aw);
        CudWriteRow(mem, bank, lna.plane_row(bit), ~aw);
        CudWriteRow(mem, bank, lb.plane_row(bit),   bw);
        CudWriteRow(mem, bank, lnb.plane_row(bit), ~bw);
    }
    // Zero-init output rows
    for (uint32_t bit = 0; bit <= W; ++bit)
        CudWriteRow(mem, bank, lout.plane_row(bit), 0ULL);

    ScratchAllocator scratch(bank, row_to_mat(la.base_row));
    CudWriteRow(mem, bank, scratch.abs_row(kZeroRow),  0ULL);
    CudWriteRow(mem, bank, scratch.abs_row(kOnesRow), ~0ULL);

    const auto insts = gen_add(la, lna, lb, lnb, lout, W, scratch);
    std::cout << "[inst] count=" << insts.size() << "\n";
    char trace_label[32];
    std::snprintf(trace_label, sizeof(trace_label), "ADD %d-bit", static_cast<int>(W));
    print_inst_trace(insts, trace_label);

    if (!CudExecute(io, insts)) { std::cout << "[FAIL] timeout\n"; return; }

    size_t total_errs = 0;
    for (uint32_t bit = 0; bit <= W; ++bit) {
        const uint64_t expect_word = ((vout >> bit) & 1u) ? ~0ULL : 0ULL;
        const std::vector<uint64_t> expect(NUM_COL, expect_word);
        const auto result = CudReadRow(mem, bank, lout.plane_row(bit));
        total_errs += check_rows(expect, result);
    }
    std::cout << (total_errs == 0 ? "[PASS]" : "[FAIL]")
              << " ADD " << static_cast<int>(W) << "-bit\n";
}

void run_add_test(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg) {
    std::cout << "\n[Logical: ADD]\n";
    for (uint8_t W = 1; W <= 8; ++W)
        test_add_width(mem, io, cfg.bank, cfg.pattern_a, cfg.pattern_b, W);
}
