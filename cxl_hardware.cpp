#include "include/cxl_hardware.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>

#include "include/cxl_driver.h"

#ifdef __linux__
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>
#endif
#endif

class CxlHardware::Impl {
public:
    explicit Impl(CxlHardwareConfig config_in)
        : config(std::move(config_in)) {}

    ~Impl() {
#ifdef __linux__
        unmap_dax_window();
        driver.reset();
        register_io.reset();
        if (bar_base != MAP_FAILED) {
            munmap(bar_base, static_cast<size_t>(bar_bytes));
        }
        if (bar_fd >= 0) {
            close(bar_fd);
        }
        if (dax_fd >= 0) {
            close(dax_fd);
        }
#endif
    }

    bool fail(const std::string& message) {
        error = message;
        ready = false;
        return false;
    }

#ifdef __linux__
    void unmap_dax_window() {
        if (dax_base != MAP_FAILED) {
            munmap(dax_base, static_cast<size_t>(dax_map_bytes));
            dax_base = MAP_FAILED;
            dax_map_start = 0;
            dax_map_bytes = 0;
        }
    }

    bool map_dax_offset(uint64_t offset) {
        if (offset > config.dax_size_bytes - sizeof(uint64_t)) {
            return fail("CXL.mem offset is outside the configured DAX size.");
        }

        if (dax_base != MAP_FAILED &&
            offset >= dax_map_start &&
            offset + sizeof(uint64_t) <= dax_map_start + dax_map_bytes) {
            return true;
        }

        unmap_dax_window();

        const uint64_t align = config.dax_map_alignment;
        const uint64_t map_start = offset & ~(align - 1ULL);
        uint64_t map_bytes = align;
        if (map_start + map_bytes > config.dax_size_bytes) {
            map_bytes = config.dax_size_bytes - map_start;
        }

        dax_base = mmap(
            nullptr,
            static_cast<size_t>(map_bytes),
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            dax_fd,
            static_cast<off_t>(map_start)
        );
        if (dax_base == MAP_FAILED) {
            return fail("mmap(" + config.dax_path + ") failed: " + std::string(strerror(errno)));
        }

        dax_map_start = map_start;
        dax_map_bytes = map_bytes;
        return true;
    }

    volatile uint64_t* mapped_word(uint64_t offset) {
        return reinterpret_cast<volatile uint64_t*>(
            reinterpret_cast<volatile uint8_t*>(dax_base) + (offset - dax_map_start)
        );
    }

    static void flush_cacheline(volatile void* p) {
#if defined(__x86_64__) || defined(__i386__)
        _mm_clflush(const_cast<const void*>(p));
        _mm_mfence();
#else
        (void)p;
        std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
    }
#endif

    CxlHardwareConfig config;
    std::string error;
    bool ready = false;

#ifdef __linux__
    int bar_fd = -1;
    void* bar_base = MAP_FAILED;
    uint64_t bar_bytes = 0;

    int dax_fd = -1;
    void* dax_base = MAP_FAILED;
    uint64_t dax_map_start = 0;
    uint64_t dax_map_bytes = 0;

    std::unique_ptr<MappedMmioRegisterIO> register_io;
    std::unique_ptr<CxlCuDDriver> driver;
#endif
};

