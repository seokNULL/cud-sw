#pragma once
#include "cud/interface.h"
#include "../test_config.h"

// Logical-level AND/OR tests via the CudAnd / CudOr public API
// (full pipeline: ROWCOPY to compute rows → MAJ3 → ROWCOPY result to dst).
void run_logical_tests(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg);
