#include "../../include/cxl/enumerator.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

void print_cxl_devices(const std::vector<CxlDeviceInfo>& devices) {
    if (devices.empty()) {
        std::cout << "[INFO] No CXL DAX devices found.\n";
        return;
    }
    std::cout << "[INFO] Found " << devices.size() << " CXL DAX device(s):\n";
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& d = devices[i];
        std::cout << "  [" << i << "]"
                  << "  bdf=" << (d.bdf.empty() ? "(n/a)" : d.bdf)
                  << "  bar=" << d.bar_index
                  << "  dax=" << d.dax_path
                  << "  size=" << (d.dax_size_bytes >> 20) << " MiB\n";
    }
}

namespace {

std::string read_attr(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::string s;
    std::getline(f, s);
    return s;
}

// Parse one line of /sys/bus/pci/devices/<bdf>/resource:
// format: "0xSTART 0xEND 0xFLAGS"
// Returns BAR size = end - start + 1, or 0 if the BAR is absent/IO.
size_t parse_resource_line(const std::string& line) {
    try {
        size_t pos  = 0;
        uint64_t start = std::stoull(line, &pos, 16);
        while (pos < line.size() && line[pos] == ' ') ++pos;
        size_t pos2 = 0;
        uint64_t end   = std::stoull(line.substr(pos), &pos2, 16);
        pos += pos2;
        while (pos < line.size() && line[pos] == ' ') ++pos;
        uint64_t flags = std::stoull(line.substr(pos), nullptr, 16);
        // Bit 0 = PCI_IORESOURCE_IO; skip IO BARs and absent BARs.
        if (start == 0 || end < start || (flags & 0x1)) return 0;
        return static_cast<size_t>(end - start + 1);
    } catch (...) { return 0; }
}

// Read BAR size for bar_index from the text resource file.
size_t read_bar_size(const std::string& bdf, uint32_t bar_index) {
    const std::string path = "/sys/bus/pci/devices/" + bdf + "/resource";
    std::ifstream f(path);
    if (!f) return 0;
    std::string line;
    for (uint32_t i = 0; std::getline(f, line); ++i) {
        if (i == bar_index) return parse_resource_line(line);
    }
    return 0;
}

// Walk directory components of real_path from right to left.
// Return the first component that matches BDF format AND exists under
// /sys/bus/pci/devices/.
std::string extract_bdf(const std::string& real_path) {
    static const std::regex kBdf("[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-9a-f]");
    std::string path = real_path;
    while (!path.empty() && path != "/") {
        const size_t slash = path.rfind('/');
        if (slash == std::string::npos) break;
        const std::string component = path.substr(slash + 1);
        if (std::regex_match(component, kBdf)) {
            struct stat st{};
            const std::string pci = "/sys/bus/pci/devices/" + component;
            if (stat(pci.c_str(), &st) == 0)
                return component;
        }
        path = path.substr(0, slash);
    }
    return "";
}

uint64_t read_dax_size(const std::string& dax_name) {
    const std::string path = "/sys/bus/dax/devices/" + dax_name + "/size";
    const std::string val  = read_attr(path);
    if (val.empty()) return 0;
    try { return std::stoull(val, nullptr, 0); } catch (...) { return 0; }
}

// Find the first memory BAR index with non-zero size, trying 2, 0, 4.
uint32_t detect_bar_index(const std::string& bdf) {
    for (uint32_t idx : {2u, 0u, 4u}) {
        if (read_bar_size(bdf, idx) > 0) return idx;
    }
    return 2;
}

} // namespace

std::vector<CxlDeviceInfo> enumerate_cxl_devices() {
    std::vector<CxlDeviceInfo> result;

    const char* const kDaxBus = "/sys/bus/dax/devices";
    DIR* dir = opendir(kDaxBus);
    if (!dir) return result;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const std::string name(ent->d_name);
        if (name.rfind("dax", 0) != 0) continue;

        const std::string link = std::string(kDaxBus) + "/" + name;
        char real_buf[PATH_MAX] = {};
        const std::string real_path =
            realpath(link.c_str(), real_buf) ? real_buf : link;

        CxlDeviceInfo info;
        info.bdf            = extract_bdf(real_path);
        info.bar_index      = info.bdf.empty() ? 2u : detect_bar_index(info.bdf);
        info.dax_path       = "/dev/" + name;
        info.dax_size_bytes = read_dax_size(name);

        result.push_back(std::move(info));
    }
    closedir(dir);

    std::sort(result.begin(), result.end(),
              [](const CxlDeviceInfo& a, const CxlDeviceInfo& b) {
                  return a.dax_path < b.dax_path;
              });

    return result;
}
