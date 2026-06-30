#include "../../include/cxl/driver.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace {
constexpr uint32_t kEndUop = 0x00000000U;
}

MmioRegisterIo::MmioRegisterIo(volatile uint8_t* base, uint64_t span_bytes)
    : base_(base), span_bytes_(span_bytes) {}

bool MmioRegisterIo::can_access32(uint64_t offset) const {
    if (base_ == nullptr || span_bytes_ < sizeof(uint32_t)) return false;
    if ((offset & 0x3ULL) != 0) return false;
    return offset <= (span_bytes_ - sizeof(uint32_t));
}

bool MmioRegisterIo::write32(uint64_t offset, uint32_t value) {
    if (!can_access32(offset)) return false;
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(base_ + offset);
    *reg = value;
    return true;
}

bool MmioRegisterIo::read32(uint64_t offset, uint32_t& value_out) {
    if (!can_access32(offset)) return false;
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(base_ + offset);
    value_out = *reg;
    return true;
}

CxlMemIo::CxlMemIo(volatile uint8_t* base, uint64_t span_bytes)
    : base_(base), span_bytes_(span_bytes) {}

bool CxlMemIo::can_access64(uint64_t byte_offset) const {
    if (base_ == nullptr || span_bytes_ < sizeof(uint64_t)) return false;
    if ((byte_offset & 0x7ULL) != 0) return false;
    return byte_offset <= (span_bytes_ - sizeof(uint64_t));
}

bool CxlMemIo::write64(uint64_t byte_offset, uint64_t value) {
    if (!can_access64(byte_offset)) return false;
    volatile uint64_t* p = reinterpret_cast<volatile uint64_t*>(base_ + byte_offset);
    *p = value;
    return true;
}

bool CxlMemIo::read64(uint64_t byte_offset, uint64_t& value_out) {
    if (!can_access64(byte_offset)) return false;
    volatile uint64_t* p = reinterpret_cast<volatile uint64_t*>(base_ + byte_offset);
    value_out = *p;
    return true;
}

CxlMemAccessor::CxlMemAccessor(ICxlMemIo& mem, CxlMemLayout layout)
    : mem_(mem), layout_(layout) {}

uint64_t CxlMemAccessor::compute_row_offset_bytes(uint32_t bank, uint32_t global_row) const {
    return compute_row_col_offset_bytes(bank, global_row, 0);
}

bool CxlMemAccessor::write_row(uint32_t bank, uint32_t global_row, uint64_t value) {
    return mem_.write64(compute_row_offset_bytes(bank, global_row), value);
}

bool CxlMemAccessor::read_row(uint32_t bank, uint32_t global_row, uint64_t& value_out) {
    return mem_.read64(compute_row_offset_bytes(bank, global_row), value_out);
}

uint64_t CxlMemAccessor::compute_row_col_offset_bytes(
    uint32_t bank,
    uint32_t global_row,
    uint32_t col64
) const {
    const uint64_t dram_address =
        (static_cast<uint64_t>(global_row & 0x1FFFFU) << 17) |
        (static_cast<uint64_t>((col64 >> 3) & 0x7FU) << 10) |
        (static_cast<uint64_t>(bank & 0xFU) << 6) |
        (static_cast<uint64_t>(col64 & 0x7U) << 3);

    const uint32_t address_bits = std::min(layout_.system_address_bits, 63U);
    const uint64_t address_mask = (1ULL << address_bits) - 1ULL;
    const uint64_t physical_base_low = layout_.system_physical_base_bytes & address_mask;
    const uint64_t relative_offset = (dram_address - physical_base_low) & address_mask;
    return layout_.base_offset_bytes + relative_offset;
}

bool CxlMemAccessor::write_row_col(
    uint32_t bank, uint32_t global_row, uint32_t col64, uint64_t value
) {
    return mem_.write64(compute_row_col_offset_bytes(bank, global_row, col64), value);
}

bool CxlMemAccessor::read_row_col(
    uint32_t bank, uint32_t global_row, uint32_t col64, uint64_t& value_out
) {
    return mem_.read64(compute_row_col_offset_bytes(bank, global_row, col64), value_out);
}

bool CxlMemAccessor::fill_row(uint32_t bank, uint32_t global_row, uint64_t value) {
    if (layout_.columns_per_row == 0) return false;
    for (uint32_t col = 0; col < layout_.columns_per_row; ++col) {
        if (!write_row_col(bank, global_row, col, value)) return false;
    }
    return true;
}

bool CxlControlConfig::has_minimum_required_fields() const {
    return command_window_offset != INVALID_OFFSET &&
           poll_reg_offset != INVALID_OFFSET;
}

CxlControlConfig CxlControlConfig::make_fifo_autostart_profile() {
    CxlControlConfig cfg{};
    cfg.command_window_offset = 0x0;
    cfg.poll_reg_offset = 0x0;
    cfg.kick_reg_offset = INVALID_OFFSET;
    cfg.tail_reg_offset = INVALID_OFFSET;
    cfg.done_mask = 0x00000001U;
    cfg.kick_value = 0x00000001U;
    cfg.uop_fifo_depth_words = 1024;
    cfg.command_address_dont_care = true;
    cfg.done_assert_latency_us = 0;
    return cfg;
}

