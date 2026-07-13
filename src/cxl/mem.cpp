#include "../../include/cxl/mem.h"

#include <cerrno>
#include <cstring>
#include <emmintrin.h>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

static uint64_t read_dax_size_from_sysfs(const std::string& dax_path) {
    const std::string name = dax_path.substr(dax_path.rfind('/') + 1);
    const std::string path = "/sys/bus/dax/devices/" + name + "/size";
    std::ifstream f(path);
    if (!f) return 0;
    std::string val;
    std::getline(f, val);
    try { return std::stoull(val, nullptr, 0); } catch (...) { return 0; }
}

bool CxlMem::open(const std::string& dax_path, uint64_t size_bytes) {
    fd_ = ::open(dax_path.c_str(), O_RDWR);
    if (fd_ < 0) {
        last_error_ = "open " + dax_path + ": " + std::strerror(errno);
        return false;
    }

    if (size_bytes == 0)
        size_bytes = read_dax_size_from_sysfs(dax_path);

    if (size_bytes == 0) {
        last_error_ = "cannot determine size for " + dax_path;
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    size_ = size_bytes;

    base_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (base_ == MAP_FAILED) {
        last_error_ = "mmap " + dax_path + ": " + std::strerror(errno);
        ::close(fd_);
        fd_   = -1;
        base_ = nullptr;
        return false;
    }
    return true;
}

void CxlMem::close() {
    if (base_) {
        munmap(base_, size_);
        base_ = nullptr;
        size_ = 0;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

uint64_t CxlMem::read64(uint64_t byte_offset) const {
    return *reinterpret_cast<volatile uint64_t*>(static_cast<char*>(base_) + byte_offset);
}

void CxlMem::write64(uint64_t byte_offset, uint64_t val) {
    *reinterpret_cast<volatile uint64_t*>(static_cast<char*>(base_) + byte_offset) = val;
}

void CxlMem::flush(uint64_t byte_offset, uint64_t size) {
    static constexpr uint64_t kLineSize = 64;
    const uint64_t start = byte_offset & ~(kLineSize - 1);
    const uint64_t end   = byte_offset + size;
    for (uint64_t off = start; off < end; off += kLineSize)
        _mm_clflush(static_cast<char*>(base_) + off);
    _mm_mfence();
}

void CxlMem::fence() {
    _mm_mfence();
}
