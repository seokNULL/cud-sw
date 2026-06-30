#include "include/cud/ops.h"
#include "include/cud/types.h"
#include "include/cxl/device.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static constexpr uint32_t DEMO_BANK = 0;

static std::vector<uint64_t> make_addr_vec(uint32_t bank,
                                           const std::vector<uint32_t>& rows) {
    std::vector<uint64_t> addrs;
    addrs.reserve(rows.size());
    for (uint32_t r : rows) addrs.push_back(make_row_addr(bank, r));
    return addrs;
}

static void print_menu() {
    std::cout << "\n===== CuD Operation Menu =====\n"
              << "  [1]  row_copy               Copy one DRAM row to another\n"
              << "  [2]  row_copy_fan            Fan-out copy (1 src -> 2 dsts)\n"
              << "  [3]  maj3                    Majority-3 gate\n"
              << "  [4]  maj5                    Majority-5 gate\n"
              << "  [5]  op_and                  AND of two rows\n"
              << "  [6]  op_or                   OR of two rows\n"
              << "  [7]  op_xor                  XOR of two rows\n"
              << "  [8]  op_not                  NOT of a row to destination\n"
              << "  [9]  maj5_via_maj3            MAJ5 emulated using MAJ3 gates\n"
              << " [10]  add_new                 Full adder (dual-MAT output)\n"
              << " [11]  add_4bit                4-bit ripple-carry adder\n"
              << " [12]  add_nbit                N-bit ripple-carry adder\n"
              << " [13]  add_nbit_via_maj3        N-bit adder using only MAJ3\n"
              << " [14]  add_legacy              Legacy in-place adder\n"
              << " [15]  mult_4bit_dual           4x4-bit multiplier (dual-MAT output)\n"
              << " [16]  mult_4bit_opt_dual       Optimized 4x4-bit multiplier (dual-MAT)\n"
              << " [17]  mult_4bit_opt            Optimized 4x4-bit multiplier (MAT0 only)\n"
              << " [18]  mult_4bit_opt_v2         Optimized 4x4-bit multiplier v2\n"
              << " [19]  mult_4bit_csa            4x4-bit multiplier with CSA scheduling\n"
              << " [20]  mult_4bit_csa_via_maj3   CSA 4x4-bit multiplier via MAJ3\n"
              << " [21]  mult_nbit               N-bit multiplier\n"
              << "  [0]  Exit\n"
              << "Select: ";
}

static void try_execute_on_hardware(const std::vector<uint32_t>& uops) {
    char yn;
    std::cout << "Execute on CXL hardware? [y/N]: ";
    std::cin >> yn;
    if (yn != 'y' && yn != 'Y') return;

    std::string bdf, dax_path;
    std::cout << "BDF (e.g. 0000:b8:00.0): ";
    std::cin >> bdf;
    std::cout << "DAX device path (e.g. /dev/dax1.0): ";
    std::cin >> dax_path;

    CxlDeviceConfig cfg;
    cfg.bdf = bdf;
    cfg.dax_path = dax_path;

    CxlDevice dev(cfg);
    if (!dev.init()) {
        std::cout << "[ERROR] CXL init failed: " << dev.last_error() << "\n";
        return;
    }
    if (!dev.execute(uops)) {
        std::cout << "[ERROR] Execute failed: " << dev.last_error() << "\n";
        return;
    }
    std::cout << "[OK] Execution completed.\n";
}

