#pragma once
#include <cstdint>
#include <vector>

// ── DRAM topology ─────────────────────────────────────────────────────────────
// Edit these to match your CXL device.

#define NUM_CH   1         // number of channels
#define NUM_BK   16        // banks per channel
#define NUM_ROW  65536     // rows per bank
#define NUM_COL  128       // columns per row (cache-line addressed)

// ── Physical address → DRAM bit mapping ──────────────────────────────────────
// Each macro lists PA bit positions that form one DRAM field,
// from the field's LSB (first entry) to its MSB (last entry).
//
// Bits may be non-contiguous and appear in any order, so split fields are
// fully supported. Leave a macro empty when that dimension has only one unit.
//
// Example — column split across two PA regions
//   (lower 3 bits at PA[2:0], upper 7 bits at PA[12:6]):
//
//   #define ADDR_COL_BITS  0, 1, 2, 6, 7, 8, 9, 10, 11, 12
//
// Example below: DDR5-like, single channel, contiguous fields.
//   PA[ 5: 0] = byte offset (not mapped)
//   PA[12: 6] = col  [6:0]
//   PA[16:13] = bank [3:0]
//   PA[32:17] = row [15:0]

#define ADDR_COL_BITS   6, 7, 8, 9, 10, 11, 12
#define ADDR_BK_BITS    13, 14, 15, 16
#define ADDR_ROW_BITS   17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32
#define ADDR_CH_BITS    /* single channel — leave empty */

// ── Result type ───────────────────────────────────────────────────────────────
struct DramAddress {
    uint32_t channel = 0;
    uint32_t bank    = 0;
    uint32_t row     = 0;
    uint32_t col     = 0;
};

// Decode a physical address into DRAM components using the mapping above.
DramAddress decode_physical_addr(uint64_t pa);

// Encode DRAM components back to a physical address (inverse of decode).
uint64_t encode_dram_addr(const DramAddress& addr);

// Print one decoded address line to stdout.
void print_dram_address(uint64_t pa, const DramAddress& addr);

// Validate the mapping defined in this header:
//   - bit-count sufficient for each topology count
//   - no PA bit assigned to more than one field
// Prints warnings and returns false on any violation.
bool validate_address_map();

// ── Low-level helpers (exposed for custom / split-field testing) ──────────────
// Extract a DRAM field value from a PA using an arbitrary list of bit positions.
// bit_positions[0] → field bit 0 (LSB), bit_positions[n-1] → field bit n-1 (MSB).
uint32_t extract_pa_field(uint64_t pa, const std::vector<int>& bit_positions);

// Insert a DRAM field value into a PA at the given bit positions.
uint64_t insert_pa_field(uint64_t pa, uint32_t value, const std::vector<int>& bit_positions);
