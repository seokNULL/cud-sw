#pragma once
#include <cstdint>
#include <vector>

// ── DRAM topology ─────────────────────────────────────────────────────────────
// Edit these to match your CXL device.

#define NUM_CH   1          // number of channels
#define NUM_BK   16         // banks per channel
#define NUM_ROW  128 * 1024 // rows per bank
#define NUM_COL  1024       // columns per row (cache-line addressed)

// ── Physical address → DRAM bit mapping ──────────────────────────────────────
//   PA[ 2: 0] = byte offset (not mapped)
//   PA[ 5: 3] = col [2:0]
//   PA[ 9: 6] = bank [3:0]
//   PA[16:10] = col [9:3]
//   PA[33:17] = row [16:0]

#define ADDR_COL_BITS   3, 4, 5, 10, 11, 12, 13, 14, 15, 16
#define ADDR_BK_BITS    6, 7, 8, 9
#define ADDR_ROW_BITS   17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33
#define ADDR_CH_BITS    /* single channel — leave empty */

// ── Result type ───────────────────────────────────────────────────────────────
struct DramAddress {
    uint32_t channel = 0;
    uint32_t bank    = 0;
    uint32_t row     = 0;
    uint32_t col     = 0;
};

DramAddress decode_physical_addr(uint64_t pa);
uint64_t encode_dram_addr(const DramAddress& addr);
void print_dram_address(uint64_t pa, const DramAddress& addr);
uint32_t extract_pa_field(uint64_t pa, const std::vector<int>& bit_positions);
uint64_t insert_pa_field(uint64_t pa, uint32_t value, const std::vector<int>& bit_positions);
bool validate_address_map();
