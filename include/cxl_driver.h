#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct CxlControlMap {
    // Fill these when hardware team provides BAR offsets.
    // Keep INVALID_OFFSET while unknown.
    static constexpr uint64_t INVALID_OFFSET = ~0ULL;

    uint64_t command_window_offset = INVALID_OFFSET; // MMIO window/FIFO port for u-ops (32-bit words)
    uint64_t poll_reg_offset = INVALID_OFFSET;       // done/status register

    // Optional control registers.
    uint64_t kick_reg_offset = INVALID_OFFSET;       // write kick_value to trigger execution
    uint64_t tail_reg_offset = INVALID_OFFSET;       // write u-op count if hardware uses tail model

    uint32_t done_mask = 0x00000001U;
    uint32_t kick_value = 0x00000001U;
    uint32_t uop_fifo_depth_words = 1024;
    bool command_address_dont_care = false; // true for FIFO write-port designs where MMIO write address is ignored
    uint32_t done_assert_latency_us = 0;     // known END->DONE delay for a given design (optional)

    bool has_minimum_required_fields() const;

    // Profile from current prototype:
    // - write u-ops to BAR offset 0x0
    // - poll done on BAR read offset 0x0 (bit0)
    // - no explicit clear/kick/tail
    // - FIFO depth 1024 words
    static CxlControlMap make_fifo_autostart_profile();

    // VCU FPGA test image profiles:
    // - lower 4KB queue window: 0x0000..0x0FFC
    // - upper 4KB queue window: 0x1000..0x1FFC (test logic only)
    // - poll done on BAR read offset 0x0 (bit0)
    // - done is asserted ~5ms after END is input
    static CxlControlMap make_vcu_test_profile_lower4kb();
    static CxlControlMap make_vcu_test_profile_upper4kb();
};

class ICxlRegisterIO {
public:
    virtual ~ICxlRegisterIO() = default;
    virtual bool write32(uint64_t offset, uint32_t value) = 0;
    virtual bool read32(uint64_t offset, uint32_t& value_out) = 0;
};

// Backed by a memory-mapped BAR base pointer.
class MappedMmioRegisterIO final : public ICxlRegisterIO {
public:
    MappedMmioRegisterIO(volatile uint8_t* base, uint64_t span_bytes);

    bool write32(uint64_t offset, uint32_t value) override;
    bool read32(uint64_t offset, uint32_t& value_out) override;

private:
    bool can_access32(uint64_t offset) const;

private:
    volatile uint8_t* base_ = nullptr;
    uint64_t span_bytes_ = 0;
};

class ICxlMemIO {
public:
    virtual ~ICxlMemIO() = default;
    virtual bool write64(uint64_t byte_offset, uint64_t value) = 0;
    virtual bool read64(uint64_t byte_offset, uint64_t& value_out) = 0;
};

class MappedCxlMemIO final : public ICxlMemIO {
public:
    MappedCxlMemIO(volatile uint8_t* base, uint64_t span_bytes);

    bool write64(uint64_t byte_offset, uint64_t value) override;
    bool read64(uint64_t byte_offset, uint64_t& value_out) override;

private:
    bool can_access64(uint64_t byte_offset) const;

private:
    volatile uint8_t* base_ = nullptr;
    uint64_t span_bytes_ = 0;
};

struct CxlMemLayout {
    // Hardware mapping:
    // system[33:17]=row, [16:10]=col[9:3], [9:8]=BA,
    // [7:6]=BG, [5:3]=col[2:0], [2:0]=byte.
    // bank is encoded as {BA[1:0], BG[1:0]}.
    uint64_t base_offset_bytes = 0;
    uint64_t system_physical_base_bytes = 0;
    uint32_t system_address_bits = 34;
    uint32_t columns_per_row = 1024; // 1024 x 64-bit columns = 8KB per bank row
};

class CxlMemAccessor {
public:
    CxlMemAccessor(ICxlMemIO& mem, CxlMemLayout layout);

    bool write_row(uint32_t bank, uint32_t global_row, uint64_t value);
    bool read_row(uint32_t bank, uint32_t global_row, uint64_t& value_out);
    uint64_t compute_row_offset_bytes(uint32_t bank, uint32_t global_row) const;

    // Column index is a 64-bit word index inside the row (col 0 => first 8 bytes).
    bool write_row_col(uint32_t bank, uint32_t global_row, uint32_t col64, uint64_t value);
    bool read_row_col(uint32_t bank, uint32_t global_row, uint32_t col64, uint64_t& value_out);
    uint64_t compute_row_col_offset_bytes(uint32_t bank, uint32_t global_row, uint32_t col64) const;

    // Fills all 1024 64-bit columns in one bank row by default.
    bool fill_row(uint32_t bank, uint32_t global_row, uint64_t value);

private:
    ICxlMemIO& mem_;
    CxlMemLayout layout_;
};

class CxlCuDDriver {
public:
    CxlCuDDriver(ICxlRegisterIO& io, CxlControlMap map = {});

    void set_control_map(const CxlControlMap& map);
    const CxlControlMap& control_map() const;

    bool validate_config(std::string* err = nullptr) const;

    // Pushes u-ops to command window. Appends END(0x00000000) by default.
    bool submit_uops(
        const std::vector<uint32_t>& uops,
        bool append_end = true,
        std::string* err = nullptr
    );

    // Polls done bit until timeout.
    bool wait_done(
        uint32_t timeout_us,
        uint32_t poll_sleep_us = 10,
        std::string* err = nullptr
    );

    bool submit_and_wait(
        const std::vector<uint32_t>& uops,
        uint32_t timeout_us,
        bool append_end = true,
        uint32_t poll_sleep_us = 10,
        std::string* err = nullptr
    );

private:
    ICxlRegisterIO& io_;
    CxlControlMap map_;
};