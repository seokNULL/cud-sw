#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <set>
#include <optional>

static constexpr uint32_t ROWS_PER_MAT = 1184;

struct RAGroup {
    std::string name;
    std::vector<std::string> bit_names;
    std::vector<uint32_t> bit_positions;
    std::vector<uint32_t> offsets;
    std::set<uint32_t> offset_set;

    RAGroup(std::string n, std::vector<std::string> bits);
};

// Declare global supported groups
extern std::vector<RAGroup> SUPPORTED_GROUPS;

struct AnalyzeResult {
    uint32_t base_row;
    uint32_t ra_group_index;
    std::vector<uint32_t> frac_rows; // can hold multiple rows
    bool valid;
};

AnalyzeResult analyze_rows(std::vector<uint32_t> row_ids);

// functions from utils.cpp
std::vector<uint32_t> single_Row_Copy(uint64_t src, uint64_t dest);
std::vector<uint32_t> Multi_Row_Copy(uint64_t src, uint64_t dest1, uint64_t dest2);
std::vector<uint32_t> OR(uint64_t src1, uint64_t src2);
std::vector<uint32_t> AND(uint64_t src1, uint64_t src2);
std::vector<uint32_t> NOT(uint64_t src, uint64_t dest);
std::vector<uint32_t> Maj3(uint64_t src1, uint64_t src2, uint64_t src3);
std::vector<uint32_t> Maj5(uint64_t src1, uint64_t src2, uint64_t src3, uint64_t src4, uint64_t src5);
std::vector<uint32_t> XOR(uint64_t src1, uint64_t src2);
std::vector<uint32_t> Maj5_via_Maj3(uint64_t src1, uint64_t src2, uint64_t src3, uint64_t src4, uint64_t src5, uint64_t out);
std::vector<uint32_t> ADD(uint64_t src1, uint64_t src2, uint64_t src3);
std::vector<uint32_t> ADD_new(uint64_t src1, uint64_t src2, uint64_t src3, uint64_t sum_m0, uint64_t sum_m1, uint64_t carry_m0, uint64_t carry_m1);
std::vector<uint32_t> ADD_4_bit(std::vector<uint64_t> src1, std::vector<uint64_t> src2, uint64_t src3, std::vector<uint64_t> sum, uint64_t c_out);
std::vector<uint32_t> ADD_4_bit_or_sum(std::vector<uint64_t> src1, std::vector<uint64_t> src2, uint64_t src3, std::vector<uint64_t> sum, uint64_t c_out);
std::vector<uint32_t> ADD_n_bit(std::vector<uint64_t> src1, std::vector<uint64_t> src2, uint64_t src3, std::vector<uint64_t> sum, uint64_t c_out);
std::vector<uint32_t> ADD_n_bit_via_Maj3(std::vector<uint64_t> src1, std::vector<uint64_t> src2, uint64_t src3, std::vector<uint64_t> sum, uint64_t c_out);
std::vector<uint32_t> Mult_n_bit(
                                    std::vector<uint64_t> srcA,
                                    std::vector<uint64_t> srcB,
                                    std::vector<uint64_t> prod_m0_dst);
std::vector<uint32_t> Mult_4_bit_new(    
                                    std::vector<uint64_t> srcA,
                                    std::vector<uint64_t> srcB,
                                    std::vector<uint64_t> prod_m0_dst,
                                    std::vector<uint64_t> prod_m1_dst);
std::vector<uint32_t> Mult_4_bit_optimized(
                                    std::vector<uint64_t> srcA,
                                    std::vector<uint64_t> srcB,
                                    std::vector<uint64_t> prod_m0_dst,
                                    std::vector<uint64_t> prod_m1_dst);
std::vector<uint32_t> Mult_4_bit_optimized_m0(
                                    std::vector<uint64_t> srcA,
                                    std::vector<uint64_t> srcB,
                                    std::vector<uint64_t> prod_m0_dst);
std::vector<uint32_t> Mult_4_bit_optimized_m0_1(
                                    std::vector<uint64_t> srcA,
                                    std::vector<uint64_t> srcB,
                                    std::vector<uint64_t> prod_m0_dst);
std::vector<uint32_t> Mult_4_bit_csa(
                                    std::vector<uint64_t> srcA,
                                    std::vector<uint64_t> srcB,
                                    std::vector<uint64_t> prod_m0_dst);
std::vector<uint32_t> Mult_4_bit_csa_via_Maj3(
                                    std::vector<uint64_t> srcA,
                                    std::vector<uint64_t> srcB,
                                    std::vector<uint64_t> prod_m0_dst);
std::vector<uint32_t> Mult_4_bit_optimized_mo_1(
                                    std::vector<uint64_t> srcA,
                                    std::vector<uint64_t> srcB,
                                    std::vector<uint64_t> prod_m0_dst);
uint32_t END();
uint32_t MULTI_BANK_ENTRY(uint32_t open_banks_mode);
uint32_t MULTI_BANK_EXIT();

                                 
uint64_t make_addr(uint32_t bank, uint32_t row);

enum class AddrMode : uint8_t {
    END = 0,
    ROW_COPY,
    ROW_COPY_1,
    MAJ3,
    MAJ5_1,
    MAJ5_2,
    MULTI_BANK_ENTRY,
    MULTI_BANK_EXIT
};

uint32_t build_address(AddrMode mode,
                       uint32_t bank = 0,
                       uint32_t row = 0,
                       uint32_t last = 0,
                       uint32_t maj3_frac = 0,
                       uint32_t maj5_frac = 0,
                       uint32_t dont_care = 0);

uint32_t get_dont_care_pos_maj3(uint32_t ra_group_index);
uint32_t get_dont_care_pos_maj5(uint32_t ra_group_index);
uint32_t extract_row_addr(uint64_t addr);
uint32_t extract_bank_addr(uint64_t addr);

bool row_matches_any(uint32_t row, const std::vector<uint64_t>& rows);

uint32_t get_mat_id_from_row(uint32_t global_row);
uint32_t get_local_row_from_global(uint32_t global_row);
uint32_t make_global_row(uint32_t mat_id, uint32_t local_row);

class CuDProgram {
private:
    std::vector<uint32_t> insts;
    bool valid = true;

public:
    CuDProgram& push(const std::vector<uint32_t>& new_insts);
    CuDProgram& add_end();

    bool is_valid() const;
    void clear();
    const std::vector<uint32_t>& get_insts() const;
    void run() const;
    void print_uops() const;
};
 void cud_print(const std::vector<uint32_t>& insts);