int main() {
    int choice;
    do {
        print_menu();
        std::cin >> choice;

        std::vector<uint32_t> uops;

        switch (choice) {
        case 0:
            std::cout << "Goodbye.\n";
            break;

        case 1: {
            const uint64_t src = make_row_addr(DEMO_BANK, 20);
            const uint64_t dst = make_row_addr(DEMO_BANK, 50);
            std::cout << "\n[row_copy] src=row20 dst=row50 bank=" << DEMO_BANK << "\n";
            uops = row_copy(src, dst);
            break;
        }

        case 2: {
            const uint64_t src  = make_row_addr(DEMO_BANK, 20);
            const uint64_t dst1 = make_row_addr(DEMO_BANK, 50);
            const uint64_t dst2 = make_row_addr(DEMO_BANK, 60);
            std::cout << "\n[row_copy_fan] src=row20 dst1=row50 dst2=row60 bank=" << DEMO_BANK << "\n";
            uops = row_copy_fan(src, dst1, dst2);
            break;
        }

        case 3: {
            // 2-RA group 0 (RA3,RA0): offsets {0,1,8,9}; use 3 of them.
            const uint64_t r0 = make_row_addr(DEMO_BANK, 0);
            const uint64_t r1 = make_row_addr(DEMO_BANK, 1);
            const uint64_t r8 = make_row_addr(DEMO_BANK, 8);
            std::cout << "\n[maj3] rows 0,1,8 bank=" << DEMO_BANK << " (RA3,RA0 group)\n";
            uops = maj3(r0, r1, r8);
            break;
        }

        case 4: {
            // 3-RA group 7 (RA4,RA3,RA0): offsets {0,1,8,9,16,17,24,25}; use 5.
            const uint64_t r0  = make_row_addr(DEMO_BANK, 0);
            const uint64_t r1  = make_row_addr(DEMO_BANK, 1);
            const uint64_t r8  = make_row_addr(DEMO_BANK, 8);
            const uint64_t r9  = make_row_addr(DEMO_BANK, 9);
            const uint64_t r16 = make_row_addr(DEMO_BANK, 16);
            std::cout << "\n[maj5] rows 0,1,8,9,16 bank=" << DEMO_BANK << " (RA4,RA3,RA0 group)\n";
            uops = maj5(r0, r1, r8, r9, r16);
            break;
        }

        case 5: {
            const uint64_t r0 = make_row_addr(DEMO_BANK, 0);
            const uint64_t r8 = make_row_addr(DEMO_BANK, 8);
            std::cout << "\n[op_and] rows 0,8 bank=" << DEMO_BANK << "\n";
            uops = op_and(r0, r8);
            break;
        }

        case 6: {
            const uint64_t r0 = make_row_addr(DEMO_BANK, 0);
            const uint64_t r8 = make_row_addr(DEMO_BANK, 8);
            std::cout << "\n[op_or] rows 0,8 bank=" << DEMO_BANK << "\n";
            uops = op_or(r0, r8);
            break;
        }

        case 7: {
            const uint64_t r0 = make_row_addr(DEMO_BANK, 0);
            const uint64_t r8 = make_row_addr(DEMO_BANK, 8);
            std::cout << "\n[op_xor] rows 0,8 bank=" << DEMO_BANK << "\n";
            uops = op_xor(r0, r8);
            break;
        }

        case 8: {
            const uint64_t src = make_row_addr(DEMO_BANK, 0);
            const uint64_t dst = make_row_addr(DEMO_BANK, 50);
            std::cout << "\n[op_not] src=row0 dst=row50 bank=" << DEMO_BANK << "\n";
            uops = op_not(src, dst);
            break;
        }

        case 9: {
            // 3-RA group 7 (RA4,RA3,RA0) at base 0.
            const uint64_t r0  = make_row_addr(DEMO_BANK, 0);
            const uint64_t r1  = make_row_addr(DEMO_BANK, 1);
            const uint64_t r8  = make_row_addr(DEMO_BANK, 8);
            const uint64_t r9  = make_row_addr(DEMO_BANK, 9);
            const uint64_t r16 = make_row_addr(DEMO_BANK, 16);
            const uint64_t out = make_row_addr(DEMO_BANK, 50);
            std::cout << "\n[maj5_via_maj3] src rows 0,1,8,9,16 -> out row50 bank=" << DEMO_BANK << "\n";
            uops = maj5_via_maj3(r0, r1, r8, r9, r16, out);
            break;
        }

        case 10: {
            // src1/src2/src3 must be in the same MAT; add_new also uses MAT+1 mirrors.
            const uint64_t src1     = make_row_addr(DEMO_BANK, global_row_from_mat(0, 0));
            const uint64_t src2     = make_row_addr(DEMO_BANK, global_row_from_mat(0, 1));
            const uint64_t src3     = make_row_addr(DEMO_BANK, global_row_from_mat(0, 8));
            const uint64_t sum_m0   = make_row_addr(DEMO_BANK, global_row_from_mat(0, 300));
            const uint64_t sum_m1   = make_row_addr(DEMO_BANK, global_row_from_mat(1, 300));
            const uint64_t carry_m0 = make_row_addr(DEMO_BANK, global_row_from_mat(0, 301));
            const uint64_t carry_m1 = make_row_addr(DEMO_BANK, global_row_from_mat(1, 301));
            std::cout << "\n[add_new] src=MAT0:0,1,8 sum=MAT0/1:300 carry=MAT0/1:301 bank=" << DEMO_BANK << "\n";
            uops = add_new(src1, src2, src3, sum_m0, sum_m1, carry_m0, carry_m1);
            break;
        }

        case 11: {
            const std::vector<uint64_t> src1 = make_addr_vec(DEMO_BANK, {0, 1, 2, 3});
            const std::vector<uint64_t> src2 = make_addr_vec(DEMO_BANK, {10, 11, 12, 13});
            const uint64_t cin = make_row_addr(DEMO_BANK, 290);
            const std::vector<uint64_t> sum = make_addr_vec(DEMO_BANK, {
                global_row_from_mat(1, 600), global_row_from_mat(1, 601),
                global_row_from_mat(1, 602), global_row_from_mat(1, 603)
            });
            const uint64_t cout = make_row_addr(DEMO_BANK, global_row_from_mat(1, 604));
            std::cout << "\n[add_4bit] A=rows0-3 B=rows10-13 Cin=row290 Sum=MAT1:600-603 Cout=MAT1:604 bank=" << DEMO_BANK << "\n";
            uops = add_4bit(src1, src2, cin, sum, cout);
            break;
        }

        case 12: {
            uint32_t n;
            std::cout << "Bit width N (1-32): ";
            std::cin >> n;
            if (n == 0 || n > 32) { std::cout << "N must be 1-32.\n"; break; }

            std::vector<uint64_t> src1, src2, sum;
            for (uint32_t i = 0; i < n; ++i) {
                src1.push_back(make_row_addr(DEMO_BANK, global_row_from_mat(0, i)));
                src2.push_back(make_row_addr(DEMO_BANK, global_row_from_mat(0, 100 + i)));
                sum.push_back(make_row_addr(DEMO_BANK, global_row_from_mat(1, 200 + i)));
            }
            const uint64_t cin  = make_row_addr(DEMO_BANK, global_row_from_mat(0, 50));
            const uint64_t cout = make_row_addr(DEMO_BANK, global_row_from_mat(0, 51));
            std::cout << "\n[add_nbit] " << n << "-bit A=MAT0:0.. B=MAT0:100.. Cin=MAT0:50 Sum=MAT1:200.. Cout=MAT0:51 bank=" << DEMO_BANK << "\n";
            uops = add_nbit(src1, src2, cin, sum, cout);
            break;
        }

        case 13: {
            uint32_t n;
            std::cout << "Bit width N (1-32): ";
            std::cin >> n;
            if (n == 0 || n > 32) { std::cout << "N must be 1-32.\n"; break; }

            std::vector<uint64_t> src1, src2, sum;
            for (uint32_t i = 0; i < n; ++i) {
                src1.push_back(make_row_addr(DEMO_BANK, global_row_from_mat(0, i)));
                src2.push_back(make_row_addr(DEMO_BANK, global_row_from_mat(0, 100 + i)));
                sum.push_back(make_row_addr(DEMO_BANK, global_row_from_mat(1, 200 + i)));
            }
            const uint64_t cin  = make_row_addr(DEMO_BANK, global_row_from_mat(0, 50));
            const uint64_t cout = make_row_addr(DEMO_BANK, global_row_from_mat(0, 51));
            std::cout << "\n[add_nbit_via_maj3] " << n << "-bit A=MAT0:0.. B=MAT0:100.. Cin=MAT0:50 Sum=MAT1:200.. Cout=MAT0:51 bank=" << DEMO_BANK << "\n";
            uops = add_nbit_via_maj3(src1, src2, cin, sum, cout);
            break;
        }

        case 14: {
            const uint64_t r0 = make_row_addr(DEMO_BANK, 0);
            const uint64_t r1 = make_row_addr(DEMO_BANK, 1);
            const uint64_t r8 = make_row_addr(DEMO_BANK, 8);
            std::cout << "\n[add_legacy] rows 0,1,8 bank=" << DEMO_BANK << "\n";
            uops = add_legacy(r0, r1, r8);
            break;
        }

        case 15: {
            const auto srcA    = make_addr_vec(DEMO_BANK, {100, 101, 102, 103});
            const auto srcB    = make_addr_vec(DEMO_BANK, {200, 201, 202, 203});
            const auto prod_m0 = make_addr_vec(DEMO_BANK, {
                global_row_from_mat(0, 300), global_row_from_mat(0, 301),
                global_row_from_mat(0, 302), global_row_from_mat(0, 303),
                global_row_from_mat(0, 304), global_row_from_mat(0, 305),
                global_row_from_mat(0, 306), global_row_from_mat(0, 307)
            });
            const auto prod_m1 = make_addr_vec(DEMO_BANK, {
                global_row_from_mat(1, 300), global_row_from_mat(1, 301),
                global_row_from_mat(1, 302), global_row_from_mat(1, 303),
                global_row_from_mat(1, 304), global_row_from_mat(1, 305),
                global_row_from_mat(1, 306), global_row_from_mat(1, 307)
            });
            std::cout << "\n[mult_4bit_dual] A=rows100-103 B=rows200-203 Prod=MAT0/1:300-307 bank=" << DEMO_BANK << "\n";
            uops = mult_4bit_dual(srcA, srcB, prod_m0, prod_m1);
            break;
        }

        case 16: {
            const auto srcA    = make_addr_vec(DEMO_BANK, {100, 101, 102, 103});
            const auto srcB    = make_addr_vec(DEMO_BANK, {200, 201, 202, 203});
            const auto prod_m0 = make_addr_vec(DEMO_BANK, {
                global_row_from_mat(0, 300), global_row_from_mat(0, 301),
                global_row_from_mat(0, 302), global_row_from_mat(0, 303),
                global_row_from_mat(0, 304), global_row_from_mat(0, 305),
                global_row_from_mat(0, 306), global_row_from_mat(0, 307)
            });
            const auto prod_m1 = make_addr_vec(DEMO_BANK, {
                global_row_from_mat(1, 300), global_row_from_mat(1, 301),
                global_row_from_mat(1, 302), global_row_from_mat(1, 303),
                global_row_from_mat(1, 304), global_row_from_mat(1, 305),
                global_row_from_mat(1, 306), global_row_from_mat(1, 307)
            });
            std::cout << "\n[mult_4bit_opt_dual] A=rows100-103 B=rows200-203 Prod=MAT0/1:300-307 bank=" << DEMO_BANK << "\n";
            uops = mult_4bit_opt_dual(srcA, srcB, prod_m0, prod_m1);
            break;
        }

        case 17: {
            const auto srcA    = make_addr_vec(DEMO_BANK, {100, 101, 102, 103});
            const auto srcB    = make_addr_vec(DEMO_BANK, {200, 201, 202, 203});
            const auto prod_m0 = make_addr_vec(DEMO_BANK, {
                global_row_from_mat(0, 300), global_row_from_mat(0, 301),
                global_row_from_mat(0, 302), global_row_from_mat(0, 303),
                global_row_from_mat(0, 304), global_row_from_mat(0, 305),
                global_row_from_mat(0, 306), global_row_from_mat(0, 307)
            });
            std::cout << "\n[mult_4bit_opt] A=rows100-103 B=rows200-203 Prod=MAT0:300-307 bank=" << DEMO_BANK << "\n";
            uops = mult_4bit_opt(srcA, srcB, prod_m0);
            break;
        }

        case 18: {
            const auto srcA    = make_addr_vec(DEMO_BANK, {100, 101, 102, 103});
            const auto srcB    = make_addr_vec(DEMO_BANK, {200, 201, 202, 203});
            const auto prod_m0 = make_addr_vec(DEMO_BANK, {
                global_row_from_mat(0, 300), global_row_from_mat(0, 301),
                global_row_from_mat(0, 302), global_row_from_mat(0, 303),
                global_row_from_mat(0, 304), global_row_from_mat(0, 305),
                global_row_from_mat(0, 306), global_row_from_mat(0, 307)
            });
            std::cout << "\n[mult_4bit_opt_v2] A=rows100-103 B=rows200-203 Prod=MAT0:300-307 bank=" << DEMO_BANK << "\n";
            uops = mult_4bit_opt_v2(srcA, srcB, prod_m0);
            break;
        }

        case 19: {
            const auto srcA    = make_addr_vec(DEMO_BANK, {100, 101, 102, 103});
            const auto srcB    = make_addr_vec(DEMO_BANK, {200, 201, 202, 203});
            const auto prod_m0 = make_addr_vec(DEMO_BANK, {
                global_row_from_mat(0, 300), global_row_from_mat(0, 301),
                global_row_from_mat(0, 302), global_row_from_mat(0, 303),
                global_row_from_mat(0, 304), global_row_from_mat(0, 305),
                global_row_from_mat(0, 306), global_row_from_mat(0, 307)
            });
            std::cout << "\n[mult_4bit_csa] A=rows100-103 B=rows200-203 Prod=MAT0:300-307 bank=" << DEMO_BANK << "\n";
            uops = mult_4bit_csa(srcA, srcB, prod_m0);
            break;
        }

        case 20: {
            const auto srcA    = make_addr_vec(DEMO_BANK, {100, 101, 102, 103});
            const auto srcB    = make_addr_vec(DEMO_BANK, {200, 201, 202, 203});
            const auto prod_m0 = make_addr_vec(DEMO_BANK, {
                global_row_from_mat(0, 300), global_row_from_mat(0, 301),
                global_row_from_mat(0, 302), global_row_from_mat(0, 303),
                global_row_from_mat(0, 304), global_row_from_mat(0, 305),
                global_row_from_mat(0, 306), global_row_from_mat(0, 307)
            });
            std::cout << "\n[mult_4bit_csa_via_maj3] A=rows100-103 B=rows200-203 Prod=MAT0:300-307 bank=" << DEMO_BANK << "\n";
            uops = mult_4bit_csa_via_maj3(srcA, srcB, prod_m0);
            break;
        }

        case 21: {
            uint32_t n;
            std::cout << "Bit width N (1-16): ";
            std::cin >> n;
            if (n == 0 || n > 16) { std::cout << "N must be 1-16.\n"; break; }

            std::vector<uint64_t> srcA, srcB, prod_m0;
            for (uint32_t i = 0; i < n; ++i) {
                srcA.push_back(make_row_addr(DEMO_BANK, global_row_from_mat(0, 100 + i)));
                srcB.push_back(make_row_addr(DEMO_BANK, global_row_from_mat(0, 200 + i)));
            }
            for (uint32_t i = 0; i < 2 * n; ++i) {
                prod_m0.push_back(make_row_addr(DEMO_BANK, global_row_from_mat(0, 300 + i)));
            }
            std::cout << "\n[mult_nbit] " << n << "-bit A=MAT0:100.. B=MAT0:200.. Prod=MAT0:300.. bank=" << DEMO_BANK << "\n";
            uops = mult_nbit(srcA, srcB, prod_m0);
            break;
        }

        default:
            std::cout << "Invalid selection.\n";
            break;
        }

        if (!uops.empty()) {
            cud_print(uops);
            try_execute_on_hardware(uops);
        }

    } while (choice != 0);

    return 0;
}
