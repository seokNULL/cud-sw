#pragma once
#include <cstdint>

// Fixed compute-row addresses for MAJ3 (mode 0: RA3 and RA0 don't-care).
// Group pattern {n, n+1, n+8, n+9} anchored at n = 0x0010.
static constexpr uint32_t kCmpRow0    = 0x0010u;
static constexpr uint32_t kCmpRow1    = 0x0011u;
static constexpr uint32_t kCmpRow2    = 0x0018u;
static constexpr uint32_t kCmpRowFrac = 0x0019u;
static constexpr uint32_t kCmpFracPos = 3u;
static constexpr uint32_t kCmpMode    = 0u;
