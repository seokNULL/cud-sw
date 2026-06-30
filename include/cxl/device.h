#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct CxlDeviceConfig {
    std::string bdf = "0000:b8:00.0";
    uint32_t bar_index = 2;
    std::string dax_path = "/dev/dax1.0";

    uint64_t dax_size_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
    uint64_t dax_map_alignment = 2ULL * 1024ULL * 1024ULL;

    bool use_upper_window = false;
    bool command_address_dont_care = true;
    uint32_t timeout_us = 200000;
    uint32_t poll_sleep_us = 50;
    uint32_t done_assert_latency_us = 5000;
};

class CxlDevice {
public:
    explicit CxlDevice(CxlDeviceConfig config = {});
    ~CxlDevice();

    CxlDevice(const CxlDevice&) = delete;
    CxlDevice& operator=(const CxlDevice&) = delete;

    bool init();
    bool is_ready() const;
    const std::string& last_error() const;

    bool write_row_col(uint32_t bank, uint32_t row, uint32_t col64, uint64_t value);
    bool read_row_col(uint32_t bank, uint32_t row, uint32_t col64, uint64_t& value_out);
    bool fill_row(uint32_t bank, uint32_t row, uint64_t value);

    bool execute(const std::vector<uint32_t>& uops, bool append_end = true);
    uint64_t row_col_offset(uint32_t bank, uint32_t row, uint32_t col64) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
