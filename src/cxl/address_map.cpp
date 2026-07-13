#include "../../include/cxl/address_map.h"

#include <cstdint>
#include <iostream>

// ── Bit manipulation helpers ──────────────────────────────────────────────────

static uint32_t extract_bits(uint64_t val, const AddrBitField& f) {
    if (f.num_bits <= 0) return 0;
    return static_cast<uint32_t>((val >> f.start_bit) & ((1ULL << f.num_bits) - 1));
}

static uint64_t insert_bits(uint64_t val, uint32_t field, const AddrBitField& f) {
    if (f.num_bits <= 0) return val;
    const uint64_t mask = ((1ULL << f.num_bits) - 1ULL) << f.start_bit;
    return (val & ~mask) | ((static_cast<uint64_t>(field) << f.start_bit) & mask);
}

// ── Default map ───────────────────────────────────────────────────────────────
// Edit this function to match your CXL device's actual address mapping.
//
// Current example — DDR5-like, single channel:
//   PA[5:0]  = byte offset within 64-byte cache line (not mapped)
//   PA[12:6] = column   (7 bits → 128 cache-line-addressed columns)
//   PA[16:13]= bank     (4 bits → 16 banks)
//   PA[32:17]= row      (16 bits → 65536 rows)
DramAddressMap default_dram_address_map() {
    DramAddressMap m;
    m.topology.num_channels = 1;
    m.topology.num_banks    = 16;
    m.topology.num_rows     = 65536;
    m.topology.num_cols     = 128;

    m.col     = { .start_bit =  6, .num_bits =  7 }; // PA[12:6]
    m.bank    = { .start_bit = 13, .num_bits =  4 }; // PA[16:13]
    m.row     = { .start_bit = 17, .num_bits = 16 }; // PA[32:17]
    m.channel = { .start_bit =  0, .num_bits =  0 }; // single channel

    return m;
}

// ── Decode / Encode ───────────────────────────────────────────────────────────

DramAddress decode_physical_addr(uint64_t pa, const DramAddressMap& map) {
    DramAddress d;
    d.channel = extract_bits(pa, map.channel);
    d.bank    = extract_bits(pa, map.bank);
    d.row     = extract_bits(pa, map.row);
    d.col     = extract_bits(pa, map.col);
    return d;
}

uint64_t encode_dram_addr(const DramAddress& addr, const DramAddressMap& map) {
    uint64_t pa = 0;
    pa = insert_bits(pa, addr.channel, map.channel);
    pa = insert_bits(pa, addr.bank,    map.bank);
    pa = insert_bits(pa, addr.row,     map.row);
    pa = insert_bits(pa, addr.col,     map.col);
    return pa;
}

// ── Print ─────────────────────────────────────────────────────────────────────

void print_dram_address(uint64_t pa, const DramAddress& d) {
    std::cout << "  PA=0x" << std::hex << pa << std::dec
              << "  channel=" << d.channel
              << "  bank="    << d.bank
              << "  row="     << d.row
              << "  col="     << d.col << "\n";
}

// ── Validation ────────────────────────────────────────────────────────────────

bool validate_address_map(const DramAddressMap& map) {
    bool ok = true;

    // Check each field's bit width is sufficient for the declared topology count.
    struct { const char* name; const AddrBitField& f; uint32_t count; } fields[] = {
        { "channel", map.channel, map.topology.num_channels },
        { "bank",    map.bank,    map.topology.num_banks    },
        { "row",     map.row,     map.topology.num_rows     },
        { "col",     map.col,     map.topology.num_cols     },
    };
    for (const auto& fd : fields) {
        if (fd.f.num_bits < 0) {
            std::cerr << "[WARN] " << fd.name << ": negative num_bits\n";
            ok = false;
            continue;
        }
        if (fd.count > 1 && fd.f.num_bits == 0) {
            std::cerr << "[WARN] " << fd.name << ": topology count=" << fd.count
                      << " but num_bits=0 (no bits allocated)\n";
            ok = false;
            continue;
        }
        if (fd.f.num_bits > 0) {
            const uint32_t addressable = 1u << fd.f.num_bits;
            if (addressable < fd.count) {
                std::cerr << "[WARN] " << fd.name << ": " << fd.f.num_bits
                          << " bit(s) can address " << addressable
                          << " units but topology says " << fd.count << "\n";
                ok = false;
            }
        }
    }

    // Check for overlapping bit ranges between fields.
    // Build a bitmask of used PA bits for each field and check for intersections.
    auto bit_mask = [](const AddrBitField& f) -> uint64_t {
        if (f.num_bits <= 0) return 0;
        return ((1ULL << f.num_bits) - 1ULL) << f.start_bit;
    };

    struct { const char* name; uint64_t mask; } masks[] = {
        { "channel", bit_mask(map.channel) },
        { "bank",    bit_mask(map.bank)    },
        { "row",     bit_mask(map.row)     },
        { "col",     bit_mask(map.col)     },
    };
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = i + 1; j < 4; ++j) {
            if (masks[i].mask & masks[j].mask) {
                std::cerr << "[WARN] " << masks[i].name << " and " << masks[j].name
                          << " share overlapping PA bits\n";
                ok = false;
            }
        }
    }

    return ok;
}
