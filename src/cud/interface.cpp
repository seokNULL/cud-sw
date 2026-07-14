#include "../../include/cud/interface.h"
#include "../../include/cxl/address_map.h"
#include "../../include/cxl/enumerator.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <emmintrin.h>  // _mm_clflush, _mm_mfence, _mm_pause
#include <iostream>
#include <vector>

// ── Device initialisation ─────────────────────────────────────────────────────

bool CxlInit(CxlMem& mem, CxlIo& io) {
    const auto mem_devs = enumerate_cxl_devices();
    if (mem_devs.empty()) {
        std::cerr << "[CxlInit] No CXL DAX devices found "
                     "(checked /sys/bus/dax/devices/dax*).\n";
        return false;
    }

    const auto io_devs = enumerate_cxl_io_devices();
    if (io_devs.empty()) {
        std::cerr << "[CxlInit] No CXL IO devices found "
                     "(checked PCI class 0x0502xx and /sys/bus/cxl/devices/mem*).\n";
        return false;
    }

    if (!mem.open(mem_devs[0].dax_path)) {
        std::cerr << "[CxlInit] CxlMem(" << mem_devs[0].dax_path
                  << "): " << mem.last_error() << "\n";
        return false;
    }

    if (!io.open(io_devs[0].bdf, io_devs[0].bar_index)) {
        std::cerr << "[CxlInit] CxlIo(" << io_devs[0].bdf
                  << " BAR" << io_devs[0].bar_index
                  << "): " << io.last_error() << "\n";
        return false;
    }

    return true;
}

// ── Internal helpers ──────────────────────────────────────────────────────────

static std::vector<uint64_t> row_pa_list(uint32_t bank, uint32_t row) {
    std::vector<uint64_t> pas;
    pas.reserve(NUM_COL);
    for (uint32_t col = 0; col < static_cast<uint32_t>(NUM_COL); ++col)
        pas.push_back(encode_dram_addr({0u, bank, row, col}));
    return pas;
}

static void flush_pa_set(CxlMem& mem, const std::vector<uint64_t>& pas) {
    static constexpr uint64_t kLine = 64;
    std::vector<uint64_t> lines;
    lines.reserve(pas.size());
    for (uint64_t pa : pas)
        lines.push_back(pa & ~(kLine - 1));
    std::sort(lines.begin(), lines.end());
    lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
    char* const base = static_cast<char*>(mem.base());
    for (uint64_t off : lines)
        _mm_clflush(base + off);
    _mm_mfence();
}

static void write_instructions(CxlIo& io, uint64_t base_offset,
                                const std::vector<CudInst>& insts) {
    for (size_t i = 0; i < insts.size(); ++i)
        io.write32(base_offset + i * sizeof(CudInst), insts[i]);
}

static bool poll_done(CxlIo& io, uint64_t status_reg,
                      uint32_t done_mask, uint64_t timeout_us) {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::microseconds(timeout_us);
    do {
        if (io.read32(status_reg) & done_mask)
            return true;
        _mm_pause();
    } while (Clock::now() < deadline);
    return false;
}

// ── 1. Write input data (CXL.mem) ────────────────────────────────────────────

void cud_write_row(CxlMem& mem, uint32_t bank, uint32_t row, uint64_t pattern) {
    const auto pas = row_pa_list(bank, row);
    for (uint64_t pa : pas)
        mem.write64(pa, pattern);
    flush_pa_set(mem, pas);
}

void cud_write_row(CxlMem& mem, uint32_t bank, uint32_t row,
                   const std::vector<uint64_t>& patterns) {
    assert(patterns.size() == static_cast<size_t>(NUM_COL));
    const auto pas = row_pa_list(bank, row);
    for (uint32_t col = 0; col < static_cast<uint32_t>(NUM_COL); ++col)
        mem.write64(pas[col], patterns[col]);
    flush_pa_set(mem, pas);
}

// ── 2. Execute CUD instructions (CXL.io) ─────────────────────────────────────

bool CudExecute(CxlIo& io, const std::vector<CudInst>& insts,
                uint64_t inst_base, uint64_t status_reg,
                uint32_t done_mask, uint64_t timeout_us) {
    write_instructions(io, inst_base, insts);
    return poll_done(io, status_reg, done_mask, timeout_us);
}

// ── 3. Read result data (CXL.mem) ────────────────────────────────────────────

std::vector<uint64_t> cud_read_row(CxlMem& mem, uint32_t bank, uint32_t row) {
    const auto pas = row_pa_list(bank, row);
    flush_pa_set(mem, pas);  // invalidate before read — device wrote outside CPU cache
    std::vector<uint64_t> result(NUM_COL);
    for (uint32_t col = 0; col < static_cast<uint32_t>(NUM_COL); ++col)
        result[col] = mem.read64(pas[col]);
    return result;
}
