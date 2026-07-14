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
    size_t total   = 0;
    size_t printed = 0;
    const size_t n = std::min(expected.size(), got.size());

    for (size_t col = 0; col < n; ++col) {
        uint64_t diff = expected[col] ^ got[col];
        while (diff) {
            const int bit     = __builtin_ctzll(diff);
            const int exp_bit = (expected[col] >> bit) & 1;
            const int got_bit = (got[col]      >> bit) & 1;
            if (printed < max_print) {
                std::cout << "  [col " << std::setw(4) << col
                          << " bit " << std::setw(2) << bit
                          << "] exp=" << exp_bit << " got=" << got_bit << "\n";
                ++printed;
            }
            ++total;
            diff &= diff - 1;
        }
    }
    if (total > printed)
        std::cout << "  ... (" << (total - printed) << " more error(s) not shown)\n";
    if (total > 0)
        std::cout << "  total: " << total << " bit error(s)\n";
    return total;
}
