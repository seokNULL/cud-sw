#include "../../include/cud/stack.h"
#include "../../include/cxl/address_map.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <emmintrin.h>  // _mm_clflush, _mm_mfence, _mm_pause
#include <vector>

// ── Internal helpers ──────────────────────────────────────────────────────────

// Collect physical address offsets for every column in (bank, row).
static std::vector<uint64_t> row_pa_list(uint32_t bank, uint32_t row) {
    std::vector<uint64_t> pas;
    pas.reserve(NUM_COL);
    for (uint32_t col = 0; col < static_cast<uint32_t>(NUM_COL); ++col)
        pas.push_back(encode_dram_addr({0u, bank, row, col}));
    return pas;
}

// Flush a set of physical address offsets from the CPU cache.
// Deduplicates by cache line so each 64-byte line is flushed at most once.
// Issues a single _mm_mfence at the end.
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

// ── 1. Write input data ───────────────────────────────────────────────────────

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

// ── 2. Write CUD instructions ─────────────────────────────────────────────────

void cud_write_instructions(CxlIo& io, uint64_t base_offset,
                            const std::vector<CudInst>& insts) {
    for (size_t i = 0; i < insts.size(); ++i)
        io.write64(base_offset + i * sizeof(CudInst), insts[i]);
}

// ── 3. Poll for done ──────────────────────────────────────────────────────────

bool cud_poll_done(CxlIo& io, uint64_t status_offset,
                   uint32_t done_mask, uint64_t timeout_us) {
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::microseconds(timeout_us);

    do {
        if (io.read32(status_offset) & done_mask)
            return true;
        _mm_pause();  // spin-wait hint — reduces power and contention
    } while (Clock::now() < deadline);

    return false;
}

// ── 4. Read result data ───────────────────────────────────────────────────────

std::vector<uint64_t> cud_read_row(CxlMem& mem, uint32_t bank, uint32_t row) {
    const auto pas = row_pa_list(bank, row);

    // Invalidate CPU cache before reading so stale lines are not returned.
    // CUD results are written by the device (outside the CPU cache hierarchy).
    flush_pa_set(mem, pas);

    std::vector<uint64_t> result(NUM_COL);
    for (uint32_t col = 0; col < static_cast<uint32_t>(NUM_COL); ++col)
        result[col] = mem.read64(pas[col]);
    return result;
}
