#pragma once
#include "types.h"
#include <cstdint>
#include <vector>

// Control u-ops
uint32_t encode_end();
uint32_t encode_multi_bank_entry(uint32_t open_banks_mode);
uint32_t encode_multi_bank_exit();

// Row copy primitives
std::vector<uint32_t> row_copy(uint64_t src, uint64_t dst);
std::vector<uint32_t> row_copy_fan(uint64_t src, uint64_t dst1, uint64_t dst2);

// Majority gate operations (native hardware)
std::vector<uint32_t> maj3(uint64_t src1, uint64_t src2, uint64_t src3);
std::vector<uint32_t> maj5(uint64_t src1, uint64_t src2, uint64_t src3,
                           uint64_t src4, uint64_t src5);

// Derived logic operations
std::vector<uint32_t> op_or(uint64_t src1, uint64_t src2);
std::vector<uint32_t> op_and(uint64_t src1, uint64_t src2);
std::vector<uint32_t> op_not(uint64_t src, uint64_t dst);
std::vector<uint32_t> op_xor(uint64_t src1, uint64_t src2);

// MAJ5 emulation using only MAJ3
std::vector<uint32_t> maj5_via_maj3(uint64_t src1, uint64_t src2, uint64_t src3,
                                    uint64_t src4, uint64_t src5, uint64_t out);

// Full adder (single bit, dual-MAT output)
std::vector<uint32_t> add_new(uint64_t src1, uint64_t src2, uint64_t src3,
                              uint64_t sum_m0, uint64_t sum_m1,
                              uint64_t carry_m0, uint64_t carry_m1);

// Ripple-carry adders
std::vector<uint32_t> add_4bit(std::vector<uint64_t> src1, std::vector<uint64_t> src2,
                               uint64_t carry_in, std::vector<uint64_t> sum,
                               uint64_t carry_out);

std::vector<uint32_t> add_4bit_or_sum(std::vector<uint64_t> src1, std::vector<uint64_t> src2,
                                      uint64_t carry_in, std::vector<uint64_t> sum,
                                      uint64_t carry_out);

std::vector<uint32_t> add_nbit(std::vector<uint64_t> src1, std::vector<uint64_t> src2,
                               uint64_t carry_in, std::vector<uint64_t> sum,
                               uint64_t carry_out);

std::vector<uint32_t> add_nbit_via_maj3(std::vector<uint64_t> src1, std::vector<uint64_t> src2,
                                        uint64_t carry_in, std::vector<uint64_t> sum,
                                        uint64_t carry_out);

// Legacy in-place adder (result written to scratch rows, no separate output)
std::vector<uint32_t> add_legacy(uint64_t src1, uint64_t src2, uint64_t src3);

// 4-bit multipliers
std::vector<uint32_t> mult_4bit_dual(std::vector<uint64_t> srcA, std::vector<uint64_t> srcB,
                                     std::vector<uint64_t> prod_m0, std::vector<uint64_t> prod_m1);

std::vector<uint32_t> mult_4bit_opt_dual(std::vector<uint64_t> srcA, std::vector<uint64_t> srcB,
                                         std::vector<uint64_t> prod_m0, std::vector<uint64_t> prod_m1);

std::vector<uint32_t> mult_4bit_opt(std::vector<uint64_t> srcA, std::vector<uint64_t> srcB,
                                    std::vector<uint64_t> prod_m0);

std::vector<uint32_t> mult_4bit_opt_v2(std::vector<uint64_t> srcA, std::vector<uint64_t> srcB,
                                       std::vector<uint64_t> prod_m0);

std::vector<uint32_t> mult_4bit_csa(std::vector<uint64_t> srcA, std::vector<uint64_t> srcB,
                                    std::vector<uint64_t> prod_m0);

std::vector<uint32_t> mult_4bit_csa_via_maj3(std::vector<uint64_t> srcA, std::vector<uint64_t> srcB,
                                             std::vector<uint64_t> prod_m0);

// n-bit multiplier
std::vector<uint32_t> mult_nbit(std::vector<uint64_t> srcA, std::vector<uint64_t> srcB,
                                std::vector<uint64_t> prod_m0);
