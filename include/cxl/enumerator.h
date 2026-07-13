#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ── CXL.mem (DAX) device ─────────────────────────────────────────────────────

struct CxlDeviceInfo {
    std::string bdf;              // PCI BDF, e.g. "0000:b8:00.0" (may be empty)
    uint32_t    bar_index  = 2;
    std::string dax_path;         // e.g. "/dev/dax1.0"
    uint64_t    dax_size_bytes = 0;
};

// Scan /sys/bus/dax/devices/dax* for CXL DAX character devices.
std::vector<CxlDeviceInfo> enumerate_cxl_devices();
void print_cxl_devices(const std::vector<CxlDeviceInfo>& devices);

// ── CXL.io (PCI BAR) device ──────────────────────────────────────────────────

struct CxlIoDevice {
    std::string bdf;          // PCI BDF, e.g. "0000:b8:00.0"
    uint32_t    bar_index = 2;
};

// Scan for CXL devices accessible via PCI BAR (CXL.io path).
// Primary:   /sys/bus/pci/devices/<bdf>/class — class code 0x0502xx
// Secondary: /sys/bus/cxl/devices/mem* realpath walk
// BAR index: detected via open()+fstat() on each resource file.
std::vector<CxlIoDevice> enumerate_cxl_io_devices();
