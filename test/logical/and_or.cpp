#include "and_or.h"
#include "cxl/address_map.h"
#include "cud/interface.h"
#include "cud/instruction.h"
#include "../utils.h"

#include <iostream>
#include <vector>

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
