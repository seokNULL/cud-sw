#include "cud/interface.h"
#include "test/test_config.h"
#include "test/fault_search.h"
#include "test/cud_compute.h"

#include <iostream>
#include <random>

void run_cxl_enum();
void run_cxl_addr_map();
void run_cxl_io();
void run_cxl_mem();
void run_cud_interface_tests(CxlMem& mem, CxlIo& io, const CudTestConfig& cfg);

// ── CUD test configuration ────────────────────────────────────────────────────
// Bank/row assignments and register offsets are fixed here.
// copy_pattern, pattern_a, and pattern_b are randomised per test run.
static CudTestConfig make_cud_cfg() {
    std::mt19937_64 rng(std::random_device{}());
    const uint64_t pa = rng();
    const uint64_t pb = rng();
    const uint64_t pc = rng();
    std::cout << "  [pattern] copy=0x" << std::hex << pc
              << "  a=0x" << pa << "  b=0x" << pb << std::dec << "\n";
    return CudTestConfig{
        .bank         = 0,

        .src_row      = 0,
        .dst_row      = 1,

        .row_a        = 0,
        .row_b        = 1,
        .row_bias     = 2,
        .row_dst      = 3,
        .row_zero     = 4,

        .copy_pattern = pc,
        .pattern_a    = pa,
        .pattern_b    = pb,

        .inst_base    = 0x0000,
        .status_reg   = 0x0000,
        .done_mask    = 0x1,
    };
}

// ── CUD sub-menu ──────────────────────────────────────────────────────────────

static void print_cud_menu() {
    std::cout << "\n  [1]  Fault Row Search\n"
              << "  [2]  ROWCOPY\n"
              << "  [3]  MAJ3\n"
              << "  [4]  AND / OR\n"
              << "  [5]  DataCopy\n"
              << "  [6]  XOR\n"
              << "  [7]  ADD (1-8 bit)\n"
              << "  [8]  MUL (1-4 bit)\n"
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
        case 1: run_fault_search(mem, io); break;
        case 2: { const CudTestConfig cfg = make_cud_cfg();
                  run_rowcopy_test(mem, io, cfg);      break; }
        case 3: { const CudTestConfig cfg = make_cud_cfg();
                  run_maj3_test(mem, io, cfg);          break; }
        case 4: { const CudTestConfig cfg = make_cud_cfg();
                  run_logical_tests(mem, io, cfg);      break; }
        case 5: { const CudTestConfig cfg = make_cud_cfg();
                  run_cud_interface_tests(mem, io, cfg); break; }
        case 6: { const CudTestConfig cfg = make_cud_cfg();
                  run_xor_test(mem, io, cfg);            break; }
        case 7: { const CudTestConfig cfg = make_cud_cfg();
                  run_add_test(mem, io, cfg);            break; }
        case 8: { const CudTestConfig cfg = make_cud_cfg();
                  run_mul_test(mem, io, cfg);            break; }
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
