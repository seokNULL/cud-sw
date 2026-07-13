#include "../include/cxl/address_map.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

void run_addr_map_test() {
    std::cout << "\n===== DRAM Address Map Test =====\n";

    const DramAddressMap map = default_dram_address_map();

    // ── Validation ────────────────────────────────────────────────────────────
    if (!validate_address_map(map)) {
        std::cout << "[ERROR] Address map validation failed — fix the map first.\n";
        return;
    }

    std::cout << "[INFO] Topology:"
              << "  channels=" << map.topology.num_channels
              << "  banks="    << map.topology.num_banks
              << "  rows="     << map.topology.num_rows
              << "  cols="     << map.topology.num_cols << "\n";
    std::cout << "[INFO] PA layout:"
              << "  col["    << map.col.start_bit + map.col.num_bits - 1
              <<        ":"  << map.col.start_bit << "]"
              << "  bank["   << map.bank.start_bit + map.bank.num_bits - 1
              <<        ":"  << map.bank.start_bit << "]"
              << "  row["    << map.row.start_bit + map.row.num_bits - 1
              <<        ":"  << map.row.start_bit << "]";
    if (map.channel.num_bits > 0)
        std::cout << "  ch["
                  << map.channel.start_bit + map.channel.num_bits - 1
                  << ":"  << map.channel.start_bit << "]";
    std::cout << "\n";

    // ── Sample decode ─────────────────────────────────────────────────────────
    std::cout << "\n[Decode] Sample physical addresses:\n";
    const std::vector<uint64_t> sample_pas = {
        0x0000000000000000ULL,                      // all zero
        0x0000000000000040ULL,                      // col 1
        0x0000000000002000ULL,                      // bank 1
        0x0000000000020000ULL,                      // row 1
        0x0000000000022040ULL,                      // row 1, bank 1, col 1
        0x00000000FFFFE0C0ULL,                      // max-ish row/bank/col
    };
    for (uint64_t pa : sample_pas)
        print_dram_address(pa, decode_physical_addr(pa, map));

    // ── Encode → Decode round-trip ────────────────────────────────────────────
    // Walk a coarse grid over the full topology to keep runtime short.
    std::cout << "\n[Round-trip] encode_dram_addr → decode_physical_addr:\n";

    const uint32_t ch_step  = 1;
    const uint32_t bk_step  = 1;
    const uint32_t row_step = std::max(1u, map.topology.num_rows / 16);
    const uint32_t col_step = std::max(1u, map.topology.num_cols / 16);

    uint64_t tested   = 0;
    uint64_t failures = 0;

    for (uint32_t ch = 0; ch < map.topology.num_channels; ch += ch_step)
    for (uint32_t bk = 0; bk < map.topology.num_banks;    bk += bk_step)
    for (uint32_t ro = 0; ro < map.topology.num_rows;      ro += row_step)
    for (uint32_t co = 0; co < map.topology.num_cols;      co += col_step) {
        const DramAddress orig = { ch, bk, ro, co };
        const uint64_t    pa   = encode_dram_addr(orig, map);
        const DramAddress dec  = decode_physical_addr(pa, map);

        ++tested;
        if (dec.channel != orig.channel || dec.bank != orig.bank ||
            dec.row     != orig.row     || dec.col  != orig.col) {
            if (failures < 8) {
                std::cout << "  [FAIL] orig: ch=" << orig.channel
                          << " bk=" << orig.bank
                          << " ro=" << orig.row
                          << " co=" << orig.col
                          << "  PA=0x" << std::hex << pa << std::dec
                          << "  decoded: ch=" << dec.channel
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
        std::cout << "  [FAIL] " << failures << " / " << tested
                  << " round-trip check(s) failed.\n";

    std::cout << "\n===== RESULT: " << (failures == 0 ? "PASS" : "FAIL")
              << " =====\n";
}
