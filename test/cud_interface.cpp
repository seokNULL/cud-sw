#include "../include/cxl/address_map.h"
#include "../include/cud/interface.h"
#include "../include/cud/instruction.h"
#include "utils.h"

#include <iostream>
#include <vector>

// ── Test parameters ───────────────────────────────────────────────────────────

static constexpr uint32_t kSrcBank   = 0;
static constexpr uint32_t kSrcRow    = 0;
static constexpr uint32_t kDstBank   = 0;
static constexpr uint32_t kDstRow    = 1;
static constexpr uint64_t kPattern   = 0xDEADBEEFCAFEBABEULL;

static constexpr uint64_t kInstBase  = 0x0000;
static constexpr uint64_t kStatusReg = 0x0000;
static constexpr uint32_t kDoneMask  = 0x1;

// ── Interface test entry point ────────────────────────────────────────────────

void run_cud_interface() {
    std::cout << "\n===== CUD Interface Test =====\n";

    CxlMem mem;
    CxlIo  io;
    if (!CxlInit(mem, io)) return;

    std::cout << "[INFO] src=bank" << kSrcBank << "/row" << kSrcRow
              << "  dst=bank" << kDstBank << "/row" << kDstRow << "\n";

    const uint64_t src_pa = encode_dram_addr({0, kSrcBank, kSrcRow, 0});
    const uint64_t dst_pa = encode_dram_addr({0, kDstBank, kDstRow, 0});

    // Step 1: write pattern into source row
    CudWriteRow(mem, kSrcBank, kSrcRow, kPattern);
    const auto src_data = CudReadRow(mem, kSrcBank, kSrcRow);
    print_row("[src ]", src_data);

    // Step 2: build instruction list and execute via CXL.io
    const auto insts = CudDataCopy(src_pa, dst_pa, CUD_ROW_SIZE_BYTES);
    std::cout << "[inst] count=" << insts.size() << "\n";
    if (!CudExecute(io, insts, kInstBase, kStatusReg, kDoneMask)) {
        std::cout << "[FAIL] CUD timed out\n";
        return;
    }

    // Step 3: read and verify result
    const auto result = CudReadRow(mem, kDstBank, kDstRow);
    print_row("[dst ]", result);
    std::cout << (result == src_data ? "[PASS]" : "[FAIL]") << " CUD data copy\n";

    std::cout << "\n===== DONE =====\n";
}
