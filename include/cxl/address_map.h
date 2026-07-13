#pragma once
#include <cstdint>

// ── Bit-field descriptor ──────────────────────────────────────────────────────
// Describes a contiguous slice of a 64-bit physical address.
// Set num_bits = 0 when the dimension has only one unit (e.g. single channel).
struct AddrBitField {
    int start_bit = 0;
    int num_bits  = 0;
};

// ── DRAM topology ─────────────────────────────────────────────────────────────
// Fill in the actual counts for your CXL device.
// num_banks = bank_groups × banks_per_group (total banks per channel).
// num_cols  = cache-line-addressed columns per row.
struct DramTopology {
    uint32_t num_channels = 1;
    uint32_t num_banks    = 16;
    uint32_t num_rows     = 65536;
    uint32_t num_cols     = 128;
};

// ── Physical-address → DRAM mapping ──────────────────────────────────────────
// Each field is a contiguous bit slice of the physical address.
// Bits not covered by any field are ignored (e.g. byte offset within a cache
// line typically occupies PA[5:0] and is left unmapped).
//
// Example (DDR5-like, 1 channel):
//   PA[5:0]  = byte offset (ignored)
//   PA[12:6] = col   (7 bits → 128 columns)
//   PA[16:13]= bank  (4 bits → 16 banks)
//   PA[32:17]= row   (16 bits → 65536 rows)
//
// Edit default_dram_address_map() in src/cxl/address_map.cpp
// to match your device.
struct DramAddressMap {
    DramTopology topology;

    AddrBitField col;      // column index (cache-line addressed)
    AddrBitField bank;     // bank (or combined bank-group + bank)
    AddrBitField row;      // row
    AddrBitField channel;  // channel select; num_bits=0 → single channel
};

// ── Decoded DRAM address ──────────────────────────────────────────────────────
struct DramAddress {
    uint32_t channel = 0;
    uint32_t bank    = 0;
    uint32_t row     = 0;
    uint32_t col     = 0;
};

// Returns the default example map; edit the body in src/cxl/address_map.cpp
// to match your CXL device's actual address scheme.
DramAddressMap default_dram_address_map();

// Decode a physical address into DRAM components.
DramAddress decode_physical_addr(uint64_t pa, const DramAddressMap& map);

// Encode DRAM components back to a physical address (inverse of decode).
// Bits that are not part of any field are left as zero.
uint64_t encode_dram_addr(const DramAddress& addr, const DramAddressMap& map);

// Print one decoded address line to stdout.
void print_dram_address(uint64_t pa, const DramAddress& addr);

// Validate that the map's topology is consistent with its bit fields.
// Prints warnings for mismatches and overlapping bit ranges.
// Returns true if all checks pass.
bool validate_address_map(const DramAddressMap& map);
