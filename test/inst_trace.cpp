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

// ── File writer ───────────────────────────────────────────────────────────────

void dump_inst_trace(const std::vector<CudInst>& insts,
                     const char* path,
                     const char* label,
                     TraceOptions opts)
{
    FILE* f = fopen(path, "w");
    if (!f) {
        printf("  [trace] WARNING: could not open '%s' for writing\n", path);
        return;
    }

    if (label)
        fprintf(f, "── inst trace: %s (%zu insts) ──────────────────────────────\n",
                label, insts.size());

    uint32_t cnt_copy = 0, cnt_maj3 = 0, cnt_end = 0, cnt_other = 0;
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
                    fprintf(f, "[%04zu]  %08X %08X  ", i, raw, insts[i + 1]);
                else
                    fprintf(f, "[%04zu]  ", i);
                fprintf(f, "COPY  bk%u:r%-6u  ←  bk%u:r%u",
                        d2.bank, d2.row, d.bank, d.row);
                if (dupe) fprintf(f, "  *** dup src");
                fprintf(f, "\n");
                prev_src_bank = d.bank;
                prev_src_row  = d.row;
                ++cnt_copy;
                i += 2;
                continue;
            }
        }

        // All other instructions written individually
        if (opts.show_hex)
            fprintf(f, "[%04zu]  %08X            ", i, raw);
        else
            fprintf(f, "[%04zu]  ", i);

        switch (d.opcode) {
        case CUD_OP_END:
            fprintf(f, "END\n");
            ++cnt_end;
            break;
        case CUD_OP_MAJ3:
            fprintf(f, "MAJ3  bk%u:r%u  frac=%u mode=%u\n",
                    d.bank, d.row, d.frac, d.mode);
            ++cnt_maj3;
            break;
        case CUD_OP_ROWCOPY_SRC:
            fprintf(f, "ROWCOPY_SRC  bk%u:r%u  (unpaired)\n", d.bank, d.row);
            ++cnt_other;
            break;
        case CUD_OP_ROWCOPY_DST:
            fprintf(f, "ROWCOPY_DST  bk%u:r%u  last=%d  (unpaired)\n",
                    d.bank, d.row, d.last);
            ++cnt_other;
            break;
        default:
            fprintf(f, "%-12s  bk%u:r%u\n", opcode_name(d.opcode), d.bank, d.row);
            ++cnt_other;
            break;
        }
        prev_src_bank = UINT32_MAX;
        prev_src_row  = UINT32_MAX;
        ++i;
    }

    if (opts.show_summary) {
        const size_t total = insts.size();
        fprintf(f, "── summary: %zu insts  COPY×%u  MAJ3×%u  END×%u",
                total, cnt_copy, cnt_maj3, cnt_end);
        if (cnt_other) fprintf(f, "  other×%u", cnt_other);
        const uint32_t rowcopy_insts = cnt_copy * 2;
        if (total > 0)
            fprintf(f, "  (COPY%.0f%%  MAJ3%.0f%%)",
                    100.0 * rowcopy_insts / total,
                    100.0 * cnt_maj3 / total);
        fprintf(f, "\n");
    }

    fclose(f);
    printf("  [trace] written to %s\n", path);
}
