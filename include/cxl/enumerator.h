#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Information discovered about one CXL-capable DAX character device.
struct CxlDeviceInfo {
    std::string bdf;              // PCI Bus:Device.Function, e.g. "0000:b8:00.0"
    uint32_t    bar_index  = 2;   // BAR number used for CXL.io register access
    std::string dax_path;         // Character device path, e.g. "/dev/dax1.0"
    uint64_t    dax_size_bytes = 0;
};

// Scan the running Linux system for CXL-capable DAX devices via sysfs.
// Returns an empty vector on non-Linux platforms or if nothing is found.
std::vector<CxlDeviceInfo> enumerate_cxl_devices();

// Print the discovered device list to stdout.
void print_cxl_devices(const std::vector<CxlDeviceInfo>& devices);
