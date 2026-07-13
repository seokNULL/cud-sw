#include "../include/cxl/io.h"

#include <algorithm>
#include <cstdint>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <regex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string read_sysfs(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::string s;
    std::getline(f, s);
    return s;
}

// ── BDF discovery ─────────────────────────────────────────────────────────────

// Primary: scan /sys/bus/pci/devices/ and filter by CXL class code.
// PCI_CLASS_MEMORY_CXL = 0x050210 (class=05h Memory, subclass=02h CXL, any prog-if).
// This is the most reliable method — it works regardless of how the device
// is exposed in the CXL or DAX subsystem hierarchy.
static std::vector<std::string> scan_by_pci_class() {
    std::vector<std::string> result;
    DIR* dir = opendir("/sys/bus/pci/devices");
    if (!dir) return result;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const std::string bdf(ent->d_name);
        const std::string val = read_sysfs("/sys/bus/pci/devices/" + bdf + "/class");
        if (val.empty()) continue;
        try {
            // class file: "0xCCSSSP" (class | subclass | prog-if, 3 bytes)
            // CXL memory controller: class=0x05, subclass=0x02
            const uint32_t cls = std::stoul(val, nullptr, 16);
            if ((cls >> 8) == 0x0502)
                result.push_back(bdf);
        } catch (...) {}
    }
    closedir(dir);
    return result;
}

// Secondary: scan /sys/bus/cxl/devices/mem* and resolve parent PCI BDF.
// CXL mem devices are registered as direct children of their PCIe endpoint,
// so the realpath reliably contains the BDF as a directory component.
// Used as a fallback in case the class code scan misses any device.
static std::vector<std::string> scan_by_cxl_bus() {
    std::vector<std::string> result;
    DIR* dir = opendir("/sys/bus/cxl/devices");
    if (!dir) return result;

    static const std::regex kBdf("[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-9a-f]");
    static const std::regex kMem("mem[0-9]+");

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const std::string name(ent->d_name);
        if (!std::regex_match(name, kMem)) continue;

        char real_buf[PATH_MAX] = {};
        if (!realpath(("/sys/bus/cxl/devices/" + name).c_str(), real_buf)) continue;

        // Walk path right-to-left; return the rightmost BDF that exists under
        // /sys/bus/pci/devices/ (= the endpoint, not a root port or bridge).
        std::string path(real_buf);
        while (!path.empty() && path != "/") {
            const size_t slash = path.rfind('/');
            if (slash == std::string::npos) break;
            const std::string comp = path.substr(slash + 1);
            if (std::regex_match(comp, kBdf)) {
                struct stat st{};
                if (stat(("/sys/bus/pci/devices/" + comp).c_str(), &st) == 0) {
                    result.push_back(comp);
                    break;
                }
            }
            path = path.substr(0, slash);
        }
    }
    closedir(dir);
    return result;
}

// ── BAR detection ─────────────────────────────────────────────────────────────

// Detect the first valid memory BAR for the given BDF by opening each resource
// file and calling fstat() — the kernel sets st_size = BAR size for PCI
// resource files, which is the same technique used by the user's working init().
// Tries BAR 2 first (CXL Component Register Interface is commonly BAR 2),
// then BAR 0, then BAR 4.
static uint32_t detect_bar(const std::string& bdf) {
    for (uint32_t idx : {2u, 0u, 4u}) {
        const std::string path = "/sys/bus/pci/devices/" + bdf +
                                 "/resource" + std::to_string(idx);
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) continue;
        struct stat st{};
        const bool valid = (fstat(fd, &st) == 0 && st.st_size > 0);
        ::close(fd);
        if (valid) return idx;
    }
    return 2; // default — will surface an error later via CxlIo::open()
}

// ── Device list ───────────────────────────────────────────────────────────────

struct CxlIoDevice {
    std::string bdf;
    uint32_t    bar_index = 2;
};

static std::vector<CxlIoDevice> find_cxl_io_devices() {
    // Merge class-code scan (primary) with CXL-bus scan (secondary).
    auto bdfs = scan_by_pci_class();
    for (const auto& b : scan_by_cxl_bus()) {
        if (std::find(bdfs.begin(), bdfs.end(), b) == bdfs.end())
            bdfs.push_back(b);
    }
    std::sort(bdfs.begin(), bdfs.end());
    bdfs.erase(std::unique(bdfs.begin(), bdfs.end()), bdfs.end());

    std::vector<CxlIoDevice> result;
    result.reserve(bdfs.size());
    for (const auto& bdf : bdfs)
        result.push_back({bdf, detect_bar(bdf)});
    return result;
}

// ── Test entry point ──────────────────────────────────────────────────────────

void run_cxl_io() {
    std::cout << "\n===== CXL.io BAR Register Test =====\n";

    const auto devices = find_cxl_io_devices();
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
