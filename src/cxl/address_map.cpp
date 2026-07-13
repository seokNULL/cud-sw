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

// Extract a field from pa using the given bit-position list.
// bits[0] maps to field bit 0 (LSB), bits[n-1] maps to field bit n-1 (MSB).
static uint32_t extract_field(uint64_t pa, const std::vector<int>& bits) {
    uint32_t val = 0;
    for (size_t i = 0; i < bits.size(); ++i)
        val |= static_cast<uint32_t>((pa >> bits[i]) & 1ULL) << i;
    return val;
}

// Insert a field into pa at the given bit positions.
static uint64_t insert_field(uint64_t pa, uint32_t field, const std::vector<int>& bits) {
    for (size_t i = 0; i < bits.size(); ++i) {
        const uint64_t mask = 1ULL << bits[i];
        if ((field >> i) & 1u)
            pa |= mask;
        else
            pa &= ~mask;
    }
    return pa;
}

// ── Public API ────────────────────────────────────────────────────────────────

DramAddress decode_physical_addr(uint64_t pa) {
    DramAddress d;
    d.channel = extract_field(pa, kChBits);
    d.bank    = extract_field(pa, kBankBits);
    d.row     = extract_field(pa, kRowBits);
    d.col     = extract_field(pa, kColBits);
    return d;
}

uint64_t encode_dram_addr(const DramAddress& addr) {
    uint64_t pa = 0;
    pa = insert_field(pa, addr.channel, kChBits);
    pa = insert_field(pa, addr.bank,    kBankBits);
    pa = insert_field(pa, addr.row,     kRowBits);
    pa = insert_field(pa, addr.col,     kColBits);
    return pa;
}

void print_dram_address(uint64_t pa, const DramAddress& d) {
    std::cout << "  PA=0x" << std::hex << pa << std::dec
              << "  ch="   << d.channel
              << "  bank=" << d.bank
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
