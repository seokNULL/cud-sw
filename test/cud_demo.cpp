#include "../include/cxl/address_map.h"
#include "../include/cud/interface.h"
#include "../include/cud/instruction.h"

#include <iomanip>
#include <iostream>
#include <vector>

// ── Demo parameters ───────────────────────────────────────────────────────────

static constexpr uint32_t kSrcBank    = 0;
static constexpr uint32_t kSrcRow     = 0;
static constexpr uint32_t kDstBank    = 0;
static constexpr uint32_t kDstRow     = 1;
static constexpr uint64_t kPattern    = 0xDEADBEEFCAFEBABEULL;

// CXL.io register offsets (device-specific — adjust to match hardware)
static constexpr uint64_t kInstBase   = 0x0000;  // instruction FIFO base
static constexpr uint64_t kStatusReg  = 0x0010;  // completion status register
static constexpr uint32_t kDoneMask   = 0x1;     // bit 0 = done

// ── Helpers ───────────────────────────────────────────────────────────────────

static void print_row(const char* label, const std::vector<uint64_t>& row) {
    std::cout << label;
    const size_t preview = std::min(row.size(), size_t(4));
    for (size_t i = 0; i < preview; ++i)
        std::cout << " 0x" << std::hex << std::setw(16) << std::setfill('0') << row[i];
    if (row.size() > preview)
        std::cout << " ...";
    std::cout << std::dec << "\n";
}

static bool rows_equal(const std::vector<uint64_t>& a,
                       const std::vector<uint64_t>& b) {
    return a == b;
}

// ── Demo entry point ──────────────────────────────────────────────────────────

void run_cud_demo() {
    std::cout << "\n===== CUD Data Copy Demo =====\n";

    CxlMem mem;
    CxlIo  io;
    if (!CxlInit(mem, io)) return;

    std::cout << "[INFO] src=bank" << kSrcBank << "/row" << kSrcRow
              << "  dst=bank" << kDstBank << "/row" << kDstRow << "\n";

    // PA of column 0 in each row — used as the row identifier for instruction
    // encoding (decode_physical_addr extracts bank + row from any col in row).
    const uint64_t src_pa = encode_dram_addr({0, kSrcBank, kSrcRow, 0});
    const uint64_t dst_pa = encode_dram_addr({0, kDstBank, kDstRow, 0});

    // Write source pattern once — both copy paths use the same source.
    cud_write_row(mem, kSrcBank, kSrcRow, kPattern);
    const auto src_data = cud_read_row(mem, kSrcBank, kSrcRow);
    print_row("[src ]", src_data);

    // ── CPU data copy ─────────────────────────────────────────────────────────
    std::cout << "\n[CPU copy]\n";

    cud_write_row(mem, kDstBank, kDstRow, src_data);   // CPU: read src, write dst
    const auto cpu_result = cud_read_row(mem, kDstBank, kDstRow);
    print_row("[dst ]", cpu_result);
    std::cout << (rows_equal(cpu_result, src_data) ? "[PASS]" : "[FAIL]")
              << " CPU copy\n";

    // ── CUD data copy ─────────────────────────────────────────────────────────
    std::cout << "\n[CUD copy]\n";

    // Step 1: load input into CXL.mem (already written above, but redo for clarity)
    cud_write_row(mem, kSrcBank, kSrcRow, kPattern);

    // Step 2: generate instruction list and write to CXL.io BAR
    const std::vector<CudInst> insts = cud_data_copy(src_pa, dst_pa);
    std::cout << "[inst] count=" << insts.size() << "\n";
    cud_write_instructions(io, kInstBase, insts);

    // Step 3: poll for CUD completion
    if (!cud_poll_done(io, kStatusReg, kDoneMask)) {
        std::cout << "[FAIL] CUD timed out\n";
        return;
    }

    // Step 4: read result from CXL.mem
    const auto cud_result = cud_read_row(mem, kDstBank, kDstRow);
    print_row("[dst ]", cud_result);
    std::cout << (rows_equal(cud_result, src_data) ? "[PASS]" : "[FAIL]")
              << " CUD copy\n";

    std::cout << "\n===== DONE =====\n";
}
