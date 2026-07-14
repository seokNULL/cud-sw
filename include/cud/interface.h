#pragma once
#include <cstdint>
#include <vector>
#include "cxl/mem.h"
#include "cxl/io.h"
#include "cud/instruction.h"

// ── Device initialisation ─────────────────────────────────────────────────────

// Open the first available CXL device: CxlMem via DAX, CxlIo via PCI BAR.
// Prints a diagnostics message and returns false on any failure.
bool CxlInit(CxlMem& mem, CxlIo& io);

// ── CUD software interface ────────────────────────────────────────────────────
// Sits on top of CxlMem (CXL.mem data path) and CxlIo (CXL.io register path).
//
// Typical flow:
//   cud_write_row()  → load input data into CXL.mem
//   CudExecute()     → write instruction list to CXL.io BAR and poll for done
//   cud_read_row()   → retrieve result from CXL.mem

// ── 1. Write input data (CXL.mem) ────────────────────────────────────────────

// Write a uniform 64-bit pattern to every column of the given (bank, row).
// Flushes all written cache lines to CXL.mem before returning.
void cud_write_row(CxlMem& mem,
                   uint32_t bank,
                   uint32_t row,
                   uint64_t pattern);

// Per-column variant.  patterns[col] is written to column col.
// patterns.size() must equal NUM_COL.
void cud_write_row(CxlMem& mem,
                   uint32_t bank,
                   uint32_t row,
                   const std::vector<uint64_t>& patterns);

// ── 2. Execute CUD instructions (CXL.io) ────────────────────────────────────

// Write the instruction list to the CXL.io BAR starting at inst_base,
// then spin on status_reg until (value & done_mask) != 0.
// Returns true on success, false on timeout.
bool CudExecute(CxlIo&                      io,
                const std::vector<CudInst>& insts,
                uint64_t inst_base   = 0x0000,
                uint64_t status_reg  = 0x0010,
                uint32_t done_mask   = 0x1,
                uint64_t timeout_us  = 1'000'000);

// ── 3. Read result data (CXL.mem) ────────────────────────────────────────────

// Invalidate CPU cache for all columns of (bank, row), then read each column.
// Returns a vector of NUM_COL uint64_t values in column order (col 0 first).
std::vector<uint64_t> cud_read_row(CxlMem& mem,
                                   uint32_t bank,
                                   uint32_t row);
