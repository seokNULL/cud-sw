#include "../../../include/cud/compute_lib/data_copy.h"
#include "../cud_inst_helpers.h"

// Single-row ROWCOPY primitive: [ROWCOPY_SRC(src_pa), ROWCOPY_DST(dst_pa)]
// No validation, no loop, no END — caller's responsibility.
std::vector<CudInst> cud_data_copy(uint64_t src_pa, uint64_t dst_pa) {
    std::vector<CudInst> insts;
    insts.push_back(cud_make_rowcopy_src(src_pa));
    insts.push_back(cud_make_rowcopy_dst(dst_pa));
    return insts;
}
