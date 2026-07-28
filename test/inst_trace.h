#pragma once
#include "cud/instruction.h"
#include <vector>

// Options for print_inst_trace.
struct TraceOptions {
    bool show_hex     = false;  // print raw 32-bit hex alongside each decoded line
    bool show_summary = true;   // print opcode count summary at end
    bool show_dupes   = true;   // flag consecutive duplicate src rows
};

// Decode and print a CUD instruction sequence to stdout.
// Each ROWCOPY_SRC is paired with the following ROWCOPY_DST on one line.
// label is printed as a section header when non-null.
void print_inst_trace(const std::vector<CudInst>& insts,
                      const char* label = nullptr,
                      TraceOptions opts = {});
