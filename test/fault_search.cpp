#include "fault_search.h"
#include "../include/cxl/address_map.h"
#include "../include/cud/instruction.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr int      kConsecFailThresh = 4;
static constexpr uint64_t kSrcPattern       = 0xFFFFFFFFFFFFFFFFULL;
static constexpr uint64_t kInitPattern      = 0x0000000000000000ULL;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build mat_id → [row, ...] mapping once; same layout for every bank.
static std::vector<std::vector<uint32_t>> build_mat_rows() {
    std::vector<std::vector<uint32_t>> m(NUM_MAT);
    for (uint32_t r = 0; r < (uint32_t)NUM_ROW; ++r)
        m[row_to_mat(r)].push_back(r);
    return m;
}

// Write src pattern, init dst to zero, execute DataCopy, read back dst.
// Returns true iff dst matches src pattern after the copy.
static bool datacopy_ok(CxlMem& mem, CxlIo& io,
                         uint32_t bank, uint32_t src_row, uint32_t dst_row) {
    CudWriteRow(mem, bank, dst_row, kInitPattern);

    const uint64_t src_pa = encode_dram_addr({0, bank, src_row, 0});
    const uint64_t dst_pa = encode_dram_addr({0, bank, dst_row, 0});
    const auto insts = CudDataCopy(src_pa, dst_pa, CUD_ROW_SIZE_BYTES);

    if (!CudExecute(io, insts)) return false;

    for (const auto& w : CudReadRow(mem, bank, dst_row))
        if (w != kSrcPattern) return false;
    return true;
}

// Scan one MAT in one bank.
// Tries each row as source in turn; switches source after kConsecFailThresh
// consecutive dst failures and discards tentative results.
// Returns faulty (dst) row addresses; empty if the whole MAT is untestable.
static std::vector<uint32_t> scan_mat(CxlMem& mem, CxlIo& io,
                                       uint32_t bank,
                                       const std::vector<uint32_t>& rows) {
    for (size_t si = 0; si < rows.size(); ++si) {
        const uint32_t src_row = rows[si];
        CudWriteRow(mem, bank, src_row, kSrcPattern);

        int  consec   = 0;
        bool src_bad  = false;
        std::vector<uint32_t> tentative;

        for (size_t di = 0; di < rows.size(); ++di) {
            if (di == si) continue;
            const uint32_t dst_row = rows[di];

            if (datacopy_ok(mem, io, bank, src_row, dst_row)) {
                consec = 0;
            } else {
                tentative.push_back(dst_row);
                if (++consec >= kConsecFailThresh) { src_bad = true; break; }
            }
        }

        if (!src_bad) return tentative;
        // else: this source is bad — try next candidate, discard tentative results
    }
    return {};  // all candidates exhausted — MAT untestable
}

static std::string make_csv_filename() {
    auto now = std::chrono::system_clock::to_time_t(
                   std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);
    char buf[40];
    std::strftime(buf, sizeof(buf), "fault_map_%Y%m%d_%H%M%S.csv", &tm);
    return buf;
}

// ── Public entry point ────────────────────────────────────────────────────────

void run_fault_search(CxlMem& mem, CxlIo& io) {
    std::cout << "\n[Fault Row Search]\n";

    const auto mat_rows = build_mat_rows();

    const std::string fname = make_csv_filename();
    std::ofstream csv(fname);
    if (!csv) { std::cerr << "  [ERR] cannot open " << fname << "\n"; return; }
    csv << "bank,mat,row\n";

    size_t total_faults = 0;

    for (uint32_t bank = 0; bank < (uint32_t)NUM_BK; ++bank) {
        std::cout << "  bank " << std::setw(2) << bank
                  << " / " << NUM_BK << " ...\n" << std::flush;

        for (uint32_t mat = 0; mat < (uint32_t)NUM_MAT; ++mat) {
            const auto& rows = mat_rows[mat];
            if (rows.empty()) continue;

            const auto faults = scan_mat(mem, io, bank, rows);

            if (faults.empty() && !rows.empty()) {
                // check if scan_mat returned empty because all sources were bad
                // (can't distinguish easily, so just skip silently unless we
                //  want to add an untestable-MAT log — keep it simple for now)
            }

            for (uint32_t row : faults) {
                csv << bank << "," << mat << "," << row << "\n";
                ++total_faults;
            }

            if (!faults.empty()) {
                std::cout << "    mat " << std::setw(3) << mat
                          << ": " << faults.size() << " fault(s)\n";
            }
        }

        csv.flush();
    }

    std::cout << "\n  Total: " << total_faults << " faulty row(s)\n"
              << "  Results saved to: " << fname << "\n";
}
