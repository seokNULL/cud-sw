#include "maj3.h"
#include "cxl/address_map.h"
#include "cud/instruction.h"
#include "cud/compute_lib/compute_rows.h"
#include "../../src/cud/cud_inst_helpers.h"
#include "../utils.h"

#include <iostream>
#include <vector>

static void test_maj3_case(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg,
                            const char* label, uint64_t bias,
                            uint64_t expected_word) {
    std::cout << "\n[Instruction: MAJ3 — " << label << "]\n";

    // Write operands directly to the fixed compute rows (no ROWCOPY instruction)
    CudWriteRow(mem, cfg.bank, kCmpRow0,    cfg.pattern_a);
    CudWriteRow(mem, cfg.bank, kCmpRow1,    cfg.pattern_b);
    CudWriteRow(mem, cfg.bank, kCmpRow2,    bias);
    CudWriteRow(mem, cfg.bank, kCmpRowFrac, 0ULL);

    const uint64_t cmp0_pa = encode_dram_addr({0, cfg.bank, kCmpRow0, 0});

    // Issue only the MAJ3 instruction
    const std::vector<CudInst> insts = {
        cud_make_maj3(cmp0_pa, kCmpFracPos, kCmpMode),
        cud_make_end(),
    };
    std::cout << "[inst] count=" << insts.size() << "\n";

    if (!CudExecute(io, insts)) { std::cout << "[FAIL] timeout\n"; return; }

    const std::vector<uint64_t> expected(NUM_COL, expected_word);
    const auto result = CudReadRow(mem, cfg.bank, kCmpRowFrac);

    print_row("[a     ]", std::vector<uint64_t>(NUM_COL, cfg.pattern_a));
    print_row("[b     ]", std::vector<uint64_t>(NUM_COL, cfg.pattern_b));
    print_row("[expect]", expected);
    print_row("[result]", result);
    const size_t errs = check_rows(expected, result);
    std::cout << (errs == 0 ? "[PASS]" : "[FAIL]") << " MAJ3 " << label << "\n";
}

void run_maj3_test(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg) {
    test_maj3_case(mem, io, cfg, "AND", 0ULL,  cfg.pattern_a & cfg.pattern_b);
    test_maj3_case(mem, io, cfg, "OR",  ~0ULL, cfg.pattern_a | cfg.pattern_b);
}
