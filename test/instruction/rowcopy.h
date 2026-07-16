#pragma once
#include "cud/interface.h"
#include "../test_config.h"

// Instruction-level ROWCOPY test: builds ROWCOPY_SRC + ROWCOPY_DST + END
// directly from internal primitives without going through the public DataCopy API.
void run_rowcopy_test(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg);
