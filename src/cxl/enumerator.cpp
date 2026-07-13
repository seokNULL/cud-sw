#include "../../include/cxl/enumerator.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#ifdef __linux__
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

#ifndef __linux__

std::vector<CxlDeviceInfo> enumerate_cxl_devices() { return {}; }

#else

namespace {

// Read the first line of a sysfs attribute file.
std::string read_attr(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::string s;
    std::getline(f, s);
    return s;
}

// Extract the last BDF token (XXXX:XX:XX.X) from a sysfs path string.
// There can be multiple PCI bridges in the path; we want the leaf device.
std::string extract_bdf(const std::string& path) {
    static const std::regex kBdf("[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-9a-f]");
    std::smatch m;
    std::string tail = path;
    std::string last;
    while (std::regex_search(tail, m, kBdf)) {
        last = m[0].str();
        tail = m.suffix().str();
    }
    return last;
}

// Read the DAX device capacity from its sysfs 'size' attribute.
uint64_t read_dax_size(const std::string& dax_name) {
    const std::string path = "/sys/bus/dax/devices/" + dax_name + "/size";
    const std::string val  = read_attr(path);
    if (val.empty()) return 0;
    try { return std::stoull(val, nullptr, 0); } catch (...) { return 0; }
}

// Probe resource{0,2,4} sysfs files; return the index of the first non-empty one.
// The sysfs resource file's st_size == BAR window size in bytes.
uint32_t detect_bar_index(const std::string& bdf) {
    const std::string base = "/sys/bus/pci/devices/" + bdf + "/resource";
    for (uint32_t idx : {2u, 0u, 4u}) {
        struct stat st{};
        if (stat((base + std::to_string(idx)).c_str(), &st) == 0 && st.st_size > 0) {
            return idx;
        }
    }
    return 2; // CXL devices conventionally use BAR2
}

} // namespace

std::vector<CxlDeviceInfo> enumerate_cxl_devices() {
    std::vector<CxlDeviceInfo> result;

    // Walk /sys/bus/dax/devices/ looking for dax* entries.
    const char* const kDaxBus = "/sys/bus/dax/devices";
    DIR* dir = opendir(kDaxBus);
    if (!dir) return result;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const std::string name(ent->d_name);
        // DAX device names look like dax0.0, dax1.0, dax1.1, …
        if (name.rfind("dax", 0) != 0) continue;

        // Resolve the symlink to its canonical sysfs path so we can extract
        // the PCI BDF from the path components.
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

#endif // __linux__
