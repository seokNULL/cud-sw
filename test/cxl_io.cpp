#include "../include/cxl/io.h"
#include "../include/cxl/enumerator.h"

#include <cstdint>
#include <iomanip>
#include <iostream>

void run_cxl_io() {
    std::cout << "\n===== CXL.io BAR Register Test =====\n";

    const auto devices = enumerate_cxl_devices();
    if (devices.empty()) {
        std::cout << "[INFO] No CXL devices found — skipping.\n";
        return;
    }

    for (const auto& dev : devices) {
        std::cout << "\n[Device] BDF=" << dev.bdf
                  << "  BAR" << dev.bar_index << "\n";

        CxlIo io;
        if (!io.open(dev.bdf, dev.bar_index)) {
            std::cout << "  [WARN] open failed: " << io.last_error() << "\n";
            continue;
        }

        std::cout << "  BAR size = 0x" << std::hex << io.bar_size()
                  << std::dec << " bytes\n";

        // Print first 8 32-bit registers (first 32 bytes)
        const uint64_t print_regs = 8;
        const uint64_t limit = std::min<uint64_t>(print_regs * 4, io.bar_size());
        std::cout << "  Offset   Value\n";
        for (uint64_t off = 0; off + 4 <= limit; off += 4) {
            const uint32_t val = io.read32(off);
            std::cout << "  0x" << std::hex << std::setw(4) << std::setfill('0') << off
                      << "     0x" << std::setw(8) << std::setfill('0') << val
                      << std::dec << "\n";
        }
    }

    std::cout << "\n===== DONE =====\n";
}
