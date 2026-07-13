#include "../include/cxl/address_map.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

void run_addr_map_test() {
    std::cout << "\n===== DRAM Address Map Test =====\n";

    if (!validate_address_map()) {
        std::cout << "[ERROR] Address map validation failed — fix address_map.h.\n";
        return;
    }

    std::cout << "[INFO] Topology:"
              << "  channels=" << NUM_CH
              << "  banks="    << NUM_BK
              << "  rows="     << NUM_ROW
              << "  cols="     << NUM_COL << "\n";

    // ── Sample decode ─────────────────────────────────────────────────────────
    std::cout << "\n[Decode] Sample physical addresses:\n";
    const std::vector<uint64_t> sample_pas = {
        0x0000000000000000ULL,  // all zero
        0x0000000000000040ULL,  // col 1  (PA bit 6 set)
        0x0000000000002000ULL,  // bank 1 (PA bit 13 set)
        0x0000000000020000ULL,  // row 1  (PA bit 17 set)
        0x0000000000022040ULL,  // row 1, bank 1, col 1
        0x00000000FFFFE0C0ULL,  // near-max row/bank/col
    };
    for (uint64_t pa : sample_pas)
        print_dram_address(pa, decode_physical_addr(pa));

    // ── Encode → Decode round-trip ────────────────────────────────────────────
    // Walk a coarse grid to cover all banks and a sample of rows/cols.
    std::cout << "\n[Round-trip] encode_dram_addr → decode_physical_addr:\n";

    const uint32_t row_step = std::max(1u, static_cast<uint32_t>(NUM_ROW) / 64);
    const uint32_t col_step = std::max(1u, static_cast<uint32_t>(NUM_COL) / 16);

    uint64_t tested   = 0;
    uint64_t failures = 0;

    for (uint32_t ch = 0; ch < NUM_CH;  ++ch)
    for (uint32_t bk = 0; bk < NUM_BK;  ++bk)
    for (uint32_t ro = 0; ro < NUM_ROW; ro += row_step)
    for (uint32_t co = 0; co < NUM_COL; co += col_step) {
        const DramAddress orig = { ch, bk, ro, co };
        const uint64_t    pa   = encode_dram_addr(orig);
        const DramAddress dec  = decode_physical_addr(pa);

        ++tested;
        if (dec.channel != orig.channel || dec.bank != orig.bank ||
            dec.row     != orig.row     || dec.col  != orig.col) {
            if (failures < 8) {
                std::cout << "  [FAIL] orig ch=" << orig.channel
                          << " bk=" << orig.bank
                          << " ro=" << orig.row
                          << " co=" << orig.col
                          << "  PA=0x" << std::hex << pa << std::dec
                          << "  got ch=" << dec.channel
                          << " bk=" << dec.bank
                          << " ro=" << dec.row
                          << " co=" << dec.col << "\n";
            }
            ++failures;
        }
    }

    if (failures == 0)
        std::cout << "  [PASS] All " << tested << " round-trip checks passed.\n";
    else
        std::cout << "  [FAIL] " << failures << " / " << tested << " check(s) failed.\n";

    std::cout << "\n===== RESULT: " << (failures == 0 ? "PASS" : "FAIL") << " =====\n";
}
