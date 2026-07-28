#pragma once
#include "cud/instruction.h"
#include <vector>

// Options for dump_inst_trace.
struct TraceOptions {
    bool show_hex     = false;  // write raw 32-bit hex alongside each decoded line
    bool show_summary = true;   // write opcode count summary at end
    bool show_dupes   = true;   // flag consecutive duplicate src rows
};

// Decode a CUD instruction sequence and write it to `path`.
// Each ROWCOPY_SRC is paired with the following ROWCOPY_DST on one line.
// label is written as a section header when non-null.
// Prints "  [trace] written to <path>" to stdout on success,
// or a warning to stdout on failure (non-fatal).
void dump_inst_trace(const std::vector<CudInst>& insts,
                     const char* path,
                     const char* label = nullptr,
                     TraceOptions opts = {});
