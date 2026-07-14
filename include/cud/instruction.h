#pragma once
#include <cstdint>

// ── CUD instruction type (32-bit) ─────────────────────────────────────────────
using CudInst = uint32_t;

// ── Row size ─────────────────────────────────────────────────────────────────
// One CUD operation unit: NUM_COL(1024) columns × 8 bytes = 8 KiB.
#define CUD_ROW_SIZE_BYTES  8192u

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
#define CUD_FIELD_BA(ba)      (((uint32_t)(ba))   << CUD_BA_SHIFT)
#define CUD_FIELD_BG(bg)      (((uint32_t)(bg))   << CUD_BG_SHIFT)
#define CUD_FIELD_ROW(row)    ( (uint32_t)(row)   &  CUD_ROW_MASK)
#define CUD_FIELD_LAST        (1u                 << CUD_LAST_SHIFT)
#define CUD_FIELD_FRAC(frac)  (((uint32_t)(frac)) << CUD_FRAC_SHIFT)
#define CUD_FIELD_MB_NUM(n)   ( (uint32_t)(n)     &  CUD_MB_NUM_MASK)

// ── High-level kernel builders ────────────────────────────────────────────────
// Validate inputs, build the complete instruction list (including END), and
// return it ready for CudExecute(). These are the public API for instruction
// generation; the compute_lib primitives are internal implementation details.

#include <cstddef>
#include <vector>

// Row-to-row data copy: size_bytes must be a non-zero multiple of
// CUD_ROW_SIZE_BYTES; src and dst must be in the same bank.
std::vector<CudInst> CudDataCopy(uint64_t src_pa, uint64_t dst_pa,
                                  size_t size_bytes);

// Bitwise AND via MAJ3: dst = a AND b = MAJ3(a, b, 0).
// zero_pa must point to a row pre-filled with all zeros; all PAs same bank.
std::vector<CudInst> CudAnd(uint64_t a_pa, uint64_t b_pa,
                             uint64_t zero_pa, uint64_t dst_pa);

// Bitwise OR via MAJ3: dst = a OR b = MAJ3(a, b, 1).
// one_pa must point to a row pre-filled with all ones; all PAs same bank.
std::vector<CudInst> CudOr(uint64_t a_pa, uint64_t b_pa,
                            uint64_t one_pa, uint64_t dst_pa);
