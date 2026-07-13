#include "../include/cxl/io.h"
#include "../include/cxl/enumerator.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

static std::string read_sysfs(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::string s;
    std::getline(f, s);
    return s;
}

void run_cxl_io() {
    std::cout << "\n===== CXL.io BAR Register Test =====\n";

    const auto devices = enumerate_cxl_io_devices();
    if (devices.empty()) {
        std::cout << "[INFO] No CXL devices found.\n"
                  << "       Scanned: /sys/bus/pci/devices/ (class 0x0502xx)\n"
                  << "            +   /sys/bus/cxl/devices/mem*\n";
        return;
    }
    std::cout << "[INFO] Found " << devices.size() << " CXL device(s).\n";

    for (const auto& dev : devices) {
        const std::string resource_path = "/sys/bus/pci/devices/" + dev.bdf +
                                          "/resource" + std::to_string(dev.bar_index);
        std::cout << "\n[Device] BDF=" << dev.bdf
                  << "  BAR" << dev.bar_index << "\n"
                  << "  resource: " << resource_path << "\n";

        // Print vendor/device/class for context
        std::cout << "  vendor=" << read_sysfs("/sys/bus/pci/devices/" + dev.bdf + "/vendor")
                  << "  device=" << read_sysfs("/sys/bus/pci/devices/" + dev.bdf + "/device")
                  << "  class="  << read_sysfs("/sys/bus/pci/devices/" + dev.bdf + "/class")
                  << "\n";

        CxlIo io;
        if (!io.open(dev.bdf, dev.bar_index)) {
            std::cout << "  [FAIL] " << io.last_error() << "\n"
                      << "  NOTE: if error is EINVAL on mmap, the driver may have\n"
                      << "        claimed the BAR exclusively (pci_request_regions_exclusive).\n"
                      << "        Unbind the driver first:\n"
                      << "          echo " << dev.bdf
                      << " > /sys/bus/pci/drivers/<driver>/unbind\n";
            continue;
        }

        std::cout << "  BAR size = 0x" << std::hex << io.bar_size()
                  << std::dec << " bytes\n";

        // Dump first 8 32-bit registers (first 32 bytes of the BAR)
        const uint64_t limit = std::min<uint64_t>(8 * 4, io.bar_size());
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
