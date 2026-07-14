#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Print the first few values of a row with a label.
void print_row(const char* label, const std::vector<uint64_t>& row,
               size_t preview_cols = 4);

// Compare two rows bit by bit.  Prints each differing bit's column and bit
// position (up to max_print entries), followed by a total error count.
// Returns the total number of differing bits (0 = identical).
size_t check_rows(const std::vector<uint64_t>& expected,
                  const std::vector<uint64_t>& got,
                  size_t max_print = 16);
