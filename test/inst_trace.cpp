#include "inst_trace.h"
#include "cud/instruction.h"

#include <cstdio>
#include <cstdint>
#include <vector>

// ── Decoded instruction ───────────────────────────────────────────────────────

struct DecodedInst {
    uint32_t opcode;
    uint32_t bank;   // (BA<<2)|BG
    uint32_t row;
    // MAJ3 fields
    uint32_t frac;
    uint32_t mode;
    // ROWCOPY_DST field
    bool     last;
};

static DecodedInst decode(CudInst inst) {
    DecodedInst d{};
    d.opcode = (inst & CUD_OPCODE_MASK) >> CUD_OPCODE_SHIFT;
    const uint32_t ba = (inst & CUD_BA_MASK) >> CUD_BA_SHIFT;
    const uint32_t bg = (inst & CUD_BG_MASK) >> CUD_BG_SHIFT;
    d.bank   = (ba << 2) | bg;
    d.row    = inst & CUD_ROW_MASK;
    d.frac   = (inst & CUD_FRAC_MASK) >> CUD_FRAC_SHIFT;
    d.mode   = (inst & CUD_MODE_MASK) >> CUD_MODE_SHIFT;
    d.last   = (inst & CUD_LAST_MASK) != 0;
    return d;
}

static const char* opcode_name(uint32_t op) {
    switch (op) {
    case CUD_OP_END:         return "END";
    case CUD_OP_ROWCOPY_SRC: return "ROWCOPY_SRC";
    case CUD_OP_ROWCOPY_DST: return "ROWCOPY_DST";
    case CUD_OP_MAJ3:        return "MAJ3";
    case CUD_OP_MAJ5_FRONT:  return "MAJ5_FRONT";
    case CUD_OP_MAJ5_BACK:   return "MAJ5_BACK";
    case CUD_OP_MB_ENTRY:    return "MB_ENTRY";
    case CUD_OP_MB_EXIT:     return "MB_EXIT";
    default:                 return "UNKNOWN";
    }
}

// ── Printer ───────────────────────────────────────────────────────────────────

void print_inst_trace(const std::vector<CudInst>& insts,
                      const char* label,
                      TraceOptions opts)
{
    if (label)
        printf("── inst trace: %s (%zu insts) ──────────────────────────────\n",
               label, insts.size());

    // Per-opcode counters for summary
    uint32_t cnt_copy = 0, cnt_maj3 = 0, cnt_end = 0, cnt_other = 0;

    // For consecutive-duplicate detection
    uint32_t prev_src_bank = UINT32_MAX, prev_src_row = UINT32_MAX;

    size_t i = 0;
    while (i < insts.size()) {
        const CudInst raw = insts[i];
        const DecodedInst d = decode(raw);

        // Pair ROWCOPY_SRC + ROWCOPY_DST on one line
        if (d.opcode == CUD_OP_ROWCOPY_SRC && i + 1 < insts.size()) {
            const DecodedInst d2 = decode(insts[i + 1]);
            if (d2.opcode == CUD_OP_ROWCOPY_DST) {
                bool dupe = opts.show_dupes &&
                            d.bank == prev_src_bank && d.row == prev_src_row;
                if (opts.show_hex)
                    printf("[%04zu]  %08X %08X  ", i, raw, insts[i + 1]);
                else
                    printf("[%04zu]  ", i);
                printf("COPY  bk%u:r%-6u  ←  bk%u:r%u",
                       d2.bank, d2.row, d.bank, d.row);
                if (dupe) printf("  *** dup src");
                printf("\n");
                prev_src_bank = d.bank;
                prev_src_row  = d.row;
                ++cnt_copy;
                i += 2;
                continue;
            }
        }

        // All other instructions printed individually
        if (opts.show_hex)
            printf("[%04zu]  %08X            ", i, raw);
        else
            printf("[%04zu]  ", i);

        switch (d.opcode) {
        case CUD_OP_END:
            printf("END\n");
            ++cnt_end;
            break;
        case CUD_OP_MAJ3:
            printf("MAJ3  bk%u:r%u  frac=%u mode=%u\n",
                   d.bank, d.row, d.frac, d.mode);
            ++cnt_maj3;
            break;
        case CUD_OP_ROWCOPY_SRC:
            printf("ROWCOPY_SRC  bk%u:r%u  (unpaired)\n", d.bank, d.row);
            ++cnt_other;
            break;
        case CUD_OP_ROWCOPY_DST:
            printf("ROWCOPY_DST  bk%u:r%u  last=%d  (unpaired)\n",
                   d.bank, d.row, d.last);
            ++cnt_other;
            break;
        default:
            printf("%-12s  bk%u:r%u\n", opcode_name(d.opcode), d.bank, d.row);
            ++cnt_other;
            break;
        }
        prev_src_bank = UINT32_MAX;
        prev_src_row  = UINT32_MAX;
        ++i;
    }

    if (opts.show_summary) {
        const size_t total = insts.size();
        printf("── summary: %zu insts  COPY×%u  MAJ3×%u  END×%u",
               total, cnt_copy, cnt_maj3, cnt_end);
        if (cnt_other) printf("  other×%u", cnt_other);
        // ROWCOPY pairs account for 2 instructions each
        const uint32_t rowcopy_insts = cnt_copy * 2;
        if (total > 0)
            printf("  (COPY%.0f%%  MAJ3%.0f%%)",
                   100.0 * rowcopy_insts / total,
                   100.0 * cnt_maj3 / total);
        printf("\n");
    }
}
