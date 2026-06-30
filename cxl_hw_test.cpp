#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "include/cxl_hardware.h"
#include "include/instruction.h"

namespace {

uint64_t test_value(uint64_t seed, uint32_t col) {
    return seed ^ (0x9E3779B97F4A7C15ULL * (static_cast<uint64_t>(col) + 1));
}

uint64_t majority3(uint64_t a, uint64_t b, uint64_t c) {
    return (a & b) | (a & c) | (b & c);
}

uint64_t not_mask_for_mat_pair(uint32_t lower_mat) {
    return (lower_mat % 2 == 0)
        ? 0x00FF00FF00FF00FF
        : 0xFF00FF00FF00FF00;
}


uint32_t popcount64(uint64_t value) {
    uint32_t count = 0;
    while (value != 0) {
        value &= value - 1;
        ++count;
    }
    return count;
}

bool verify_constant_row(
    CxlHardware& hw,
    uint32_t bank,
    uint32_t row,
    uint64_t expected,
    const char* name
) {
    for (uint32_t col = 0; col < 1024; ++col) {
        uint64_t actual = 0;
        if (!hw.read_row_col(bank, row, col, actual)) {
            std::cout << "[ERROR] " << name
                      << " readback failed at col=" << col
                      << ": " << hw.last_error() << "\n";
            return false;
        }
        if (actual != expected) {
            std::cout << "[FAIL] " << name
                      << " initialization failed at col=" << col
                      << " expected=0x" << std::hex << expected
                      << " actual=0x" << actual << std::dec << "\n";
            return false;
        }
    }
    return true;
}

bool fill_local_row_range(
    CxlHardware& hw,
    uint32_t bank,
    uint32_t mat,
    uint32_t first_local_row,
    uint32_t last_local_row,
    uint64_t value,
    const char* name
) {
    for (uint32_t local_row = first_local_row; local_row <= last_local_row; ++local_row) {
        if (!hw.fill_row(bank, make_global_row(mat, local_row), value)) {
            std::cout << "[ERROR] " << name
                      << " initialization failed at local_row=" << local_row
                      << ": " << hw.last_error() << "\n";
            return false;
        }
    }
    return true;
}

struct TestSummary {
    std::string name;
    std::string status;
    std::string details;
};

bool write_summary_report(const std::vector<TestSummary>& summaries,
                          const std::string& path) {
    std::ofstream report(path);
    if (!report) {
        std::cout << "[ERROR] Cannot create summary report: " << path << "\n";
        return false;
    }

    report << "test,status,details\n";
    for (const auto& summary : summaries) {
        report << summary.name << ','
               << summary.status << ','
               << summary.details << "\n";
    }
    return true;
}

bool run_rowcopy_test(CxlHardware& hw) {
    const uint32_t bank = 0;
    const uint32_t mat = 0;
    const uint32_t src_row = make_global_row(mat, 20);
    const uint32_t dst_row =  make_global_row(mat, 100);

    std::cout << "\n===== CXL RowCopy TEST =====\n";

    for (uint32_t col = 0; col < 1024; ++col) {
        const uint64_t source_value =
            0x0000000000000000ULL | static_cast<uint64_t>(col);
        if (!hw.write_row_col(bank, src_row, col, source_value)) {
            std::cout << "[ERROR] CXL.mem prefill failed: " << hw.last_error() << "\n";
            return false;
        }
    }

    const std::vector<uint32_t> insts = single_Row_Copy(
        make_addr(bank, src_row),
        make_addr(bank, dst_row)
    );

    if (insts.empty()) {
        std::cout << "[ERROR] RowCopy u-op generation failed.\n";
        return false;
    }
    if (!hw.execute(insts)) {
        std::cout << "[ERROR] CXL.io execution failed: " << hw.last_error() << "\n";
        return false;
    }

    for (uint32_t col = 0; col < 1024; ++col) {
        const uint64_t expected =
            0x0000000000000000ULL | static_cast<uint64_t>(col);
        uint64_t actual = 0;
        if (!hw.read_row_col(bank, dst_row, col, actual)) {
            std::cout << "[ERROR] CXL.mem read failed: " << hw.last_error() << "\n";
            return false;
        }
        if (actual != expected) {
            std::cout << "[FAIL] col=" << col
                      << " expected=0x" << std::hex << expected
                      << " actual=0x" << actual << std::dec << "\n";
            return false;
        }
    }

    std::cout << "[PASS] RowCopy copied all 1024 x 64-bit columns.\n";
    return true;
}

bool run_maj3_test(CxlHardware& hw) {
    const uint32_t bank = 0;
    const uint32_t mat = 0;
    const uint32_t a_row = make_global_row(mat, 0);
    const uint32_t b_row = make_global_row(mat, 1);
    const uint32_t c_row = make_global_row(mat, 8);
    const uint32_t result_row = make_global_row(mat, 9);

    std::cout << "\n===== CXL MAJ3 TEST =====\n";

    for (uint32_t col = 0; col < 1024; ++col) {
        const uint64_t a = test_value(0x1111111111111111ULL, col);
        const uint64_t b = test_value(0x2222222222222222ULL, col);
        const uint64_t c = test_value(0x4444444444444444ULL, col);

        if (!hw.write_row_col(bank, a_row, col, a) ||
            !hw.write_row_col(bank, b_row, col, b) ||
            !hw.write_row_col(bank, c_row, col, c) || 
            !hw.write_row_col(bank, result_row, col, 0)) {
            std::cout << "[ERROR] CXL.mem prefill failed: " << hw.last_error() << "\n";
            return false;
        }
    }

    const std::vector<uint32_t> insts = Maj3(
        make_addr(bank, a_row),
        make_addr(bank, b_row),
        make_addr(bank, c_row)
    );

    if (insts.empty()) {
        std::cout << "[ERROR] MAJ3 u-op generation failed.\n";
        return false;
    }
    if (!hw.execute(insts)) {
        std::cout << "[ERROR] CXL.io execution failed: " << hw.last_error() << "\n";
        return false;
    }

    for (uint32_t col = 0; col < 1024; ++col) {
        const uint64_t a = test_value(0x1111111111111111ULL, col);
        const uint64_t b = test_value(0x2222222222222222ULL, col);
        const uint64_t c = test_value(0x4444444444444444ULL, col);
        const uint64_t expected = majority3(a, b, c);
        uint64_t actual = 0;

        if (!hw.read_row_col(bank, result_row, col, actual)) {
            std::cout << "[ERROR] CXL.mem read failed: " << hw.last_error() << "\n";
            return false;
        }
        if (actual != expected) {
            std::cout << "[FAIL] col=" << col
                      << " expected=0x" << std::hex << expected
                      << " actual=0x" << actual << std::dec << "\n";
            return false;
        }
    }

    std::cout << "[PASS] MAJ3 verified all 1024 x 64-bit columns.\n";
    return true;
}

bool run_and_test(CxlHardware& hw) {
    const uint32_t bank = 0;
    const uint32_t mat = 0;
    const uint32_t a_row = make_global_row(mat, 32);
    const uint32_t b_row = make_global_row(mat, 40);
    const uint32_t zero_row = make_global_row(mat, 1182);
    const uint32_t constant_copy_row = make_global_row(mat, 33);
    // Rows 32 and 40 select the {0,1,8,9} RA group at base row 32.
    // AND uses row 33 for the zero constant and row 41 for the result.
    const uint32_t result_row = make_global_row(mat, 41);

    std::cout << "\n===== CXL AND TEST =====\n";

    if (!hw.fill_row(bank, zero_row, 0) || !hw.fill_row(bank, constant_copy_row, 0) || 
        !hw.fill_row(bank, result_row, 0)) {
        std::cout << "[ERROR] CXL.mem row initialization failed: "
                  << hw.last_error() << "\n";
        return false;
    }

    for (uint32_t col = 0; col < 1024; ++col) {
        const uint64_t a = test_value(0x0F0F0F0F0F0F0F0FULL, col);
        const uint64_t b = test_value(0x3333333333333333ULL, col);

        if (!hw.write_row_col(bank, a_row, col, a) ||
            !hw.write_row_col(bank, b_row, col, b)) {
            std::cout << "[ERROR] CXL.mem prefill failed: " << hw.last_error() << "\n";
            return false;
        }
    }

    if (!verify_constant_row(hw, bank, zero_row, 0, "AND zero row")) {
        return false;
    }

    const std::vector<uint32_t> insts = AND(
        make_addr(bank, a_row),
        make_addr(bank, b_row)
    );

    if (insts.empty()) {
        std::cout << "[ERROR] AND u-op generation failed.\n";
        return false;
    }
    if (!hw.execute(insts)) {
        std::cout << "[ERROR] CXL.io execution failed: " << hw.last_error() << "\n";
        return false;
    }

    for (uint32_t col = 0; col < 1024; ++col) {
        const uint64_t a = test_value(0x0F0F0F0F0F0F0F0FULL, col);
        const uint64_t b = test_value(0x3333333333333333ULL, col);
        const uint64_t expected = a & b;
        uint64_t actual = 0;

        if (!hw.read_row_col(bank, result_row, col, actual)) {
            std::cout << "[ERROR] CXL.mem read failed: " << hw.last_error() << "\n";
            return false;
        }
        if (actual != expected) {
            std::cout << "[FAIL] col=" << col
                      << " expected=0x" << std::hex << expected
                      << " actual=0x" << actual << std::dec << "\n";
            return false;
        }
    }

    std::cout << "[PASS] AND verified all 1024 x 64-bit columns.\n";
    return true;
}

bool run_or_test(CxlHardware& hw) {
    const uint32_t bank = 0;
    const uint32_t mat = 0;
    const uint32_t a_row = make_global_row(mat, 32);
    const uint32_t b_row = make_global_row(mat, 40);
    const uint32_t one_row = make_global_row(mat, 1183);
    // const uint32_t constant_copy_row = make_global_row(mat, 33);
    // Rows 32 and 40 select the {0,1,8,9} RA group at base row 32.
    // OR uses row 33 for the one constant and row 41 for the result.
    const uint32_t result_row = make_global_row(mat, 41);

    std::cout << "\n===== CXL OR TEST =====\n";

    if (!hw.fill_row(bank, one_row, 0xFFFFFFFFFFFFFFFFULL)) {
        std::cout << "[ERROR] CXL.mem row initialization failed: "
                  << hw.last_error() << "\n";
        return false;
    }

    for (uint32_t col = 0; col < 1024; ++col) {
        const uint64_t a = test_value(0x0F0F0F0F0F0F0F0FULL, col);
        const uint64_t b = test_value(0x3333333333333333ULL, col);

        if (!hw.write_row_col(bank, a_row, col, a) ||
            !hw.write_row_col(bank, b_row, col, b)) {
            std::cout << "[ERROR] CXL.mem prefill failed: " << hw.last_error() << "\n";
            return false;
        }
    }

    if (!verify_constant_row(
            hw, bank, one_row, 0xFFFFFFFFFFFFFFFFULL, "OR one row")) {
        return false;
    }

    const std::vector<uint32_t> insts = OR(
        make_addr(bank, a_row),
        make_addr(bank, b_row)
    );

    if (insts.empty()) {
        std::cout << "[ERROR] OR u-op generation failed.\n";
        return false;
    }
    if (!hw.execute(insts)) {
        std::cout << "[ERROR] CXL.io execution failed: " << hw.last_error() << "\n";
        return false;
    }

    for (uint32_t col = 0; col < 1024; ++col) {
        const uint64_t a = test_value(0x0F0F0F0F0F0F0F0FULL, col);
        const uint64_t b = test_value(0x3333333333333333ULL, col);
        const uint64_t expected = a | b;
        uint64_t actual = 0;

        if (!hw.read_row_col(bank, result_row, col, actual)) {
            std::cout << "[ERROR] CXL.mem read failed: " << hw.last_error() << "\n";
            return false;
        }
        if (actual != expected) {
            std::cout << "[FAIL] col=" << col
                      << " expected=0x" << std::hex << expected
                      << " actual=0x" << actual << std::dec << "\n";
            return false;
        }
    }

    std::cout << "[PASS] OR verified all 1024 x 64-bit columns.\n";
    return true;
}

bool run_xor_test(CxlHardware& hw) {
    const uint32_t bank = 0;
    const uint32_t mat0 = 0;
    const uint32_t mat1 = mat0 + 1;
    const uint32_t a_row = make_global_row(mat0, 50);
    const uint32_t b_row = make_global_row(mat0, 190);
    const uint32_t a_mirror_row = make_global_row(mat1, 50);
    const uint32_t b_mirror_row = make_global_row(mat1, 190);
    const uint32_t zero_row_mat0 = make_global_row(mat0, 1182);
    const uint32_t one_row_mat0 = make_global_row(mat0, 1183);
    const uint32_t zero_row_mat1 = make_global_row(mat1, 1182);
    const uint32_t one_row_mat1 = make_global_row(mat1, 1183);
    const uint64_t valid_mask = not_mask_for_mat_pair(mat0);
    // XOR allocates the first free {0,1,8,9} group in MAT0. Its final
    // AND uses rows 0 and 1, row 8 for zero, and row 9 for the result.
    const uint32_t result_row = make_global_row(mat0, 9);
    const uint32_t intermediate_result_row = make_global_row(mat1, 9);

    std::cout << "\n===== CXL XOR TEST =====\n";
    std::cout << "[INFO] valid NOT lanes mask: 0x"
              << std::hex << valid_mask << std::dec << "\n";

    for (uint32_t col = 0; col < 1024; ++col) {
        const uint64_t a =
            test_value(0x5555555555555555ULL, col) & valid_mask;
        const uint64_t b =
            test_value(0xAAAAAAAAAAAAAAAAULL, col) & valid_mask;

        if (!hw.write_row_col(bank, a_row, col, a) ||
            !hw.write_row_col(bank, b_row, col, b) ||
            !hw.write_row_col(bank, a_mirror_row, col, a) ||
            !hw.write_row_col(bank, b_mirror_row, col, b) ||
            !hw.write_row_col(bank, zero_row_mat0, col, 0) ||
            !hw.write_row_col(bank, one_row_mat0, col, valid_mask) ||
            !hw.write_row_col(bank, zero_row_mat1, col, 0) ||
            !hw.write_row_col(bank, one_row_mat1, col, valid_mask) ||
            !hw.write_row_col(bank, result_row, col, 0) ||
            !hw.write_row_col(bank, intermediate_result_row, col, 0)) {
            std::cout << "[ERROR] CXL.mem prefill failed: " << hw.last_error() << "\n";
            return false;
        }
    }

    const std::vector<uint32_t> insts = XOR(
        make_addr(bank, a_row),
        make_addr(bank, b_row)
    );

    if (insts.empty()) {
        std::cout << "[ERROR] XOR u-op generation failed.\n";
        return false;
    }
    if (!hw.execute(insts)) {
        std::cout << "[ERROR] CXL.io execution failed: " << hw.last_error() << "\n";
        return false;
    }

    for (uint32_t col = 0; col < 1024; ++col) {
        const uint64_t a =
            test_value(0x5555555555555555ULL, col) & valid_mask;
        const uint64_t b =
            test_value(0xAAAAAAAAAAAAAAAAULL, col) & valid_mask;
        const uint64_t expected = (a ^ b) & valid_mask;
        uint64_t actual = 0;

        if (!hw.read_row_col(bank, result_row, col, actual)) {
            std::cout << "[ERROR] CXL.mem read failed: " << hw.last_error() << "\n";
            return false;
        }
        const uint64_t actual_masked = actual & valid_mask;
        if (actual_masked != expected) {
            std::cout << "[FAIL] col=" << col
                      << " expected=0x" << std::hex << expected
                      << " actual_masked=0x" << actual_masked
                      << " actual_raw=0x" << actual << std::dec << "\n";
            return false;
        }
    }

    std::cout << "[PASS] XOR verified all 1024 columns on 32 valid lanes.\n";
    return true;
}

bool run_add_n_bit_variant_test(CxlHardware& hw,
                                bool use_maj5_via_maj3,
                                const char* test_name) {
    const uint32_t bank = 0;
    const uint32_t mat0 = 1;
    const uint32_t mat1 = mat0 + 1;
    const uint32_t n_bits = 1;
    const uint64_t valid_mask = not_mask_for_mat_pair(mat0);

    const uint32_t a_base = 0;
    const uint32_t b_base = 10;
    const uint32_t cin_local = 20;
    const uint32_t sum_base = 30;
    const uint32_t cout_local = sum_base + n_bits;

    std::vector<uint32_t> a_rows;
    std::vector<uint32_t> b_rows;
    std::vector<uint32_t> sum_rows;
    a_rows.reserve(n_bits);
    b_rows.reserve(n_bits);
    sum_rows.reserve(n_bits);

    for (uint32_t bit = 0; bit < n_bits; ++bit) {
        a_rows.push_back(make_global_row(mat0, a_base + bit));
        b_rows.push_back(make_global_row(mat0, b_base + bit));
        sum_rows.push_back(make_global_row(mat1, sum_base + bit));
    }

    const uint32_t cin_row = make_global_row(mat0, cin_local);
    const uint32_t cout_row = make_global_row(mat0, cout_local);

    std::cout << "\n===== CXL " << test_name << " TEST =====\n";
    std::cout << "[INFO] n_bits: " << n_bits << "\n";
    std::cout << "[INFO] valid NOT lanes mask: 0x"
              << std::hex << valid_mask << std::dec << "\n";

    // Clear a wide scratch range so native MAJ5 and MAJ5-via-MAJ3 runs start
    // from the same known DRAM state.
    if (!fill_local_row_range(hw, bank, mat0, 0, 64, 0, "ADD_n_bit MAT0 scratch") ||
        !fill_local_row_range(hw, bank, mat1, 0, 64, 0, "ADD_n_bit MAT1 scratch")) {
        return false;
    }

    for (uint32_t col = 0; col < 1024; ++col) {
        for (uint32_t bit = 0; bit < n_bits; ++bit) {
            const uint64_t a =
                test_value(0x11ULL + bit, col) & valid_mask;
            const uint64_t b =
                test_value(0xA5ULL + bit, col) & valid_mask;

            if (!hw.write_row_col(bank, a_rows[bit], col, a) ||
                !hw.write_row_col(bank, b_rows[bit], col, b) ||
                !hw.write_row_col(bank, a_rows[bit] + ROWS_PER_MAT, col, a) ||
                !hw.write_row_col(bank, b_rows[bit] + ROWS_PER_MAT, col, b) ||
                !hw.write_row_col(bank, sum_rows[bit], col, 0)) {
                std::cout << "[ERROR] CXL.mem prefill failed: " << hw.last_error() << "\n";
                return false;
            }
        }

        const uint64_t cin = test_value(0x3CULL, col) & valid_mask;
        if (!hw.write_row_col(bank, cin_row, col, cin) ||
            !hw.write_row_col(bank, cin_row + ROWS_PER_MAT, col, cin) ||
            !hw.write_row_col(bank, make_global_row(mat0, 1182), col, 0) ||
            !hw.write_row_col(bank, make_global_row(mat0, 1183), col, valid_mask) ||
            !hw.write_row_col(bank, make_global_row(mat1, 1182), col, 0) ||
            !hw.write_row_col(bank, make_global_row(mat1, 1183), col, valid_mask) ||
            !hw.write_row_col(bank, cout_row, col, 0)) {
            std::cout << "[ERROR] CXL.mem prefill failed: " << hw.last_error() << "\n";
            return false;
        }
    }

    std::vector<uint64_t> src1;
    std::vector<uint64_t> src2;
    std::vector<uint64_t> sum;
    src1.reserve(n_bits);
    src2.reserve(n_bits);
    sum.reserve(n_bits);

    for (uint32_t bit = 0; bit < n_bits; ++bit) {
        src1.push_back(make_addr(bank, a_rows[bit]));
        src2.push_back(make_addr(bank, b_rows[bit]));
        sum.push_back(make_addr(bank, sum_rows[bit]));
    }

    const std::vector<uint32_t> native_insts = ADD_n_bit(
        src1,
        src2,
        make_addr(bank, cin_row),
        sum,
        make_addr(bank, cout_row)
    );
    const std::vector<uint32_t> via_maj3_insts = ADD_n_bit_via_Maj3(
        src1,
        src2,
        make_addr(bank, cin_row),
        sum,
        make_addr(bank, cout_row)
    );
    const std::vector<uint32_t>& insts =
        use_maj5_via_maj3 ? via_maj3_insts : native_insts;

    if (insts.empty()) {
        std::cout << "[ERROR] " << test_name << " u-op generation failed.\n";
        return false;
    }

    std::cout << "[INFO] selected uops: " << insts.size() << "\n";
    if (!native_insts.empty()) {
        std::cout << "[INFO] native uops  : " << native_insts.size() << "\n";
    }
    if (!via_maj3_insts.empty()) {
        std::cout << "[INFO] via_Maj3 uops: " << via_maj3_insts.size() << "\n";
    }

    if (!hw.execute(insts)) {
        std::cout << "[ERROR] CXL.io execution failed: " << hw.last_error() << "\n";
        return false;
    }

    const std::string report_path =
        "add_" + std::to_string(n_bits) +
        (use_maj5_via_maj3 ? "bit_via_maj3_dest_rows.csv"
                           : "bit_maj5_dest_rows.csv");
    std::ofstream report(report_path);
    if (!report) {
        std::cout << "[ERROR] Cannot create report file: " << report_path << "\n";
        return false;
    }

    report << "test," << test_name << "\n";
    report << "n_bits," << n_bits << "\n";
    report << "bank," << bank << "\n";
    report << "mat0," << mat0 << "\n";
    report << "mat1," << mat1 << "\n";
    report << "valid_mask,0x" << std::hex << std::setw(16) << std::setfill('0')
           << valid_mask << std::dec << "\n";
    report << "selected_uops," << insts.size() << "\n";
    if (!native_insts.empty()) {
        report << "native_uops," << native_insts.size() << "\n";
    }
    if (!via_maj3_insts.empty()) {
        report << "via_Maj3_uops," << via_maj3_insts.size() << "\n";
    }
    for (uint32_t bit = 0; bit < n_bits; ++bit) {
        report << "sum_bit_" << bit << "_row," << sum_rows[bit] << "\n";
    }
    report << "cout_row," << cout_row << "\n";
    report << "\n";
    report << "kind,bit,col,dest_row,expected,actual_masked,actual_raw,diff,bit_errors,valid_bits,bit_accuracy_percent,pass\n";

    uint32_t mismatch_count = 0;
    uint64_t total_bit_errors = 0;
    uint64_t total_valid_bits = 0;
    const uint32_t valid_bits_per_word = popcount64(valid_mask);
    bool printed_first_mismatch = false;

    auto write_report_line = [&](const char* kind,
                                 uint32_t bit,
                                 uint32_t col,
                                 uint32_t dest_row,
                                 uint64_t expected,
                                 uint64_t actual_masked,
                                 uint64_t actual_raw) {
        const uint64_t diff = (expected ^ actual_masked) & valid_mask;
        const uint32_t bit_errors = popcount64(diff);
        const double bit_accuracy =
            valid_bits_per_word == 0
                ? 100.0
                : 100.0 * static_cast<double>(valid_bits_per_word - bit_errors) /
                    static_cast<double>(valid_bits_per_word);
        total_bit_errors += bit_errors;
        total_valid_bits += valid_bits_per_word;

        report << kind << ','
               << std::dec << bit << ','
               << col << ','
               << dest_row << ','
               << "0x" << std::hex << std::setw(16) << std::setfill('0') << expected << ','
               << "0x" << std::setw(16) << actual_masked << ','
               << "0x" << std::setw(16) << actual_raw << ','
               << "0x" << std::setw(16) << diff << ','
               << std::dec << bit_errors << ','
               << valid_bits_per_word << ','
               << std::fixed << std::setprecision(4) << bit_accuracy << ','
               << (diff == 0 ? "PASS" : "FAIL")
               << std::defaultfloat << std::dec << "\n";
    };

    for (uint32_t col = 0; col < 1024; ++col) {
        uint64_t carry = test_value(0x3CULL, col) & valid_mask;

        for (uint32_t bit = 0; bit < n_bits; ++bit) {
            const uint64_t a =
                test_value(0x11ULL + bit, col) & valid_mask;
            const uint64_t b =
                test_value(0xA5ULL + bit, col) & valid_mask;
            const uint64_t expected_sum =
                (a ^ b ^ carry) & valid_mask;
            const uint64_t next_carry =
                majority3(a, b, carry) & valid_mask;
            uint64_t actual_sum = 0;

            if (!hw.read_row_col(bank, sum_rows[bit], col, actual_sum)) {
                std::cout << "[ERROR] CXL.mem read failed: " << hw.last_error() << "\n";
                return false;
            }
            const uint64_t actual_sum_masked =
                actual_sum & valid_mask;
            write_report_line(
                "sum",
                bit,
                col,
                sum_rows[bit],
                expected_sum,
                actual_sum_masked,
                actual_sum
            );
            if (actual_sum_masked != expected_sum) {
                ++mismatch_count;
                if (!printed_first_mismatch) {
                    std::cout << "[FAIL] sum_bit=" << bit << " col=" << col
                              << " expected=0x" << std::hex << expected_sum
                              << " actual_masked=0x" << actual_sum_masked
                              << " actual_raw=0x" << actual_sum
                              << std::dec << "\n";
                    printed_first_mismatch = true;
                }
            }

            carry = next_carry;
        }

        uint64_t actual_carry = 0;
        if (!hw.read_row_col(bank, cout_row, col, actual_carry)) {
            std::cout << "[ERROR] CXL.mem read failed: " << hw.last_error() << "\n";
            return false;
        }
        const uint64_t actual_carry_masked =
            actual_carry & valid_mask;
        write_report_line(
            "carry",
            n_bits,
            col,
            cout_row,
            carry,
            actual_carry_masked,
            actual_carry
        );
        if (actual_carry_masked != carry) {
            ++mismatch_count;
            if (!printed_first_mismatch) {
                std::cout << "[FAIL] carry col=" << col
                          << " expected=0x" << std::hex << carry
                          << " actual_masked=0x" << actual_carry_masked
                          << " actual_raw=0x" << actual_carry
                          << std::dec << "\n";
                printed_first_mismatch = true;
            }
        }
    }

    const double overall_bit_accuracy =
        total_valid_bits == 0
            ? 100.0
            : 100.0 * static_cast<double>(total_valid_bits - total_bit_errors) /
                static_cast<double>(total_valid_bits);

    report << "\n";
    report << "summary,total_valid_bits," << total_valid_bits << "\n";
    report << "summary,total_bit_errors," << total_bit_errors << "\n";
    report << "summary,bit_accuracy_percent,"
           << std::fixed << std::setprecision(6) << overall_bit_accuracy
           << std::defaultfloat << "\n";
    report << "summary,mismatched_rows," << mismatch_count << "\n";

    std::cout << "[INFO] destination row report: " << report_path << "\n";
    std::cout << "[INFO] bit_errors: " << total_bit_errors
              << " / " << total_valid_bits << "\n";
    std::cout << "[INFO] bit_accuracy_percent: "
              << std::fixed << std::setprecision(6) << overall_bit_accuracy
              << std::defaultfloat << "\n";
    if (mismatch_count != 0) {
        std::cout << "[FAIL] " << test_name << " mismatches: "
                  << mismatch_count << "\n";
        return false;
    }

    std::cout << "[PASS] " << test_name << " verified all 1024 x 64-bit columns.\n";
    return true;
}

bool run_add_n_bit_maj5_test(CxlHardware& hw) {
    return run_add_n_bit_variant_test(hw, false, "ADD_n_bit_Maj5");
}

bool run_add_n_bit_via_maj3_test(CxlHardware& hw) {
    return run_add_n_bit_variant_test(hw, true, "ADD_n_bit_via_Maj3");
}



// bool run_add_n_bit_via_maj3_test(CxlHardware& hw) {
//     const uint32_t bank = 0;
//     const uint32_t mat0 = 0;
//     const uint32_t mat1 = mat0 + 1;
//     const uint32_t n_bits = 1;
//     const uint64_t valid_mask = not_mask_for_mat_pair(mat0);

//     const uint32_t a_base = 200;
//     const uint32_t b_base = 300;
//     const uint32_t cin_local = 400;
//     const uint32_t sum_base = 800;
//     const uint32_t cout_local = sum_base + n_bits;

//     std::vector<uint32_t> a_rows;
//     std::vector<uint32_t> b_rows;
//     std::vector<uint32_t> sum_rows;
//     a_rows.reserve(n_bits);
//     b_rows.reserve(n_bits);
//     sum_rows.reserve(n_bits);

//     for (uint32_t bit = 0; bit < n_bits; ++bit) {
//         a_rows.push_back(make_global_row(mat0, a_base + bit));
//         b_rows.push_back(make_global_row(mat0, b_base + bit));
//         sum_rows.push_back(make_global_row(mat1, sum_base + bit));
//     }

//     const uint32_t cin_row = make_global_row(mat0, cin_local);
//     const uint32_t cout_row = make_global_row(mat0, cout_local);

//     std::cout << "\n===== CXL ADD_n_bit_via_Maj3 TEST =====\n";
//     std::cout << "[INFO] n_bits: " << n_bits << "\n";
//     std::cout << "[INFO] valid NOT lanes mask: 0x"
//               << std::hex << valid_mask << std::dec << "\n";

//     // This path decomposes each MAJ5 into several MAJ3 operations, so clear a
//     // wider scratch range than the native ADD test before programming inputs.
//     if (!fill_local_row_range(hw, bank, mat0, 0, 64, 0, "ADD_via_Maj3 MAT0 scratch") ||
//         !fill_local_row_range(hw, bank, mat1, 0, 64, 0, "ADD_via_Maj3 MAT1 scratch")) {
//         return false;
//     }

//     for (uint32_t col = 0; col < 1024; ++col) {
//         for (uint32_t bit = 0; bit < n_bits; ++bit) {
//             const uint64_t a =
//                 test_value(0x11ULL + bit, col) & valid_mask;
//             const uint64_t b =
//                 test_value(0xA5ULL + bit, col) & valid_mask;

//             if (!hw.write_row_col(bank, a_rows[bit], col, a) ||
//                 !hw.write_row_col(bank, b_rows[bit], col, b) ||
//                 !hw.write_row_col(bank, a_rows[bit] + ROWS_PER_MAT, col, a) ||
//                 !hw.write_row_col(bank, b_rows[bit] + ROWS_PER_MAT, col, b) ||
//                 !hw.write_row_col(bank, sum_rows[bit], col, 0)) {
//                 std::cout << "[ERROR] CXL.mem prefill failed: " << hw.last_error() << "\n";
//                 return false;
//             }
//         }

//         const uint64_t cin = test_value(0x3CULL, col) & valid_mask;
//         if (!hw.write_row_col(bank, cin_row, col, cin) ||
//             !hw.write_row_col(bank, cin_row + ROWS_PER_MAT, col, cin) ||
//             !hw.write_row_col(bank, make_global_row(mat0, 1182), col, 0) ||
//             !hw.write_row_col(bank, make_global_row(mat0, 1183), col, valid_mask) ||
//             !hw.write_row_col(bank, make_global_row(mat1, 1182), col, 0) ||
//             !hw.write_row_col(bank, make_global_row(mat1, 1183), col, valid_mask) ||
//             !hw.write_row_col(bank, cout_row, col, 0)) {
//             std::cout << "[ERROR] CXL.mem prefill failed: " << hw.last_error() << "\n";
//             return false;
//         }
//     }

//     std::vector<uint64_t> src1;
//     std::vector<uint64_t> src2;
//     std::vector<uint64_t> sum;
//     src1.reserve(n_bits);
//     src2.reserve(n_bits);
//     sum.reserve(n_bits);

//     for (uint32_t bit = 0; bit < n_bits; ++bit) {
//         src1.push_back(make_addr(bank, a_rows[bit]));
//         src2.push_back(make_addr(bank, b_rows[bit]));
//         sum.push_back(make_addr(bank, sum_rows[bit]));
//     }

//     const std::vector<uint32_t> insts = ADD_n_bit_via_Maj3(
//         src1,
//         src2,
//         make_addr(bank, cin_row),
//         sum,
//         make_addr(bank, cout_row)
//     );
//     const std::vector<uint32_t> native_insts = ADD_n_bit(
//         src1,
//         src2,
//         make_addr(bank, cin_row),
//         sum,
//         make_addr(bank, cout_row)
//     );

//     if (insts.empty()) {
//         std::cout << "[ERROR] ADD_n_bit_via_Maj3 u-op generation failed.\n";
//         return false;
//     }

//     std::cout << "[INFO] via_Maj3 uops: " << insts.size() << "\n";
//     if (!native_insts.empty()) {
//         std::cout << "[INFO] native uops  : " << native_insts.size() << "\n";
//     }

//     if (!hw.execute(insts)) {
//         std::cout << "[ERROR] CXL.io execution failed: " << hw.last_error() << "\n";
//         return false;
//     }

//     for (uint32_t col = 0; col < 1024; ++col) {
//         uint64_t carry = test_value(0x3CULL, col) & valid_mask;

//         for (uint32_t bit = 0; bit < n_bits; ++bit) {
//             const uint64_t a =
//                 test_value(0x11ULL + bit, col) & valid_mask;
//             const uint64_t b =
//                 test_value(0xA5ULL + bit, col) & valid_mask;
//             const uint64_t expected_sum =
//                 (a ^ b ^ carry) & valid_mask;
//             const uint64_t next_carry =
//                 majority3(a, b, carry) & valid_mask;
//             uint64_t actual_sum = 0;

//             if (!hw.read_row_col(bank, sum_rows[bit], col, actual_sum)) {
//                 std::cout << "[ERROR] CXL.mem read failed: " << hw.last_error() << "\n";
//                 return false;
//             }
//             const uint64_t actual_sum_masked =
//                 actual_sum & valid_mask;
//             if (actual_sum_masked != expected_sum) {
//                 std::cout << "[FAIL] sum_bit=" << bit << " col=" << col
//                           << " expected=0x" << std::hex << expected_sum
//                           << " actual_masked=0x" << actual_sum_masked
//                           << " actual_raw=0x" << actual_sum
//                           << std::dec << "\n";
//                 return false;
//             }

//             carry = next_carry;
//         }

//         uint64_t actual_carry = 0;
//         if (!hw.read_row_col(bank, cout_row, col, actual_carry)) {
//             std::cout << "[ERROR] CXL.mem read failed: " << hw.last_error() << "\n";
//             return false;
//         }
//         const uint64_t actual_carry_masked =
//             actual_carry & valid_mask;
//         if (actual_carry_masked != carry) {
//             std::cout << "[FAIL] carry col=" << col
//                       << " expected=0x" << std::hex << carry
//                       << " actual_masked=0x" << actual_carry_masked
//                       << " actual_raw=0x" << actual_carry
//                       << std::dec << "\n";
//             return false;
//         }
//     }

//     std::cout << "[PASS] ADD_n_bit_via_Maj3 verified all 1024 x 64-bit columns.\n";
//     return true;
// }


bool run_add_4_bit_test(CxlHardware& hw) {
    const uint32_t bank = 0;
    const uint32_t mat0 = 1;
    const uint32_t mat1 = mat0 + 1;
    const uint64_t valid_mask = not_mask_for_mat_pair(mat0);

    const std::vector<uint32_t> a_rows = {
        make_global_row(mat0, 0),
        make_global_row(mat0, 1),
        make_global_row(mat0, 2),
        make_global_row(mat0, 3)
    };
    const std::vector<uint32_t> b_rows = {
        make_global_row(mat0, 10),
        make_global_row(mat0, 11),
        make_global_row(mat0, 12),
        make_global_row(mat0, 13)
    };
    const uint32_t cin_row = make_global_row(mat0, 290);
    const std::vector<uint32_t> sum_rows = {
        make_global_row(mat1, 600),
        make_global_row(mat1, 601),
        make_global_row(mat1, 602),
        make_global_row(mat1, 603)
    };
    const uint32_t cout_row = make_global_row(mat1, 604);

    std::cout << "\n===== CXL ADD_4_bit TEST =====\n";
    std::cout << "[INFO] valid NOT lanes mask: 0x"
              << std::hex << valid_mask << std::dec << "\n";

    // ADD_4_bit allocates internal scratch RA groups near the start of MAT0/MAT1.
    // Clear them so hardware MAJ/RowCopy operations do not depend on stale DRAM data.
    if (!fill_local_row_range(hw, bank, mat0, 0, 32, 0, "ADD MAT0 scratch") ||
        !fill_local_row_range(hw, bank, mat1, 0, 32, 0, "ADD MAT1 scratch")) {
        return false;
    }

    for (uint32_t col = 0; col < 1024; ++col) {
        for (uint32_t bit = 0; bit < 4; ++bit) {
            const uint64_t a =
                test_value(0x1000ULL + bit, col) & valid_mask;
            const uint64_t b =
                test_value(0x2000ULL + bit, col) & valid_mask;

            if (!hw.write_row_col(bank, a_rows[bit], col, a) ||
                !hw.write_row_col(bank, b_rows[bit], col, b) ||
                !hw.write_row_col(bank, a_rows[bit] + ROWS_PER_MAT, col, a) ||
                !hw.write_row_col(bank, b_rows[bit] + ROWS_PER_MAT, col, b) ||
                !hw.write_row_col(bank, sum_rows[bit], col, 0)) {
                std::cout << "[ERROR] CXL.mem prefill failed: " << hw.last_error() << "\n";
                return false;
            }
        }

        const uint64_t cin = test_value(0x3000ULL, col) & valid_mask;
        if (!hw.write_row_col(bank, cin_row, col, cin) ||
            !hw.write_row_col(bank, cin_row + ROWS_PER_MAT, col, cin) ||
            !hw.write_row_col(bank, make_global_row(mat0, 1182), col, 0) ||
            !hw.write_row_col(bank, make_global_row(mat0, 1183), col, valid_mask) ||
            !hw.write_row_col(bank, make_global_row(mat1, 1182), col, 0) ||
            !hw.write_row_col(bank, make_global_row(mat1, 1183), col, valid_mask) ||
            !hw.write_row_col(bank, cout_row, col, 0)) {
            std::cout << "[ERROR] CXL.mem prefill failed: " << hw.last_error() << "\n";
            return false;
        }
    }

    std::vector<uint64_t> src1;
    std::vector<uint64_t> src2;
    std::vector<uint64_t> sum;
    for (uint32_t bit = 0; bit < 4; ++bit) {
        src1.push_back(make_addr(bank, a_rows[bit]));
        src2.push_back(make_addr(bank, b_rows[bit]));
        sum.push_back(make_addr(bank, sum_rows[bit]));
    }

    const std::vector<uint32_t> insts = ADD_4_bit(
        src1,
        src2,
        make_addr(bank, cin_row),
        sum,
        make_addr(bank, cout_row)
    );

    if (insts.empty()) {
        std::cout << "[ERROR] ADD_4_bit u-op generation failed.\n";
        return false;
    }
    if (!hw.execute(insts)) {
        std::cout << "[ERROR] CXL.io execution failed: " << hw.last_error() << "\n";
        return false;
    }

    const std::string report_path = "add_4bit_dest_rows.csv";
    std::ofstream report(report_path);
    if (!report) {
        std::cout << "[ERROR] Cannot create report file: " << report_path << "\n";
        return false;
    }

    report << "test,ADD_4_bit\n";
    report << "n_bits,4\n";
    report << "bank," << bank << "\n";
    report << "mat0," << mat0 << "\n";
    report << "mat1," << mat1 << "\n";
    report << "valid_mask,0x" << std::hex << std::setw(16) << std::setfill('0')
           << valid_mask << std::dec << "\n";
    report << "uops," << insts.size() << "\n";
    for (uint32_t bit = 0; bit < 4; ++bit) {
        report << "sum_bit_" << bit << "_row," << sum_rows[bit] << "\n";
    }
    report << "cout_row," << cout_row << "\n";
    report << "\n";
    report << "kind,bit,col,dest_row,expected,actual_masked,actual_raw,diff,bit_errors,valid_bits,bit_accuracy_percent,pass\n";

    uint32_t mismatch_count = 0;
    uint64_t total_bit_errors = 0;
    uint64_t total_valid_bits = 0;
    const uint32_t valid_bits_per_word = popcount64(valid_mask);
    bool printed_first_mismatch = false;

    auto write_report_line = [&](const char* kind,
                                 uint32_t bit,
                                 uint32_t col,
                                 uint32_t dest_row,
                                 uint64_t expected,
                                 uint64_t actual_masked,
                                 uint64_t actual_raw) {
        const uint64_t diff = (expected ^ actual_masked) & valid_mask;
        const uint32_t bit_errors = popcount64(diff);
        const double bit_accuracy =
            valid_bits_per_word == 0
                ? 100.0
                : 100.0 * static_cast<double>(valid_bits_per_word - bit_errors) /
                    static_cast<double>(valid_bits_per_word);
        total_bit_errors += bit_errors;
        total_valid_bits += valid_bits_per_word;

        report << kind << ','
               << std::dec << bit << ','
               << col << ','
               << dest_row << ','
               << "0x" << std::hex << std::setw(16) << std::setfill('0') << expected << ','
               << "0x" << std::setw(16) << actual_masked << ','
               << "0x" << std::setw(16) << actual_raw << ','
               << "0x" << std::setw(16) << diff << ','
               << std::dec << bit_errors << ','
               << valid_bits_per_word << ','
               << std::fixed << std::setprecision(4) << bit_accuracy << ','
               << (diff == 0 ? "PASS" : "FAIL")
               << std::defaultfloat << std::dec << "\n";
    };

    for (uint32_t col = 0; col < 1024; ++col) {
        uint64_t carry = test_value(0x3000ULL, col) & valid_mask;

        for (uint32_t bit = 0; bit < 4; ++bit) {
            const uint64_t a =
                test_value(0x1000ULL + bit, col) & valid_mask;
            const uint64_t b =
                test_value(0x2000ULL + bit, col) & valid_mask;
            const uint64_t expected_sum =
                (a ^ b ^ carry) & valid_mask;
            const uint64_t next_carry =
                majority3(a, b, carry) & valid_mask;
            uint64_t actual_sum = 0;

            if (!hw.read_row_col(bank, sum_rows[bit], col, actual_sum)) {
                std::cout << "[ERROR] CXL.mem read failed: " << hw.last_error() << "\n";
                return false;
            }
            const uint64_t actual_sum_masked =
                actual_sum & valid_mask;
            write_report_line(
                "sum",
                bit,
                col,
                sum_rows[bit],
                expected_sum,
                actual_sum_masked,
                actual_sum
            );
            if (actual_sum_masked != expected_sum) {
                ++mismatch_count;
                if (!printed_first_mismatch) {
                    std::cout << "[FAIL] sum_bit=" << bit << " col=" << col
                              << " expected=0x" << std::hex << expected_sum
                              << " actual_masked=0x" << actual_sum_masked
                              << " actual_raw=0x" << actual_sum
                              << std::dec << "\n";
                    printed_first_mismatch = true;
                }
            }

            carry = next_carry;
        }

        uint64_t actual_carry = 0;
        if (!hw.read_row_col(bank, cout_row, col, actual_carry)) {
            std::cout << "[ERROR] CXL.mem read failed: " << hw.last_error() << "\n";
            return false;
        }
        const uint64_t actual_carry_masked =
            actual_carry & valid_mask;
        write_report_line(
            "carry",
            4,
            col,
            cout_row,
            carry,
            actual_carry_masked,
            actual_carry
        );
        if (actual_carry_masked != carry) {
            ++mismatch_count;
            if (!printed_first_mismatch) {
                std::cout << "[FAIL] carry col=" << col
                          << " expected=0x" << std::hex << carry
                          << " actual_masked=0x" << actual_carry_masked
                          << " actual_raw=0x" << actual_carry
                          << std::dec << "\n";
                printed_first_mismatch = true;
            }
        }
    }

    const double overall_bit_accuracy =
        total_valid_bits == 0
            ? 100.0
            : 100.0 * static_cast<double>(total_valid_bits - total_bit_errors) /
                static_cast<double>(total_valid_bits);

    report << "\n";
    report << "summary,total_valid_bits," << total_valid_bits << "\n";
    report << "summary,total_bit_errors," << total_bit_errors << "\n";
    report << "summary,bit_accuracy_percent,"
           << std::fixed << std::setprecision(6) << overall_bit_accuracy
           << std::defaultfloat << "\n";
    report << "summary,mismatched_rows," << mismatch_count << "\n";

    std::cout << "[INFO] destination row report: " << report_path << "\n";
    std::cout << "[INFO] bit_errors: " << total_bit_errors
              << " / " << total_valid_bits << "\n";
    std::cout << "[INFO] bit_accuracy_percent: "
              << std::fixed << std::setprecision(6) << overall_bit_accuracy
              << std::defaultfloat << "\n";
    if (mismatch_count != 0) {
        std::cout << "[FAIL] ADD_4_bit mismatches: "
                  << mismatch_count << "\n";
        return false;
    }

    std::cout << "[PASS] ADD_4_bit verified all 1024 x 64-bit columns.\n";
    return true;
}

} // namespace

