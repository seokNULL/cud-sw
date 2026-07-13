#include "../include/cxl/io.h"

#include <algorithm>
#include <cstdint>
#include <dirent.h>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <regex>
#include <sys/stat.h>
#include <unistd.h>

struct CxlIoDevice {
    std::string bdf;
    uint32_t    bar_index = 2;
};

// Scan /sys/bus/cxl/devices/mem* to find CXL memory endpoint BDFs.
//
// CXL mem devices are registered as direct children of their PCIe endpoint
// in the kernel device tree, so realpath() of a mem* entry always contains
// the endpoint BDF as a path component (e.g. .../0000:b8:00.0/cxlmem0).
// This is more reliable than inferring BDF from the DAX device path, which
// goes through extra CXL subsystem layers (decoder/region/dax_region) that
// may not contain the BDF depending on kernel version and topology.
static std::vector<CxlIoDevice> find_cxl_io_devices() {
    std::vector<CxlIoDevice> result;

    DIR* dir = opendir("/sys/bus/cxl/devices");
    if (!dir) return result;

    static const std::regex kBdf("[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-9a-f]");
    static const std::regex kMem("mem[0-9]+");

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const std::string name(ent->d_name);
        if (!std::regex_match(name, kMem)) continue;

        char real_buf[PATH_MAX] = {};
        const std::string link = "/sys/bus/cxl/devices/" + name;
        if (!realpath(link.c_str(), real_buf)) continue;

        // Walk path right-to-left; first BDF component that exists under
        // /sys/bus/pci/devices/ is the endpoint BDF.
        std::string path(real_buf);
        std::string bdf;
        while (!path.empty() && path != "/") {
            const size_t slash = path.rfind('/');
            if (slash == std::string::npos) break;
            const std::string comp = path.substr(slash + 1);
            if (std::regex_match(comp, kBdf)) {
                struct stat st{};
                if (stat(("/sys/bus/pci/devices/" + comp).c_str(), &st) == 0) {
                    bdf = comp;
                    break;
                }
            }
            path = path.substr(0, slash);
        }
        if (bdf.empty()) continue;

        // Detect BAR: open each resource file and fstat for non-zero size.
        uint32_t bar_idx = 2;
        for (uint32_t idx : {2u, 0u, 4u}) {
            const std::string rpath = "/sys/bus/pci/devices/" + bdf +
                                      "/resource" + std::to_string(idx);
            int fd = ::open(rpath.c_str(), O_RDONLY);
            if (fd < 0) continue;
            struct stat st{};
            const bool ok = (fstat(fd, &st) == 0 && st.st_size > 0);
            ::close(fd);
            if (ok) { bar_idx = idx; break; }
        }

        result.push_back({bdf, bar_idx});
    }
    closedir(dir);

    // Deduplicate: multiple mem* entries can share the same endpoint BDF.
    std::sort(result.begin(), result.end(),
              [](const CxlIoDevice& a, const CxlIoDevice& b) {
                  return a.bdf < b.bdf;
              });
    result.erase(std::unique(result.begin(), result.end(),
                             [](const CxlIoDevice& a, const CxlIoDevice& b) {
                                 return a.bdf == b.bdf;
                             }),
                 result.end());

    return result;
}

void run_cxl_io() {
    std::cout << "\n===== CXL.io BAR Register Test =====\n";

    const auto devices = find_cxl_io_devices();
    if (devices.empty()) {
        std::cout << "[INFO] No CXL devices found under /sys/bus/cxl/devices/.\n";
        return;
    }

    for (const auto& dev : devices) {
        const std::string res_path = "/sys/bus/pci/devices/" + dev.bdf
                                   + "/resource" + std::to_string(dev.bar_index);
        std::cout << "\n[Device] BDF=" << dev.bdf
                  << "  BAR" << dev.bar_index << "\n"
                  << "  path: " << res_path << "\n";

        CxlIo io;
        if (!io.open(dev.bdf, dev.bar_index)) {
            std::cout << "  [FAIL] " << io.last_error() << "\n";
            continue;
        }

        std::cout << "  BAR size = 0x" << std::hex << io.bar_size()
                  << std::dec << " bytes\n";

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
