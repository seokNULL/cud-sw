#pragma once
#include "../include/cud/interface.h"

// Scan all banks for rows that CUD DataCopy cannot write to (repaired/faulty).
// Results are written to a timestamped CSV: bank,mat,row
void run_fault_search(CxlMem& mem, CxlIo& io);
