#include "rowcopy.h"
#include "cxl/address_map.h"
#include "cud/instruction.h"
#include "../../src/cud/cud_inst_helpers.h"
#include "../utils.h"

#include <iostream>
#include <vector>

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
