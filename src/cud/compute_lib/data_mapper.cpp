#include "../../../include/cud/compute_lib/data_mapper.h"
#include "../../../include/cxl/address_map.h"

uint64_t BitSerialLayout::plane_pa(uint32_t bit, uint32_t col) const {
    return encode_dram_addr({0, bank, plane_row(bit), col});
}

bool layout_valid(const BitSerialLayout& l) {
    if (l.bit_width == 0 || l.bit_width > 8) return false;
    if (l.num_elem == 0 || l.num_elem > static_cast<uint32_t>(NUM_COL)) return false;
    const uint32_t mat0 = row_to_mat(l.base_row);
    for (uint32_t b = 1; b < l.bit_width; ++b) {
        if (row_to_mat(l.plane_row(b)) != mat0) return false;
    }
    return true;
}

bool layouts_compatible(const BitSerialLayout& a, const BitSerialLayout& b) {
    if (a.bank != b.bank) return false;
    const uint32_t mat = row_to_mat(a.base_row);
    for (uint32_t i = 0; i < a.bit_width; ++i)
        if (row_to_mat(a.base_row + i) != mat) return false;
    for (uint32_t i = 0; i < b.bit_width; ++i)
        if (row_to_mat(b.base_row + i) != mat) return false;
    return true;
}
