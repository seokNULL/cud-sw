#include "../include/cxl/enumerator.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>
#endif

static constexpr uint64_t kTestBytes      = 4ULL * 1024 * 1024;
static constexpr uint64_t kWordBytes      = sizeof(uint64_t);
static constexpr uint64_t kCacheLineWords = 64 / kWordBytes;

static uint64_t test_pattern(uint64_t word_index) {
    return (word_index * 0x9E3779B97F4A7C15ULL) ^ 0xC0FFEE00DEADBEEFULL;
}

static void clflush_line(volatile void* p) {
#if defined(__x86_64__) || defined(__i386__)
    _mm_clflush(const_cast<const void*>(p));
#else
    (void)p;
#endif
}

static void mem_fence() {
#if defined(__x86_64__) || defined(__i386__)
    _mm_mfence();
#else
    std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
}

static bool test_one_device(const CxlDeviceInfo& dev, size_t idx) {
    std::cout << "\n[Device " << idx << "] " << dev.dax_path;
    if (!dev.bdf.empty()) std::cout << "  bdf=" << dev.bdf;
    std::cout << "  size=" << (dev.dax_size_bytes >> 20) << " MiB\n";

    if (dev.dax_size_bytes == 0) {
        std::cout << "  [SKIP] reported size is 0 — cannot test.\n";
        return false;
    }

    const uint64_t map_bytes = std::min<uint64_t>(dev.dax_size_bytes, kTestBytes);
    const uint64_t n_words   = map_bytes / kWordBytes;

    const int fd = open(dev.dax_path.c_str(), O_RDWR);
    if (fd < 0) {
        std::cout << "  [ERROR] open: " << std::strerror(errno)
                  << "  (root required?)\n";
        return false;
    }

    void* const mem = mmap(nullptr,
                           static_cast<size_t>(map_bytes),
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED,
                           fd, 0);
    if (mem == MAP_FAILED) {
        std::cout << "  [ERROR] mmap: " << std::strerror(errno) << "\n";
        close(fd);
        return false;
    }

    std::cout << "  mmap OK: " << (map_bytes >> 10) << " KiB @ " << mem << "\n";

    volatile uint64_t* const words = reinterpret_cast<volatile uint64_t*>(mem);

    std::cout << "  Writing " << n_words << " x 64-bit words ...\n";
    for (uint64_t i = 0; i < n_words; ++i)
        words[i] = test_pattern(i);
    mem_fence();
    for (uint64_t i = 0; i < n_words; i += kCacheLineWords)
        clflush_line(const_cast<uint64_t*>(&words[i]));
    mem_fence();

    std::cout << "  Reading back and verifying ...\n";
    for (uint64_t i = 0; i < n_words; i += kCacheLineWords)
        clflush_line(const_cast<uint64_t*>(&words[i]));
    mem_fence();

    uint64_t errors = 0;
    for (uint64_t i = 0; i < n_words; ++i) {
        const uint64_t got      = words[i];
        const uint64_t expected = test_pattern(i);
        if (got != expected) {
            if (errors < 8) {
                std::cout << "  [FAIL] word[" << i << "]"
                          << std::hex << std::setfill('0')
                          << "  expected=0x" << std::setw(16) << expected
                          << "  got=0x"      << std::setw(16) << got
                          << std::dec << std::setfill(' ') << "\n";
            }
            ++errors;
        }
    }

    munmap(mem, static_cast<size_t>(map_bytes));
    close(fd);

    if (errors == 0) {
        std::cout << "  [PASS] All " << n_words << " words match.\n";
        return true;
    }
    std::cout << "  [FAIL] " << errors << " / " << n_words << " words mismatched.\n";
    return false;
}

void run_cxl_test() {
    std::cout << "\n===== CXL Device Discovery and Memory R/W Test =====\n";

    const auto devices = enumerate_cxl_devices();
    print_cxl_devices(devices);

    if (devices.empty()) {
        std::cout << "[INFO] No CXL DAX devices found — nothing to test.\n";
        return;
    }

    uint32_t pass_count = 0;
    uint32_t fail_count = 0;
    for (size_t i = 0; i < devices.size(); ++i) {
        if (test_one_device(devices[i], i)) ++pass_count;
        else                                ++fail_count;
    }

    std::cout << "\n===== RESULT: " << pass_count << " passed, "
              << fail_count << " failed =====\n";
}
