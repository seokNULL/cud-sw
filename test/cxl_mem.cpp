#include "../include/cxl/mem.h"
#include "../include/cxl/enumerator.h"

#include <cstdint>
#include <cstring>
#include <iostream>

void run_cxl_mem() {
    std::cout << "\n===== CXL.mem DAX Read/Write Test =====\n";

    const auto devices = enumerate_cxl_devices();
    if (devices.empty()) {
        std::cout << "[INFO] No CXL devices found — skipping.\n";
        return;
    }

    for (const auto& dev : devices) {
        if (dev.dax_path.empty()) {
            std::cout << "[Device] BDF=" << dev.bdf << "  no DAX path — skipping.\n";
            continue;
        }

        std::cout << "\n[Device] BDF=" << dev.bdf
                  << "  DAX=" << dev.dax_path << "\n";

        CxlMem mem;
        if (!mem.open(dev.dax_path, dev.dax_size_bytes)) {
            std::cout << "  [WARN] open failed: " << mem.last_error() << "\n";
            continue;
        }

        std::cout << "  size = 0x" << std::hex << mem.size() << std::dec << " bytes\n";

        // Write test pattern to first 16 cache lines (1 KB), flush, then read back.
        static constexpr uint64_t kStride    = 64;   // one cache line
        static constexpr uint64_t kTestLines = 16;
        static constexpr uint64_t kTestBytes = kStride * kTestLines;

        const uint64_t test_size = std::min(kTestBytes, mem.size());
        const uint64_t nwords    = test_size / 8;

        // Write
        for (uint64_t i = 0; i < nwords; ++i) {
            const uint64_t val = (i * 0x9E3779B97F4A7C15ULL) ^ 0xC0FFEE00DEADBEEFULL;
            mem.write64(i * 8, val);
        }
        mem.flush(0, test_size);

        // Invalidate CPU cache lines before read-back
        for (uint64_t off = 0; off < test_size; off += kStride)
            mem.flush(off, kStride);
        mem.fence();

        // Verify
        uint64_t failures = 0;
        for (uint64_t i = 0; i < nwords; ++i) {
            const uint64_t expected = (i * 0x9E3779B97F4A7C15ULL) ^ 0xC0FFEE00DEADBEEFULL;
            const uint64_t got      = mem.read64(i * 8);
            if (got != expected) {
                if (failures < 4)
                    std::cout << "  [FAIL] offset=0x" << std::hex << (i * 8)
                              << " expected=0x" << expected
                              << " got=0x" << got << std::dec << "\n";
                ++failures;
            }
        }

        if (failures == 0)
            std::cout << "  [PASS] " << nwords << " words verified.\n";
        else
            std::cout << "  [FAIL] " << failures << " / " << nwords << " mismatches.\n";
    }

    std::cout << "\n===== DONE =====\n";
}
