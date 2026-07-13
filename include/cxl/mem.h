#pragma once
#include <cstdint>
#include <string>

// CXL.mem path — persistent memory access via DAX character device.
// Opens /dev/daxX.Y and mmaps the full device into process address space.
class CxlMem {
public:
    CxlMem() = default;
    ~CxlMem() { close(); }
    CxlMem(const CxlMem&)            = delete;
    CxlMem& operator=(const CxlMem&) = delete;

    // size_bytes = 0: auto-detect from /sys/bus/dax/devices/<name>/size
    bool open(const std::string& dax_path, uint64_t size_bytes = 0);
    void close();

    uint64_t read64(uint64_t byte_offset) const;
    void     write64(uint64_t byte_offset, uint64_t val);

    // Flush [byte_offset, byte_offset + size) to the DAX medium then fence.
    void flush(uint64_t byte_offset, uint64_t size);

    // Issue an mfence without flushing cache lines.
    void fence();

    bool               is_open()    const { return base_ != nullptr; }
    uint64_t           size()       const { return size_; }
    void*              base()       const { return base_; }
    const std::string& last_error() const { return last_error_; }

private:
    void*       base_       = nullptr;
    uint64_t    size_       = 0;
    int         fd_         = -1;
    std::string last_error_;
};
