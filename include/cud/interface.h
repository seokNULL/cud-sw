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
// Address-to-physical mapping comes from include/cxl/address_map.h.
//
// Typical flow:
//   cud_write_row()          → load input into CXL.mem
//   cud_write_instructions() → push instruction list into CXL.io BAR
//   cud_poll_done()          → wait for CUD completion
//   cud_read_row()           → retrieve result from CXL.mem

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

// ── 2. Write CUD instructions (CXL.io) ───────────────────────────────────────

// Write instructions sequentially to the CXL.io BAR starting at base_offset.
// Each CudInst is written as a 64-bit word; index 0 goes to base_offset,
// index 1 to base_offset+8, and so on.
void cud_write_instructions(CxlIo& io,
                            uint64_t base_offset,
                            const std::vector<CudInst>& insts);

// ── 3. Poll for CUD completion (CXL.io) ──────────────────────────────────────

// Read the 32-bit register at status_offset and spin until
// (value & done_mask) != 0.
// Returns true when done, false if timeout_us microseconds elapse first.
bool cud_poll_done(CxlIo& io,
                   uint64_t status_offset,
                   uint32_t done_mask   = 0x1,
                   uint64_t timeout_us  = 1'000'000);

// ── 4. Read result data (CXL.mem) ────────────────────────────────────────────

// Invalidate CPU cache for all columns of (bank, row), then read each column.
// Returns a vector of NUM_COL uint64_t values in column order (col 0 first).
std::vector<uint64_t> cud_read_row(CxlMem& mem,
                                   uint32_t bank,
                                   uint32_t row);
