#pragma once
#include "cud/interface.h"

// CPU vs CUD performance benchmark for XOR / ADD / MUL (W = 1, 2, 4, 8).
// Breakdown: CXL.mem write | inst-gen | CXL.io exec | CXL.mem read
void run_benchmark(CxlMem& mem, CxlIo& io);