int main() {
    CxlHardwareConfig config;
    config.bdf = "0000:b8:00.0";
    config.bar_index = 2;
    config.dax_path = "/dev/dax1.0";

    CxlHardware hw(config);
    if (!hw.init()) {
        std::cout << "[ERROR] CXL initialization failed: " << hw.last_error() << "\n";
        return 1;
    }

    std::vector<TestSummary> summaries;
    bool all_passed = true;

    auto run_and_record = [&](const std::string& name,
                              const std::string& details,
                              auto&& test_fn) {
        const bool passed = test_fn();
        summaries.push_back({
            name,
            passed ? "PASS" : "FAIL",
            details
        });
        all_passed = all_passed && passed;
    };

    run_and_record("RowCopy", "1024 columns checked", [&]() {
        return run_rowcopy_test(hw);
    });
    run_and_record("MAJ3", "1024 columns checked", [&]() {
        return run_maj3_test(hw);
    });
    run_and_record("AND", "1024 columns checked", [&]() {
        return run_and_test(hw);
    });
    run_and_record("OR", "1024 columns checked", [&]() {
        return run_or_test(hw);
    });
    run_and_record("XOR", "1024 columns checked on valid NOT lanes", [&]() {
        return run_xor_test(hw);
    });
    run_and_record("ADD_n_bit_via_Maj3", "see add_*bit_via_maj3_dest_rows.csv", [&]() {
        return run_add_n_bit_via_maj3_test(hw);
    });
    run_and_record("ADD_n_bit_Maj5", "see add_*bit_maj5_dest_rows.csv", [&]() {
        return run_add_n_bit_maj5_test(hw);
    });
    // run_and_record("ADD_4_bit", "see add_4bit_dest_rows.csv", [&]() {
    //     return run_add_4_bit_test(hw);
    // });

    const std::string summary_path = "cxl_hw_test_summary.csv";
    write_summary_report(summaries, summary_path);

    std::cout << "\n===== CXL HW TEST SUMMARY =====\n";
    for (const auto& summary : summaries) {
        std::cout << '[' << summary.status << "] "
                  << summary.name << " - "
                  << summary.details << "\n";
    }
    std::cout << "[INFO] summary report: " << summary_path << "\n";

    return all_passed ? 0 : 1;
}