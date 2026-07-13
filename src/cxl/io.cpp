#include "../../include/cxl/io.h"

#include <cerrno>
#include <cstring>
#include <emmintrin.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

bool CxlIo::open(const std::string& bdf, uint32_t bar_index) {
    const std::string path = "/sys/bus/pci/devices/" + bdf +
                             "/resource" + std::to_string(bar_index);

    fd_ = ::open(path.c_str(), O_RDWR | O_SYNC);
    if (fd_ < 0) {
        last_error_ = "open " + path + ": " + std::strerror(errno);
        return false;
    }

    struct stat st{};
    if (fstat(fd_, &st) != 0 || st.st_size <= 0) {
        last_error_ = "fstat " + path + ": cannot determine BAR size";
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    bar_size_ = static_cast<size_t>(st.st_size);

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
