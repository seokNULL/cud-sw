#include "../include/cxl/address_map.h"
#include "../include/cud/interface.h"
#include "../include/cud/instruction.h"
#include "test_config.h"
#include "utils.h"

#include <iostream>
#include <vector>

void run_cud_interface_tests(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg) {
    std::cout << "\n[CUD Interface: DataCopy]\n";

    const size_t N = NUM_COL;
    std::vector<uint64_t> src(N), dst_cpu(N), dst_cud(N);
    for (size_t i = 0; i < N; ++i)
        src[i] = cfg.copy_pattern ^ static_cast<uint64_t>(i);

    dst_cpu = src;

    CudWriteRow(mem, cfg.bank, cfg.src_row, src);

    const uint64_t src_pa = encode_dram_addr({0, cfg.bank, cfg.src_row, 0});
    const uint64_t dst_pa = encode_dram_addr({0, cfg.bank, cfg.dst_row, 0});
    const auto insts = CudDataCopy(src_pa, dst_pa, CUD_ROW_SIZE_BYTES);
    std::cout << "[inst] count=" << insts.size() << "\n";

    if (!CudExecute(io, insts, cfg.inst_base, cfg.status_reg, cfg.done_mask)) {
        std::cout << "[FAIL] timeout\n";
        return;
    }

    dst_cud = CudReadRow(mem, cfg.bank, cfg.dst_row);

    print_row("[src    ]", src);
    print_row("[dst_cpu]", dst_cpu);
    print_row("[dst_cud]", dst_cud);
    std::cout << (rows_equal(dst_cpu, dst_cud) ? "[PASS]" : "[FAIL]") << " DataCopy\n";
}
