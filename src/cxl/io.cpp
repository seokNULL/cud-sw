#include "../../include/cxl/io.h"

#include <cerrno>
#include <cstring>
#include <emmintrin.h>
#include <fcntl.h>
#include <fstream>
#include <sys/mman.h>
#include <unistd.h>

// Parse one line of /sys/bus/pci/devices/<bdf>/resource:
// "0xSTART 0xEND 0xFLAGS" → returns end - start + 1, or 0.
static size_t parse_resource_line(const std::string& line) {
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

static size_t read_bar_size(const std::string& bdf, uint32_t bar_index) {
    const std::string path = "/sys/bus/pci/devices/" + bdf + "/resource";
    std::ifstream f(path);
    if (!f) return 0;
    std::string line;
    for (uint32_t i = 0; std::getline(f, line); ++i) {
        if (i == bar_index) return parse_resource_line(line);
    }
    return 0;
}

bool CxlIo::open(const std::string& bdf, uint32_t bar_index) {
    const std::string path = "/sys/bus/pci/devices/" + bdf +
                             "/resource" + std::to_string(bar_index);

    // Determine BAR size from the text resource file — sysfs stat(st_size)
    // is unreliable for PCI resource files on some kernel versions.
    const size_t sz = read_bar_size(bdf, bar_index);
    if (sz == 0) {
        last_error_ = "BAR" + std::to_string(bar_index) +
                      " is absent or zero-sized for " + bdf +
                      " (checked /sys/bus/pci/devices/" + bdf + "/resource)";
        return false;
    }

    fd_ = ::open(path.c_str(), O_RDWR | O_SYNC);
    if (fd_ < 0) {
        last_error_ = "open " + path + ": " + std::strerror(errno);
        return false;
    }

    bar_size_ = sz;
    base_ = mmap(nullptr, bar_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (base_ == MAP_FAILED) {
        last_error_ = "mmap BAR: " + std::string(std::strerror(errno));
        ::close(fd_);
        fd_       = -1;
        base_     = nullptr;
        bar_size_ = 0;
        return false;
    }
    return true;
}

void CxlIo::close() {
    if (base_) {
        munmap(base_, bar_size_);
        base_     = nullptr;
        bar_size_ = 0;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

uint32_t CxlIo::read32(uint64_t offset) const {
    return *reinterpret_cast<volatile uint32_t*>(static_cast<char*>(base_) + offset);
}

uint64_t CxlIo::read64(uint64_t offset) const {
    return *reinterpret_cast<volatile uint64_t*>(static_cast<char*>(base_) + offset);
}

void CxlIo::write32(uint64_t offset, uint32_t val) {
    *reinterpret_cast<volatile uint32_t*>(static_cast<char*>(base_) + offset) = val;
    _mm_mfence();
}

void CxlIo::write64(uint64_t offset, uint64_t val) {
    *reinterpret_cast<volatile uint64_t*>(static_cast<char*>(base_) + offset) = val;
    _mm_mfence();
}
