#include "../include/cxl/address_map.h"
#include "../include/cud/interface.h"
#include "../include/cud/instruction.h"
#include "test_config.h"
#include "utils.h"

#include <iostream>
#include <vector>

static void test_and(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg) {
    std::cout << "\n[AND]\n";

    const size_t N = NUM_COL;
    std::vector<uint64_t> a(N, cfg.pattern_a);
    std::vector<uint64_t> b(N, cfg.pattern_b);
    std::vector<uint64_t> dst_cpu(N), dst_cud(N);

    for (size_t i = 0; i < N; ++i)
        dst_cpu[i] = a[i] & b[i];

    CudWriteRow(mem, cfg.bank, cfg.row_a,    a);
    CudWriteRow(mem, cfg.bank, cfg.row_b,    b);
    CudWriteRow(mem, cfg.bank, cfg.row_bias, std::vector<uint64_t>(N, 0ULL));

    const uint64_t a_pa    = encode_dram_addr({0, cfg.bank, cfg.row_a,    0});
    const uint64_t b_pa    = encode_dram_addr({0, cfg.bank, cfg.row_b,    0});
    const uint64_t bias_pa = encode_dram_addr({0, cfg.bank, cfg.row_bias, 0});
    const uint64_t dst_pa  = encode_dram_addr({0, cfg.bank, cfg.row_dst,  0});

    const auto insts = CudAnd(a_pa, b_pa, bias_pa, dst_pa);
    if (!CudExecute(io, insts, cfg.inst_base, cfg.status_reg, cfg.done_mask)) {
        std::cout << "[FAIL] timeout\n";
        return;
    }
    dst_cud = CudReadRow(mem, cfg.bank, cfg.row_dst);

    print_row("[a      ]", a);
    print_row("[b      ]", b);
    print_row("[cpu AND]", dst_cpu);
    print_row("[cud AND]", dst_cud);
    std::cout << (rows_equal(dst_cpu, dst_cud) ? "[PASS]" : "[FAIL]") << " AND\n";
}

static void test_or(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg) {
    std::cout << "\n[OR]\n";

    const size_t N = NUM_COL;
    std::vector<uint64_t> a(N, cfg.pattern_a);
    std::vector<uint64_t> b(N, cfg.pattern_b);
    std::vector<uint64_t> dst_cpu(N), dst_cud(N);

    for (size_t i = 0; i < N; ++i)
        dst_cpu[i] = a[i] | b[i];

    CudWriteRow(mem, cfg.bank, cfg.row_a,    a);
    CudWriteRow(mem, cfg.bank, cfg.row_b,    b);
    CudWriteRow(mem, cfg.bank, cfg.row_bias, std::vector<uint64_t>(N, ~0ULL));

    const uint64_t a_pa    = encode_dram_addr({0, cfg.bank, cfg.row_a,    0});
    const uint64_t b_pa    = encode_dram_addr({0, cfg.bank, cfg.row_b,    0});
    const uint64_t bias_pa = encode_dram_addr({0, cfg.bank, cfg.row_bias, 0});
    const uint64_t dst_pa  = encode_dram_addr({0, cfg.bank, cfg.row_dst,  0});

    const auto insts = CudOr(a_pa, b_pa, bias_pa, dst_pa);
    if (!CudExecute(io, insts, cfg.inst_base, cfg.status_reg, cfg.done_mask)) {
        std::cout << "[FAIL] timeout\n";
        return;
    }
    dst_cud = CudReadRow(mem, cfg.bank, cfg.row_dst);

    print_row("[a     ]", a);
    print_row("[b     ]", b);
    print_row("[cpu OR]", dst_cpu);
    print_row("[cud OR]", dst_cud);
    std::cout << (rows_equal(dst_cpu, dst_cud) ? "[PASS]" : "[FAIL]") << " OR\n";
}

void run_cud_library_tests(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg) {
    test_and(mem, io, cfg);
    test_or(mem, io, cfg);
}
