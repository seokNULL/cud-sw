#include "utils.h"

#include <iomanip>
#include <iostream>

void print_row(const char* label, const std::vector<uint64_t>& row,
               size_t preview_cols) {
    std::cout << label;
    const size_t n = std::min(row.size(), preview_cols);
    for (size_t i = 0; i < n; ++i)
        std::cout << " 0x" << std::hex << std::setw(16) << std::setfill('0') << row[i];
    if (row.size() > n)
        std::cout << " ...";
    std::cout << std::dec << "\n";
}

size_t check_rows(const std::vector<uint64_t>& expected,
                  const std::vector<uint64_t>& got,
                  size_t max_print) {
    size_t total_bits = 0;
    size_t total_cols = 0;
    size_t cols_shown = 0;
    const size_t n = std::min(expected.size(), got.size());

    for (size_t col = 0; col < n; ++col) {
        uint64_t diff = expected[col] ^ got[col];
        if (!diff) continue;

        ++total_cols;

        // Count bits in this column
        for (uint64_t d = diff; d; d &= d - 1) ++total_bits;

        if (cols_shown < max_print) {
            std::cout << "  [col " << std::setw(4) << col << "]"
                      << " exp=0x" << std::hex << std::setw(16) << std::setfill('0') << expected[col]
                      << " got=0x" << std::setw(16) << std::setfill('0') << got[col]
                      << std::dec << std::setfill(' ') << "\n";
            for (uint64_t d = diff; d; d &= d - 1) {
                const int bit     = __builtin_ctzll(d);
                const int exp_bit = (expected[col] >> bit) & 1;
                const int got_bit = (got[col]      >> bit) & 1;
                std::cout << "    bit " << std::setw(2) << bit
                          << ": exp=" << exp_bit << " got=" << got_bit << "\n";
            }
            ++cols_shown;
        }
    }
    if (total_cols > cols_shown)
        std::cout << "  ... (" << (total_cols - cols_shown) << " more column(s) not shown)\n";
    if (total_bits > 0)
        std::cout << "  total: " << total_bits << " bit error(s) in "
                  << total_cols << " column(s)\n";
    return total_bits;
}
