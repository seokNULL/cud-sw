#include "include/cxl_driver.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace {
constexpr uint32_t kEndUop = 0x00000000U;
}

MappedMmioRegisterIO::MappedMmioRegisterIO(volatile uint8_t* base, uint64_t span_bytes)
    : base_(base), span_bytes_(span_bytes) {}

bool MappedMmioRegisterIO::can_access32(uint64_t offset) const {
    if (base_ == nullptr || span_bytes_ < sizeof(uint32_t)) {
        return false;
    }
    if ((offset & 0x3ULL) != 0) {
        return false;
    }
    return offset <= (span_bytes_ - sizeof(uint32_t));
}

bool MappedMmioRegisterIO::write32(uint64_t offset, uint32_t value) {
    if (!can_access32(offset)) {
        return false;
    }
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(base_ + offset);
    *reg = value;
    return true;
}

bool MappedMmioRegisterIO::read32(uint64_t offset, uint32_t& value_out) {
    if (!can_access32(offset)) {
        return false;
    }
    volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(base_ + offset);
    value_out = *reg;
    return true;
}

MappedCxlMemIO::MappedCxlMemIO(volatile uint8_t* base, uint64_t span_bytes)
    : base_(base), span_bytes_(span_bytes) {}

bool MappedCxlMemIO::can_access64(uint64_t byte_offset) const {
    if (base_ == nullptr || span_bytes_ < sizeof(uint64_t)) {
        return false;
    }
    if ((byte_offset & 0x7ULL) != 0) {
        return false;
    }
    return byte_offset <= (span_bytes_ - sizeof(uint64_t));
}

bool MappedCxlMemIO::write64(uint64_t byte_offset, uint64_t value) {
    if (!can_access64(byte_offset)) {
        return false;
    }
    volatile uint64_t* p = reinterpret_cast<volatile uint64_t*>(base_ + byte_offset);
    *p = value;
    return true;
}

bool MappedCxlMemIO::read64(uint64_t byte_offset, uint64_t& value_out) {
    if (!can_access64(byte_offset)) {
        return false;
    }
    volatile uint64_t* p = reinterpret_cast<volatile uint64_t*>(base_ + byte_offset);
    value_out = *p;
    return true;
}

CxlMemAccessor::CxlMemAccessor(ICxlMemIO& mem, CxlMemLayout layout)
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
    const uint64_t physical_base_low =
        layout_.system_physical_base_bytes & address_mask;
    const uint64_t relative_offset =
        (dram_address - physical_base_low) & address_mask;

    return layout_.base_offset_bytes + relative_offset;
}

bool CxlMemAccessor::write_row_col(
    uint32_t bank,
    uint32_t global_row,
    uint32_t col64,
    uint64_t value
) {
    return mem_.write64(compute_row_col_offset_bytes(bank, global_row, col64), value);
}

bool CxlMemAccessor::read_row_col(
    uint32_t bank,
    uint32_t global_row,
    uint32_t col64,
    uint64_t& value_out
) {
    return mem_.read64(compute_row_col_offset_bytes(bank, global_row, col64), value_out);
}

bool CxlMemAccessor::fill_row(uint32_t bank, uint32_t global_row, uint64_t value) {
    if (layout_.columns_per_row == 0) {
        return false;
    }
    for (uint32_t col = 0; col < layout_.columns_per_row; ++col) {
        if (!write_row_col(bank, global_row, static_cast<uint32_t>(col), value)) {
            return false;
        }
    }
    return true;
}

bool CxlControlMap::has_minimum_required_fields() const {
    return command_window_offset != INVALID_OFFSET &&
           poll_reg_offset != INVALID_OFFSET;
}

CxlControlMap CxlControlMap::make_fifo_autostart_profile() {
    CxlControlMap map{};
    map.command_window_offset = 0x0;
    map.poll_reg_offset = 0x0;
    map.kick_reg_offset = INVALID_OFFSET;
    map.tail_reg_offset = INVALID_OFFSET;
    map.done_mask = 0x00000001U;
    map.kick_value = 0x00000001U;
    map.uop_fifo_depth_words = 1024;
    map.command_address_dont_care = true;
    map.done_assert_latency_us = 0;
    return map;
}

CxlControlMap CxlControlMap::make_vcu_test_profile_lower4kb() {
    CxlControlMap map{};
    map.command_window_offset = 0x0000;
    map.poll_reg_offset = 0x0000;
    map.kick_reg_offset = INVALID_OFFSET;
    map.tail_reg_offset = INVALID_OFFSET;
    map.done_mask = 0x00000001U;
    map.kick_value = 0x00000001U;
    map.uop_fifo_depth_words = 1024;
    map.command_address_dont_care = false;
    map.done_assert_latency_us = 5000;
    return map;
}

