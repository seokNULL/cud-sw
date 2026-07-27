#include "cud_compute.h"
#include "cxl/address_map.h"
#include "cud/instruction.h"
#include "cud/compute_lib/compute_rows.h"
#include "../src/cud/cud_inst_helpers.h"
#include "utils.h"

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
