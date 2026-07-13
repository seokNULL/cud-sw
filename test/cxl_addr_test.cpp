#include "../include/cxl/address_map.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

// ── Split-column test ─────────────────────────────────────────────────────────
// Tests a mapping where the column index is split across two non-contiguous
// PA regions:
//   col[ 2:0] ← PA[ 2:0]   (lower 3 bits)
//   col[ 9:3] ← PA[12:6]   (upper 7 bits)
//   bank[3:0] ← PA[16:13]
//   row[15:0] ← PA[32:17]
//   (PA[5:3] unused — e.g. byte offset bits)
static void test_split_column() {
    std::cout << "\n[Split-column] col[2:0]=PA[2:0]  col[9:3]=PA[12:6]"
                 "  bank[3:0]=PA[16:13]  row[15:0]=PA[32:17]\n";

    const std::vector<int> col_bits  = { 0, 1, 2, 6, 7, 8, 9, 10, 11, 12 }; // 10 bits → 1024 cols
    const std::vector<int> bank_bits = { 13, 14, 15, 16 };
    const std::vector<int> row_bits  = { 17, 18, 19, 20, 21, 22, 23, 24,
                                         25, 26, 27, 28, 29, 30, 31, 32 };

    // Verify known PA values decode to the expected DRAM addresses.
    struct Case {
        uint64_t pa;
        uint32_t exp_row, exp_bank, exp_col;
        const char* note;
    };
    const Case cases[] = {
        // PA[2:0]=0b111 → col lower=7,  PA[12:6]=0 → col upper=0  → col=7
        { 0x0000000000000007ULL,  0,  0,   7, "lower 3 col bits only" },
        // PA[12:6]=0b1000000 (bit 12 set) → col upper bit 6 set → col[9]=1 → col=512
        // PA[2:0]=0 → col lower=0 → col=512
        { 0x0000000000001000ULL,  0,  0, 512, "upper col bit 6 only (PA[12])" },
        // PA[2:0]=0b111, PA[12:6]=0b1111111 → col lower=7, col upper=127 → col=(127<<3)|7=1023
        { 0x0000000000001FC7ULL,  0,  0, 1023, "col=1023 (all col bits set)" },
        // PA[16:13]=0b0001 → bank=1
        { 0x0000000000002000ULL,  0,  1,   0, "bank 1" },
        // PA[32:17]=0b1 → row=1
        { 0x0000000000020000ULL,  1,  0,   0, "row 1" },
        // Combined: row=3, bank=5, col=1023
        // col=1023: PA[2:0]=111, PA[12:6]=1111111 → 0x1FC7
        // bank=5=0b0101: PA[13]=1,PA[15]=1         → 0xA000
        // row=3: PA[17]=1,PA[18]=1                 → 0x60000
        // total PA = 0x1FC7 | 0xA000 | 0x60000 = 0x6BFC7
        { 0x000000000006BFC7ULL,  3,  5, 1023, "row=3 bank=5 col=1023" },
    };

    uint32_t failures = 0;
    for (const auto& c : cases) {
        const uint32_t col  = extract_pa_field(c.pa, col_bits);
        const uint32_t bank = extract_pa_field(c.pa, bank_bits);
        const uint32_t row  = extract_pa_field(c.pa, row_bits);

        const bool ok = (col == c.exp_col && bank == c.exp_bank && row == c.exp_row);
        std::cout << (ok ? "  [PASS]" : "  [FAIL]")
                  << "  PA=0x" << std::hex << c.pa << std::dec
                  << "  row=" << row << " bank=" << bank << " col=" << col;
        if (!ok)
            std::cout << "  (expected row=" << c.exp_row
                      << " bank=" << c.exp_bank << " col=" << c.exp_col << ")";
        std::cout << "  — " << c.note << "\n";
        if (!ok) ++failures;
    }

    // Round-trip: encode then decode must be identity for all col/bank combos.
    uint64_t rt_tested = 0, rt_fail = 0;
    for (uint32_t bk = 0; bk < 16;   ++bk)
    for (uint32_t ro = 0; ro < 4;    ++ro)
    for (uint32_t co = 0; co < 1024; ++co) {
        uint64_t pa = 0;
        pa = insert_pa_field(pa, ro, row_bits);
        pa = insert_pa_field(pa, bk, bank_bits);
        pa = insert_pa_field(pa, co, col_bits);

        const uint32_t d_col  = extract_pa_field(pa, col_bits);
        const uint32_t d_bank = extract_pa_field(pa, bank_bits);
        const uint32_t d_row  = extract_pa_field(pa, row_bits);

        ++rt_tested;
        if (d_col != co || d_bank != bk || d_row != ro) {
            if (rt_fail < 4)
                std::cout << "  [FAIL] rt: ro=" << ro << " bk=" << bk << " co=" << co
                          << "  PA=0x" << std::hex << pa << std::dec
                          << "  got row=" << d_row << " bank=" << d_bank << " col=" << d_col << "\n";
            ++rt_fail;
        }
    }

    const uint64_t total_fail = failures + rt_fail;
    if (rt_fail == 0)
        std::cout << "  [PASS] round-trip: all " << rt_tested << " checks passed.\n";
    else
        std::cout << "  [FAIL] round-trip: " << rt_fail << " / " << rt_tested << " failed.\n";

    std::cout << "  Split-column result: " << (total_fail == 0 ? "PASS" : "FAIL") << "\n";
}

// ── Main test entry point ─────────────────────────────────────────────────────

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

    // ── Sample decode with the configured mapping ─────────────────────────────
    std::cout << "\n[Decode] Sample physical addresses (configured mapping):\n";
    const std::vector<uint64_t> sample_pas = {
        0x0000000000000000ULL,
        0x0000000000000040ULL,  // col 1  (PA bit 6)
        0x0000000000002000ULL,  // bank 1 (PA bit 13)
        0x0000000000020000ULL,  // row 1  (PA bit 17)
        0x0000000000022040ULL,  // row 1, bank 1, col 1
        0x00000000FFFFE0C0ULL,
    };
    for (uint64_t pa : sample_pas)
        print_dram_address(pa, decode_physical_addr(pa));

    // ── Encode → Decode round-trip with configured mapping ────────────────────
    std::cout << "\n[Round-trip] encode_dram_addr → decode_physical_addr (configured mapping):\n";

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

    // ── Split-column mapping test ─────────────────────────────────────────────
    test_split_column();

    std::cout << "\n===== RESULT: " << (failures == 0 ? "PASS" : "FAIL") << " =====\n";
}