CxlControlMap CxlControlMap::make_vcu_test_profile_upper4kb() {
    CxlControlMap map{};
    map.command_window_offset = 0x1000;
    map.poll_reg_offset = 0x0000;
    map.kick_reg_offset = INVALID_OFFSET;
    map.tail_reg_offset = INVALID_OFFSET;
    map.done_mask = 0x00000001U;
    map.kick_value = 0x00000001U;
    map.uop_fifo_depth_words = 1024;
    map.command_address_dont_care = false;
    map.done_assert_latency_us = 5000;
    return map;
}

// Protocol:
// 1. Software writes u-ops sequentially to command_window_offset (CXL.io write channel)
// 2. Hardware begins execution immediately
// 3. Software appends END u-op to terminate program
// 4. Software polls poll_reg_offset (CXL.io read channel) until done_mask is set
// 5. After done, dependent CXL.mem accesses are allowed

CxlCuDDriver::CxlCuDDriver(ICxlRegisterIO& io, CxlControlMap map)
    : io_(io), map_(map) {}

void CxlCuDDriver::set_control_map(const CxlControlMap& map) {
    map_ = map;
}

const CxlControlMap& CxlCuDDriver::control_map() const {
    return map_;
}

bool CxlCuDDriver::validate_config(std::string* err) const {
    if (!map_.has_minimum_required_fields()) {
        if (err != nullptr) {
            *err = "CXL control map is incomplete (need command_window_offset and poll_reg_offset).";
        }
        return false;
    }
    if (map_.done_mask == 0) {
        if (err != nullptr) {
            *err = "done_mask must be non-zero.";
        }
        return false;
    }
    return true;
}

bool CxlCuDDriver::submit_uops(
    const std::vector<uint32_t>& uops,
    bool append_end,
    std::string* err
) {
    if (!validate_config(err)) {
        return false;
    }
    if (uops.empty()) {
        if (err != nullptr) {
            *err = "submit_uops received empty u-op list.";
        }
        return false;
    }

    std::vector<uint32_t> program = uops;
    if (append_end && program.back() != kEndUop) {
        program.push_back(kEndUop);
    }
    if (map_.uop_fifo_depth_words != 0U && program.size() > map_.uop_fifo_depth_words) {
        if (err != nullptr) {
            *err = "U-op program exceeds configured FIFO depth.";
        }
        return false;
    }

    // Write program words into command window as contiguous 32-bit registers.
    for (size_t i = 0; i < program.size(); ++i) {
        const uint64_t offset = map_.command_address_dont_care
            ? map_.command_window_offset
            : (map_.command_window_offset + static_cast<uint64_t>(i * sizeof(uint32_t)));
        if (!io_.write32(offset, program[i])) {
            if (err != nullptr) {
                *err = "Failed while writing u-op into command window.";
            }
            return false;
        }
    }

    // Ensure command words are visible before publish/kick.
    std::atomic_thread_fence(std::memory_order_release);

    if (map_.tail_reg_offset != CxlControlMap::INVALID_OFFSET) {
        if (!io_.write32(map_.tail_reg_offset, static_cast<uint32_t>(program.size()))) {
            if (err != nullptr) {
                *err = "Failed to write tail register.";
            }
            return false;
        }
    }

    if (map_.kick_reg_offset != CxlControlMap::INVALID_OFFSET) {
        if (!io_.write32(map_.kick_reg_offset, map_.kick_value)) {
            if (err != nullptr) {
                *err = "Failed to write kick register.";
            }
            return false;
        }
    }

    return true;
}

bool CxlCuDDriver::wait_done(
    uint32_t timeout_us,
    uint32_t poll_sleep_us,
    std::string* err
) {
    if (!validate_config(err)) {
        return false;
    }

    const auto start = std::chrono::steady_clock::now();

    // Some designs assert DONE only after a fixed END->DONE latency.
    // Account for that here to avoid hammer-polling the BAR too early.
    if (map_.done_assert_latency_us != 0U) {
        const auto sleep_us = std::min<uint64_t>(map_.done_assert_latency_us, timeout_us);
        if (sleep_us != 0U) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
        }
    }

    while (true) {
        uint32_t poll_value = 0;
        if (!io_.read32(map_.poll_reg_offset, poll_value)) {
            if (err != nullptr) {
                *err = "Failed to read polling register.";
            }
            return false;
        }

        if ((poll_value & map_.done_mask) != 0U) {
            // Ensure later reads are not reordered before done observation.
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_us =
            std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
        if (elapsed_us >= timeout_us) {
            if (err != nullptr) {
                *err = "Timed out waiting for done flag. Hardware may be stalled or END u-op may not have been consumed.";
            }
            return false;
        }

        if (poll_sleep_us != 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(poll_sleep_us));
        }
    }
}

bool CxlCuDDriver::submit_and_wait(
    const std::vector<uint32_t>& uops,
    uint32_t timeout_us,
    bool append_end,
    uint32_t poll_sleep_us,
    std::string* err
) {
    if (!submit_uops(uops, append_end, err)) {
        return false;
    }
    return wait_done(timeout_us, poll_sleep_us, err);
}