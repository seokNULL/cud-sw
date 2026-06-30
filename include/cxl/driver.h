#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CxlControlConfig {
    static constexpr uint64_t INVALID_OFFSET = ~0ULL;

    uint64_t command_window_offset = INVALID_OFFSET;
    uint64_t poll_reg_offset = INVALID_OFFSET;
    uint64_t kick_reg_offset = INVALID_OFFSET;
    uint64_t tail_reg_offset = INVALID_OFFSET;

    uint32_t done_mask = 0x00000001U;
    uint32_t kick_value = 0x00000001U;
    uint32_t uop_fifo_depth_words = 1024;
    bool command_address_dont_care = false;
    uint32_t done_assert_latency_us = 0;

    bool has_minimum_required_fields() const;

    static CxlControlConfig make_fifo_autostart_profile();
    static CxlControlConfig make_vcu_test_profile_lower4kb();
    static CxlControlConfig make_vcu_test_profile_upper4kb();
};

class ICxlRegisterIo {
public:
    virtual ~ICxlRegisterIo() = default;
    virtual bool write32(uint64_t offset, uint32_t value) = 0;
    virtual bool read32(uint64_t offset, uint32_t& value_out) = 0;
};

class MmioRegisterIo final : public ICxlRegisterIo {
public:
    MmioRegisterIo(volatile uint8_t* base, uint64_t span_bytes);

    bool write32(uint64_t offset, uint32_t value) override;
    bool read32(uint64_t offset, uint32_t& value_out) override;

private:
    bool can_access32(uint64_t offset) const;

    volatile uint8_t* base_ = nullptr;
    uint64_t span_bytes_ = 0;
};

class ICxlMemIo {
public:
    virtual ~ICxlMemIo() = default;
    virtual bool write64(uint64_t byte_offset, uint64_t value) = 0;
    virtual bool read64(uint64_t byte_offset, uint64_t& value_out) = 0;
};

class CxlMemIo final : public ICxlMemIo {
public:
    CxlMemIo(volatile uint8_t* base, uint64_t span_bytes);

    bool write64(uint64_t byte_offset, uint64_t value) override;
    bool read64(uint64_t byte_offset, uint64_t& value_out) override;

private:
    bool can_access64(uint64_t byte_offset) const;

    volatile uint8_t* base_ = nullptr;
    uint64_t span_bytes_ = 0;
};

struct CxlMemLayout {
    uint64_t base_offset_bytes = 0;
    uint64_t system_physical_base_bytes = 0;
    uint32_t system_address_bits = 34;
    uint32_t columns_per_row = 1024;
};

class CxlMemAccessor {
public:
    CxlMemAccessor(ICxlMemIo& mem, CxlMemLayout layout);

    bool write_row(uint32_t bank, uint32_t global_row, uint64_t value);
    bool read_row(uint32_t bank, uint32_t global_row, uint64_t& value_out);
    uint64_t compute_row_offset_bytes(uint32_t bank, uint32_t global_row) const;

    bool write_row_col(uint32_t bank, uint32_t global_row, uint32_t col64, uint64_t value);
    bool read_row_col(uint32_t bank, uint32_t global_row, uint32_t col64, uint64_t& value_out);
    uint64_t compute_row_col_offset_bytes(uint32_t bank, uint32_t global_row, uint32_t col64) const;

    bool fill_row(uint32_t bank, uint32_t global_row, uint64_t value);

private:
    ICxlMemIo& mem_;
    CxlMemLayout layout_;
};

class CxlCudDriver {
public:
    CxlCudDriver(ICxlRegisterIo& io, CxlControlConfig config = {});

    void set_control_config(const CxlControlConfig& config);
    const CxlControlConfig& control_config() const;

    bool validate_config(std::string* err = nullptr) const;

    bool submit_uops(const std::vector<uint32_t>& uops,
                     bool append_end = true,
                     std::string* err = nullptr);

    bool wait_done(uint32_t timeout_us,
                   uint32_t poll_sleep_us = 10,
                   std::string* err = nullptr);

    bool submit_and_wait(const std::vector<uint32_t>& uops,
                         uint32_t timeout_us,
                         bool append_end = true,
                         uint32_t poll_sleep_us = 10,
                         std::string* err = nullptr);

private:
    ICxlRegisterIo& io_;
    CxlControlConfig config_;
};
