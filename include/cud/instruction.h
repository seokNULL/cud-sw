#pragma once
#include <cstdint>
#include <vector>

// ── CUD instruction type (32-bit) ─────────────────────────────────────────────
using CudInst = uint32_t;

// ── Opcode values — bits [31:29] ─────────────────────────────────────────────
#define CUD_OP_END          0x0u
#define CUD_OP_ROWCOPY_SRC  0x1u
#define CUD_OP_ROWCOPY_DST  0x2u
#define CUD_OP_MAJ3         0x3u
#define CUD_OP_MAJ5_FRONT   0x4u
#define CUD_OP_MAJ5_BACK    0x5u
#define CUD_OP_MB_ENTRY     0x6u
#define CUD_OP_MB_EXIT      0x7u

// ── Bit-field positions and masks ─────────────────────────────────────────────
#define CUD_OPCODE_SHIFT    29u
#define CUD_OPCODE_MASK     (0x7u  << CUD_OPCODE_SHIFT)  // bits [31:29]

#define CUD_ROW_MASK        0x1FFFFu                      // bits [16: 0]

#define CUD_BA_SHIFT        19u
#define CUD_BA_MASK         (0x3u  << CUD_BA_SHIFT)      // bits [20:19]

#define CUD_BG_SHIFT        17u
#define CUD_BG_MASK         (0x3u  << CUD_BG_SHIFT)      // bits [18:17]

// rowcopy_dst only
#define CUD_LAST_SHIFT      21u
#define CUD_LAST_MASK       (0x1u  << CUD_LAST_SHIFT)    // bit  [21]

// maj3 only
#define CUD_FRAC_SHIFT      24u
#define CUD_FRAC_MASK       (0x3u  << CUD_FRAC_SHIFT)    // bits [25:24]

// mb_entry only
#define CUD_MB_NUM_MASK     0x3u                          // bits [ 1: 0]

// ── bank[3:0] → BA / BG (bank[3:2]=BA, bank[1:0]=BG) ───────────────────────
#define CUD_BANK_TO_BA(bank)  (((uint32_t)(bank) >> 2) & 0x3u)
#define CUD_BANK_TO_BG(bank)  ( (uint32_t)(bank)       & 0x3u)

// ── Field-insert macros ───────────────────────────────────────────────────────
#define CUD_FIELD_OPCODE(op)  (((uint32_t)(op))   << CUD_OPCODE_SHIFT)
#define CUD_FIELD_BG(bg)      (((uint32_t)(bg))   << CUD_BG_SHIFT)
#define CUD_FIELD_BA(ba)      (((uint32_t)(ba))   << CUD_BA_SHIFT)
#define CUD_FIELD_ROW(row)    ( (uint32_t)(row)   &  CUD_ROW_MASK)
#define CUD_FIELD_LAST        (1u                 << CUD_LAST_SHIFT)
#define CUD_FIELD_FRAC(frac)  (((uint32_t)(frac)) << CUD_FRAC_SHIFT)
#define CUD_FIELD_MB_NUM(n)   ( (uint32_t)(n)     &  CUD_MB_NUM_MASK)

// ── Instruction generators ────────────────────────────────────────────────────

// Append an END instruction (opcode only, no payload).
void append_end(std::vector<CudInst>& insts);

// Append ROWCOPY_SRC + ROWCOPY_DST for a single row-to-row copy.
// Extracts BG, BA, row from each physical address via decode_physical_addr().
// last bit in ROWCOPY_DST is set to 1 (single-destination copy).
void append_rowcopy(uint64_t src_pa, uint64_t dst_pa,
                    std::vector<CudInst>& insts);

// Append a MAJ3 instruction.
// frac_pos [1:0]: fractional position field (bits [25:24]).
void append_maj3(uint64_t pa, uint32_t frac_pos,
                 std::vector<CudInst>& insts);

// Append MULTI_BANK_ENTRY; num_banks [1:0] = number of banks acting together.
void append_mb_entry(uint32_t num_banks, std::vector<CudInst>& insts);

// Append MULTI_BANK_EXIT (opcode only, no payload).
void append_mb_exit(std::vector<CudInst>& insts);
