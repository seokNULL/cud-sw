#include "../../include/cxl/enumerator.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
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
// "0xSTART 0xEND 0xFLAGS" → returns end - start + 1, or 0.
size_t parse_resource_line(const std::string& line) {
    try {
        size_t pos = 0;
        uint64_t start = std::stoull(line, &pos, 16);
        while (pos < line.size() && line[pos] == ' ') ++pos;
        size_t pos2 = 0;
        uint64_t end = std::stoull(line.substr(pos), &pos2, 16);
        pos += pos2;
        while (pos < line.size() && line[pos] == ' ') ++pos;
        uint64_t flags = std::stoull(line.substr(pos), nullptr, 16);
        if (start == 0 || end < start || (flags & 0x1)) return 0;
        return static_cast<size_t>(end - start + 1);
    } catch (...) { return 0; }
}

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

// ── CXL.io device enumeration ─────────────────────────────────────────────────

namespace {

// Scan /sys/bus/pci/devices/<bdf>/class; keep entries where (class >> 8) == 0x0502
// (PCI_CLASS_MEMORY_CXL = 0x050210 — class=05h Memory, subclass=02h CXL).
static std::vector<std::string> io_scan_by_pci_class() {
    std::vector<std::string> result;
    DIR* dir = opendir("/sys/bus/pci/devices");
    if (!dir) return result;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const std::string bdf(ent->d_name);
        const std::string val = read_attr("/sys/bus/pci/devices/" + bdf + "/class");
        if (val.empty()) continue;
        try {
            if ((std::stoul(val, nullptr, 16) >> 8) == 0x0502)
                result.push_back(bdf);
        } catch (...) {}
    }
    closedir(dir);
    return result;
}

// Walk /sys/bus/cxl/devices/mem* → realpath → extract BDF component.
static std::vector<std::string> io_scan_by_cxl_bus() {
    std::vector<std::string> result;
    DIR* dir = opendir("/sys/bus/cxl/devices");
    if (!dir) return result;
    static const std::regex kMem("mem[0-9]+");
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const std::string name(ent->d_name);
        if (!std::regex_match(name, kMem)) continue;
        char real_buf[PATH_MAX] = {};
        if (!realpath(("/sys/bus/cxl/devices/" + name).c_str(), real_buf)) continue;
        const std::string bdf = extract_bdf(real_buf);
        if (!bdf.empty()) result.push_back(bdf);
    }
    closedir(dir);
    return result;
}

// Detect the first valid BAR for bdf by open()+fstat() on each resource file.
// Tries BAR 2, 0, 4 in order (BAR 2 = CXL Component Register Interface).
static uint32_t io_detect_bar(const std::string& bdf) {
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
    return 2;
}

} // namespace (io helpers)

std::vector<CxlIoDevice> enumerate_cxl_io_devices() {
    // Merge PCI class scan (primary) with CXL bus scan (secondary).
    auto bdfs = io_scan_by_pci_class();
    for (const auto& b : io_scan_by_cxl_bus()) {
        if (std::find(bdfs.begin(), bdfs.end(), b) == bdfs.end())
            bdfs.push_back(b);
    }
    std::sort(bdfs.begin(), bdfs.end());
    bdfs.erase(std::unique(bdfs.begin(), bdfs.end()), bdfs.end());

    std::vector<CxlIoDevice> result;
    result.reserve(bdfs.size());
    for (const auto& bdf : bdfs)
        result.push_back({bdf, io_detect_bar(bdf)});
    return result;
}
