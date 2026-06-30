#pragma once
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

static constexpr uint32_t ROWS_PER_MAT = 1184;

// Global constant rows: index 1183 holds all-ones, 1182 holds all-zeros.
extern uint32_t g_const_one_row;
extern uint32_t g_const_zero_row;

// RA (Row Address) group: defines the set of DRAM rows addressable by a
// specific combination of row-address bits used in MAJ3/MAJ5 operations.
struct RaGroup {
    std::string name;
    std::vector<std::string> bit_names;
    std::vector<uint32_t> bit_positions;
    std::vector<uint32_t> offsets;
    std::set<uint32_t> offset_set;

    RaGroup(std::string n, std::vector<std::string> bits);
};

extern std::vector<RaGroup> SUPPORTED_GROUPS;

struct RowAnalysisResult {
    uint32_t base_row;
    uint32_t ra_group_index;
    std::vector<uint32_t> frac_rows;
    bool valid;
};

RowAnalysisResult analyze_rows(std::vector<uint32_t> row_ids);

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

uint32_t encode_uop(AddrMode mode,
                    uint32_t bank = 0,
                    uint32_t row = 0,
                    uint32_t last = 0,
                    uint32_t maj3_frac = 0,
                    uint32_t maj5_frac = 0,
                    uint32_t dont_care = 0);

uint32_t dont_care_pos_maj3(uint32_t ra_group_index);
uint32_t dont_care_pos_maj5(uint32_t ra_group_index);

uint32_t extract_row(uint64_t addr);
uint32_t extract_bank(uint64_t addr);
bool row_in_list(uint32_t row, const std::vector<uint64_t>& rows);

uint32_t mat_id_from_row(uint32_t global_row);
uint32_t local_row_from_global(uint32_t global_row);
uint32_t global_row_from_mat(uint32_t mat_id, uint32_t local_row);
uint64_t make_row_addr(uint32_t bank, uint32_t row);

class CudProgram {
public:
    CudProgram& push(const std::vector<uint32_t>& new_insts);
    CudProgram& add_end();

    bool is_valid() const;
    void clear();
    const std::vector<uint32_t>& get_insts() const;
    void run() const;
    void print_uops() const;

private:
    std::vector<uint32_t> insts_;
    bool valid_ = true;
};

void cud_print(const std::vector<uint32_t>& insts);