CxlHardware::CxlHardware(CxlHardwareConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

CxlHardware::~CxlHardware() = default;

bool CxlHardware::init() {
#ifndef __linux__
    return impl_->fail("CxlHardware is supported only on Linux.");
#else
    if (impl_->ready) {
        return true;
    }
    if (impl_->config.dax_map_alignment == 0 ||
        (impl_->config.dax_map_alignment & (impl_->config.dax_map_alignment - 1ULL)) != 0) {
        return impl_->fail("DAX mmap alignment must be a power of two.");
    }

    const std::string resource_path =
        "/sys/bus/pci/devices/" + impl_->config.bdf +
        "/resource" + std::to_string(impl_->config.bar_index);

    impl_->bar_fd = open(resource_path.c_str(), O_RDWR | O_SYNC);
    if (impl_->bar_fd < 0) {
        return impl_->fail("open(" + resource_path + ") failed: " + std::string(strerror(errno)));
    }

    struct stat bar_stat {};
    if (fstat(impl_->bar_fd, &bar_stat) != 0 || bar_stat.st_size <= 0) {
        return impl_->fail("Unable to determine BAR mapping size.");
    }
    impl_->bar_bytes = static_cast<uint64_t>(bar_stat.st_size);
    impl_->bar_base = mmap(
        nullptr,
        static_cast<size_t>(impl_->bar_bytes),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        impl_->bar_fd,
        0
    );
    if (impl_->bar_base == MAP_FAILED) {
        return impl_->fail("mmap(" + resource_path + ") failed: " + std::string(strerror(errno)));
    }

    impl_->dax_fd = open(impl_->config.dax_path.c_str(), O_RDWR);
    if (impl_->dax_fd < 0) {
        return impl_->fail(
            "open(" + impl_->config.dax_path + ") failed: " + std::string(strerror(errno))
        );
    }

    impl_->register_io = std::make_unique<MappedMmioRegisterIO>(
        reinterpret_cast<volatile uint8_t*>(impl_->bar_base),
        impl_->bar_bytes
    );

    CxlControlMap control = impl_->config.use_upper_window
        ? CxlControlMap::make_vcu_test_profile_upper4kb()
        : CxlControlMap::make_vcu_test_profile_lower4kb();
    control.command_address_dont_care = impl_->config.command_address_dont_care;
    control.done_assert_latency_us = impl_->config.done_assert_latency_us;

    impl_->driver = std::make_unique<CxlCuDDriver>(*impl_->register_io, control);
    impl_->error.clear();
    impl_->ready = true;
    return true;
#endif
}

bool CxlHardware::is_ready() const {
    return impl_->ready;
}

const std::string& CxlHardware::last_error() const {
    return impl_->error;
}

uint64_t CxlHardware::row_col_offset(
    uint32_t bank,
    uint32_t row,
    uint32_t col64
) const {
    return
        (static_cast<uint64_t>(row & 0x1FFFFU) << 17) |
        (static_cast<uint64_t>((col64 >> 3) & 0x7FU) << 10) |
        (static_cast<uint64_t>(bank & 0xFU) << 6) |
        (static_cast<uint64_t>(col64 & 0x7U) << 3);
}

bool CxlHardware::write_row_col(
    uint32_t bank,
    uint32_t row,
    uint32_t col64,
    uint64_t value
) {
#ifndef __linux__
    (void)bank;
    (void)row;
    (void)col64;
    (void)value;
    return impl_->fail("CxlHardware is supported only on Linux.");
#else
    if (!impl_->ready) {
        return impl_->fail("CxlHardware is not initialized.");
    }
    if (bank >= 16 || row >= (1U << 17) || col64 >= 1024) {
        return impl_->fail("Invalid bank, row, or 64-bit column.");
    }

    const uint64_t offset = row_col_offset(bank, row, col64);
    if (!impl_->map_dax_offset(offset)) {
        return false;
    }

    volatile uint64_t* word = impl_->mapped_word(offset);
    *word = value;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    Impl::flush_cacheline(word);
    impl_->error.clear();
    return true;
#endif
}

bool CxlHardware::read_row_col(
    uint32_t bank,
    uint32_t row,
    uint32_t col64,
    uint64_t& value_out
) {
#ifndef __linux__
    (void)bank;
    (void)row;
    (void)col64;
    (void)value_out;
    return impl_->fail("CxlHardware is supported only on Linux.");
#else
    if (!impl_->ready) {
        return impl_->fail("CxlHardware is not initialized.");
    }
    if (bank >= 16 || row >= (1U << 17) || col64 >= 1024) {
        return impl_->fail("Invalid bank, row, or 64-bit column.");
    }

    const uint64_t offset = row_col_offset(bank, row, col64);
    if (!impl_->map_dax_offset(offset)) {
        return false;
    }

    volatile uint64_t* word = impl_->mapped_word(offset);
    Impl::flush_cacheline(word);
    value_out = *word;
    impl_->error.clear();
    return true;
#endif
}

bool CxlHardware::fill_row(uint32_t bank, uint32_t row, uint64_t value) {
    for (uint32_t col64 = 0; col64 < 1024; ++col64) {
        if (!write_row_col(bank, row, col64, value)) {
            return false;
        }
    }
    return true;
}

bool CxlHardware::execute(const std::vector<uint32_t>& uops, bool append_end) {
#ifndef __linux__
    (void)uops;
    (void)append_end;
    return impl_->fail("CxlHardware is supported only on Linux.");
#else
    if (!impl_->ready || impl_->driver == nullptr) {
        return impl_->fail("CxlHardware is not initialized.");
    }

    std::string driver_error;
    if (!impl_->driver->submit_and_wait(
            uops,
            impl_->config.timeout_us,
            append_end,
            impl_->config.poll_sleep_us,
            &driver_error)) {
        return impl_->fail(driver_error);
    }

    impl_->error.clear();
    return true;
#endif
}