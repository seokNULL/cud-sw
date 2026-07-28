#include "../../../include/cud/compute_lib/data_mapper.h"
#include "../../../include/cxl/address_map.h"

uint64_t BitSerialLayout::plane_pa(uint32_t bit, uint32_t col) const {
    return encode_dram_addr({0, bank, plane_row(bit), col});
}

