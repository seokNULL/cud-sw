#include "../../include/cxl/address_map.h"

#include <cstddef>
#include <iostream>
#include <vector>

// Expand the #define bit-position lists into vectors.
// An empty #define (e.g. ADDR_CH_BITS) expands to {}, giving an empty vector.
static const std::vector<int> kColBits  = { ADDR_COL_BITS };
static const std::vector<int> kBankBits = { ADDR_BK_BITS  };
static const std::vector<int> kRowBits  = { ADDR_ROW_BITS };
static const std::vector<int> kChBits   = { ADDR_CH_BITS  };

// ── Bit manipulation ──────────────────────────────────────────────────────────

uint32_t extract_pa_field(uint64_t pa, const std::vector<int>& bits) {
    uint32_t val = 0;
    for (size_t i = 0; i < bits.size(); ++i)
        val |= static_cast<uint32_t>((pa >> bits[i]) & 1ULL) << i;
    return val;
}

uint64_t insert_pa_field(uint64_t pa, uint32_t field, const std::vector<int>& bits) {
    for (size_t i = 0; i < bits.size(); ++i) {
        const uint64_t mask = 1ULL << bits[i];
        if ((field >> i) & 1u)
            pa |= mask;
        else
            pa &= ~mask;
    }
    return pa;
}

// ── Mat lookup ────────────────────────────────────────────────────────────────
//
// Row offsets marking the start of each mat within one 7-mat cycle.
// Index 7 is the sentinel (= MAT_ROWS_PER_CYCLE = 8192).
static constexpr uint32_t kCycleBreaks[MAT_PER_CYCLE + 1] = {
    0, 1184, 2368, 3552, 4640, 5824, 7008, MAT_ROWS_PER_CYCLE
};

uint32_t row_to_mat(uint32_t row) {
    const uint32_t cycle      = row / MAT_ROWS_PER_CYCLE;
    const uint32_t row_in_cyc = row % MAT_ROWS_PER_CYCLE;
    uint32_t mat_in_cyc = MAT_PER_CYCLE - 1;
    for (uint32_t i = 0; i < MAT_PER_CYCLE; ++i) {
        if (row_in_cyc < kCycleBreaks[i + 1]) {
            mat_in_cyc = i;
            break;
        }
    }
    return cycle * MAT_PER_CYCLE + mat_in_cyc;
}

uint32_t mat_to_row_start(uint32_t mat) {
    const uint32_t cycle      = mat / MAT_PER_CYCLE;
    const uint32_t mat_in_cyc = mat % MAT_PER_CYCLE;
    return cycle * MAT_ROWS_PER_CYCLE + kCycleBreaks[mat_in_cyc];
}

// ── Public API ────────────────────────────────────────────────────────────────

DramAddress decode_physical_addr(uint64_t pa) {
    DramAddress d;
    d.channel = extract_pa_field(pa, kChBits);
    d.bank    = extract_pa_field(pa, kBankBits);
    d.row     = extract_pa_field(pa, kRowBits);
    d.col     = extract_pa_field(pa, kColBits);
    d.mat     = row_to_mat(d.row);
    return d;
}

uint64_t encode_dram_addr(const DramAddress& addr) {
    uint64_t pa = 0;
    pa = insert_pa_field(pa, addr.channel, kChBits);
    pa = insert_pa_field(pa, addr.bank,    kBankBits);
    pa = insert_pa_field(pa, addr.row,     kRowBits);
    pa = insert_pa_field(pa, addr.col,     kColBits);
    return pa;
}

void print_dram_address(uint64_t pa, const DramAddress& d) {
    std::cout << "  PA=0x" << std::hex << pa << std::dec
              << "  ch="   << d.channel
              << "  bank=" << d.bank
              << "  mat="  << d.mat
              << "  row="  << d.row
              << "  col="  << d.col << "\n";
}

bool validate_address_map() {
    bool ok = true;

    // Check that each field has enough bits for its declared topology count.
    struct Entry { const char* name; const std::vector<int>& bits; uint32_t count; };
    const Entry entries[] = {
        { "channel", kChBits,   NUM_CH  },
        { "bank",    kBankBits, NUM_BK  },
        { "row",     kRowBits,  NUM_ROW },
        { "col",     kColBits,  NUM_COL },
    };
    for (const auto& e : entries) {
        if (e.bits.empty()) {
            if (e.count > 1) {
                std::cerr << "[WARN] " << e.name
                          << ": no PA bits allocated but count=" << e.count << "\n";
                ok = false;
            }
            continue;
        }
        const uint32_t addressable = 1u << static_cast<int>(e.bits.size());
        if (addressable < e.count) {
            std::cerr << "[WARN] " << e.name << ": " << e.bits.size()
                      << " bit(s) can address " << addressable
                      << " units, but count=" << e.count << "\n";
            ok = false;
        }
    }

    // Check that no PA bit is assigned to more than one field.
    struct BitEntry { int bit; const char* field; };
    std::vector<BitEntry> all;
    for (int b : kChBits)   all.push_back({ b, "channel" });
    for (int b : kBankBits) all.push_back({ b, "bank"    });
    for (int b : kRowBits)  all.push_back({ b, "row"     });
    for (int b : kColBits)  all.push_back({ b, "col"     });

    for (size_t i = 0; i < all.size(); ++i) {
        for (size_t j = i + 1; j < all.size(); ++j) {
            if (all[i].bit == all[j].bit) {
                std::cerr << "[WARN] PA bit " << all[i].bit
                          << " assigned to both '" << all[i].field
                          << "' and '" << all[j].field << "'\n";
                ok = false;
            }
        }
    }

    return ok;
}
