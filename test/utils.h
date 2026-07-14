#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Print the first few values of a row with a label.
void print_row(const char* label, const std::vector<uint64_t>& row,
               size_t preview_cols = 4);

// Return true if two row vectors are element-wise equal.
bool rows_equal(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b);
