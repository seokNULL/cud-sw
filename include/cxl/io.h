#pragma once
#include <cstdint>
#include <string>

// CXL.io path — MMIO register access via PCI BAR.
// Opens /sys/bus/pci/devices/<bdf>/resource<bar_index> and mmaps it.
class CxlIo {
public:
    CxlIo() = default;
    ~CxlIo() { close(); }
    CxlIo(const CxlIo&)            = delete;
    CxlIo& operator=(const CxlIo&) = delete;

    bool open(const std::string& bdf, uint32_t bar_index);
    void close();

    uint32_t read32(uint64_t offset) const;
    uint64_t read64(uint64_t offset) const;
    void     write32(uint64_t offset, uint32_t val);
    void     write64(uint64_t offset, uint64_t val);

    bool               is_open()    const { return base_ != nullptr; }
    size_t             bar_size()   const { return bar_size_; }
    const std::string& last_error() const { return last_error_; }

private:
    void*       base_       = nullptr;
    size_t      bar_size_   = 0;
    int         fd_         = -1;
    std::string last_error_;
};
