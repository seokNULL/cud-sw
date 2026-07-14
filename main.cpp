#include "cud/interface.h"
#include "test/test_config.h"

#include <iostream>

void run_cxl_enum();
void run_cxl_addr_map();
void run_cxl_io();
void run_cxl_mem();
void run_cud_interface_tests(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg);
void run_cud_library_tests(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg);

// ── CUD test configuration ────────────────────────────────────────────────────
// Edit this block to change bank/row assignments, input patterns, and register
// offsets without touching any test file.
//
//   Interface test (DataCopy) uses: bank, src_row, dst_row, copy_pattern
//   Library tests  (AND, OR)  use:  bank, row_a, row_b, row_bias, row_dst,
//                                   pattern_a, pattern_b
static const CudTestConfig kCudCfg = {
    .bank         = 0,

    .src_row      = 0,
    .dst_row      = 1,

    .row_a        = 0,
    .row_b        = 1,
    .row_bias     = 2,
    .row_dst      = 3,

    .copy_pattern = 0xDEADBEEFCAFEBABEULL,
    .pattern_a    = 0xAAAAAAAAAAAAAAAAULL,
    .pattern_b    = 0xCCCCCCCCCCCCCCCCULL,

    .inst_base    = 0x0000,
    .status_reg   = 0x0000,
    .done_mask    = 0x1,
};

// ── CUD sub-menu ──────────────────────────────────────────────────────────────

static void print_cud_menu() {
    std::cout << "\n  -- CUD API Verification --\n"
              << "  [1]  Interface  (DataCopy: Write / Execute / Read)\n"
              << "  [2]  Library    (AND, OR)\n"
              << "  [0]  Back\n"
              << "  Select: ";
}

static void run_cud_tests() {
    CxlMem mem;
    CxlIo  io;
    if (!CxlInit(mem, io)) return;

    int choice;
    do {
        print_cud_menu();
        std::cin >> choice;
        switch (choice) {
        case 1: run_cud_interface_tests(mem, io, kCudCfg); break;
        case 2: run_cud_library_tests(mem, io, kCudCfg);   break;
        case 0: break;
        default: std::cout << "  Invalid selection.\n"; break;
        }
    } while (choice != 0);
}

// ── Top-level menu ────────────────────────────────────────────────────────────

static void print_menu() {
    std::cout << "\n===== CXL / CUD Test Suite =====\n"
              << "\n  -- CXL Verification --\n"
              << "  [1]  Device discovery\n"
              << "  [2]  Address map decode/encode\n"
              << "  [3]  CXL.io BAR register read\n"
              << "  [4]  CXL.mem DAX read/write\n"
              << "\n  -- CUD API Verification --\n"
              << "  [5]  CUD operations\n"
              << "\n  [0]  Exit\n"
              << "Select: ";
}

int main() {
    int choice;
    do {
        print_menu();
        std::cin >> choice;
        switch (choice) {
        case 1: run_cxl_enum();     break;
        case 2: run_cxl_addr_map(); break;
        case 3: run_cxl_io();       break;
        case 4: run_cxl_mem();      break;
        case 5: run_cud_tests();    break;
        case 0: std::cout << "EXIT.\n"; break;
        default: std::cout << "Invalid selection.\n"; break;
        }
    } while (choice != 0);
    return 0;
}
