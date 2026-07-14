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

bool rows_equal(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) {
    return a == b;
}
