#pragma once
#include "cud/interface.h"
#include "../test_config.h"

// Instruction-level MAJ3 test: writes pattern_a/b and bias directly to the
// fixed compute rows, then issues a single MAJ3 instruction and reads back
// the frac row.  Tests both AND (bias=0) and OR (bias=1) cases.
void run_maj3_test(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg);