CxlControlConfig CxlControlConfig::make_vcu_test_profile_lower4kb() {
    CxlControlConfig cfg{};
    cfg.command_window_offset = 0x0000;
    cfg.poll_reg_offset = 0x0000;
    cfg.kick_reg_offset = INVALID_OFFSET;
    cfg.tail_reg_offset = INVALID_OFFSET;
    cfg.done_mask = 0x00000001U;
    cfg.kick_value = 0x00000001U;
    cfg.uop_fifo_depth_words = 1024;
    cfg.command_address_dont_care = false;
    cfg.done_assert_latency_us = 5000;
    return cfg;
}

CxlControlConfig CxlControlConfig::make_vcu_test_profile_upper4kb() {
    CxlControlConfig cfg{};
    cfg.command_window_offset = 0x1000;
    cfg.poll_reg_offset = 0x0000;
    cfg.kick_reg_offset = INVALID_OFFSET;
    cfg.tail_reg_offset = INVALID_OFFSET;
    cfg.done_mask = 0x00000001U;
    cfg.kick_value = 0x00000001U;
    cfg.uop_fifo_depth_words = 1024;
    cfg.command_address_dont_care = false;
    cfg.done_assert_latency_us = 5000;
    return cfg;
}

CxlCudDriver::CxlCudDriver(ICxlRegisterIo& io, CxlControlConfig config)
    : io_(io), config_(std::move(config)) {}

void CxlCudDriver::set_control_config(const CxlControlConfig& config) {
    config_ = config;
}

const CxlControlConfig& CxlCudDriver::control_config() const {
    return config_;
}

bool CxlCudDriver::validate_config(std::string* err) const {
    if (!config_.has_minimum_required_fields()) {
        if (err) *err = "CXL control config is incomplete (need command_window_offset and poll_reg_offset).";
        return false;
    }
    if (config_.done_mask == 0) {
        if (err) *err = "done_mask must be non-zero.";
        return false;
    }
    return true;
}

bool CxlCudDriver::submit_uops(
    const std::vector<uint32_t>& uops,
    bool append_end,
    std::string* err
) {
    if (!validate_config(err)) return false;
    if (uops.empty()) {
        if (err) *err = "submit_uops received empty u-op list.";
        return false;
    }

    std::vector<uint32_t> program = uops;
    if (append_end && program.back() != kEndUop) {
        program.push_back(kEndUop);
    }
    if (config_.uop_fifo_depth_words != 0U && program.size() > config_.uop_fifo_depth_words) {
        if (err) *err = "U-op program exceeds configured FIFO depth.";
        return false;
    }

    for (size_t i = 0; i < program.size(); ++i) {
        const uint64_t offset = config_.command_address_dont_care
            ? config_.command_window_offset
            : (config_.command_window_offset + static_cast<uint64_t>(i * sizeof(uint32_t)));
        if (!io_.write32(offset, program[i])) {
            if (err) *err = "Failed while writing u-op into command window.";
            return false;
        }
    }

    std::atomic_thread_fence(std::memory_order_release);

    if (config_.tail_reg_offset != CxlControlConfig::INVALID_OFFSET) {
        if (!io_.write32(config_.tail_reg_offset, static_cast<uint32_t>(program.size()))) {
            if (err) *err = "Failed to write tail register.";
            return false;
        }
    }

    if (config_.kick_reg_offset != CxlControlConfig::INVALID_OFFSET) {
        if (!io_.write32(config_.kick_reg_offset, config_.kick_value)) {
            if (err) *err = "Failed to write kick register.";
            return false;
        }
    }

    return true;
}

bool CxlCudDriver::wait_done(
    uint32_t timeout_us,
    uint32_t poll_sleep_us,
    std::string* err
) {
    if (!validate_config(err)) return false;

    const auto start = std::chrono::steady_clock::now();

    if (config_.done_assert_latency_us != 0U) {
        const auto sleep_us = std::min<uint64_t>(config_.done_assert_latency_us, timeout_us);
        if (sleep_us != 0U) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
        }
    }

    while (true) {
        uint32_t poll_value = 0;
        if (!io_.read32(config_.poll_reg_offset, poll_value)) {
            if (err) *err = "Failed to read polling register.";
            return false;
        }

        if ((poll_value & config_.done_mask) != 0U) {
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_us =
            std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
        if (elapsed_us >= timeout_us) {
            if (err) *err = "Timed out waiting for done flag.";
            return false;
        }

        if (poll_sleep_us != 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(poll_sleep_us));
        }
    }
}

bool CxlCudDriver::submit_and_wait(
    const std::vector<uint32_t>& uops,
    uint32_t timeout_us,
    bool append_end,
    uint32_t poll_sleep_us,
    std::string* err
) {
    if (!submit_uops(uops, append_end, err)) return false;
    return wait_done(timeout_us, poll_sleep_us, err);
}
