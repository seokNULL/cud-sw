#pragma once
#include <cstdint>
#include <array>

// Row positions within a MAJ3 group (dc0=lower don't-care bit, dc1=upper):
//   [0] dc0=0, dc1=0  -> input A (cmp0)
//   [1] dc0=1, dc1=0  -> input B (cmp1)
//   [2] dc0=0, dc1=1  -> input C / bias (cmp2)
//   [3] dc0=1, dc1=1  -> output / frac row
// frac_pos is always 3.

struct CmpModeDesc {
    uint32_t dc0;   // lower don't-care row-address bit index
    uint32_t dc1;   // upper don't-care row-address bit index
    uint32_t base;  // software-assigned base row (both dc bits = 0)
};

// mode -> don't-care bits and test base row
// base addresses are chosen so groups don't overlap; a proper allocator
// will replace these fixed values later.
static constexpr std::array<CmpModeDesc, 7> kCmpModes = {{
    {0, 3, 0x0010u},  // mode 0: bits 0,3 free -> offsets {0,1,8,9}
    {0, 4, 0x0020u},  // mode 1: bits 0,4 free -> offsets {0,1,16,17}
    {0, 5, 0x0040u},  // mode 2: bits 0,5 free -> offsets {0,1,32,33}
    {3, 4, 0x0100u},  // mode 3: bits 3,4 free -> offsets {0,8,16,24}
    {3, 5, 0x0200u},  // mode 4: bits 3,5 free -> offsets {0,8,32,40}
    {4, 5, 0x0400u},  // mode 5: bits 4,5 free -> offsets {0,16,32,48}
    {1, 2, 0x0008u},  // mode 6: bits 1,2 free -> offsets {0,2,4,6}
}};

static constexpr uint32_t kCmpFracPos = 3u;

// Returns the 4 row addresses for the given mode in position order [0..3].
inline std::array<uint32_t, 4> cmp_group_rows(uint32_t mode) {
    const auto& m = kCmpModes[mode];
    return {{
        m.base,
        m.base | (1u << m.dc0),
        m.base | (1u << m.dc1),
        m.base | (1u << m.dc0) | (1u << m.dc1),
    }};
}
