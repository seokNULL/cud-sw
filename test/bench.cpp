#include "bench.h"
#include "cxl/address_map.h"
#include "cud/compute_lib/data_mapper.h"
#include "cud/compute_lib/inst_gen.h"
#include "cud/compute_lib/scratch.h"

#include <chrono>
#include <ctime>
#include <emmintrin.h>   // _mm_stream_si64, _mm_sfence, _mm_clflush, _mm_mfence
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <streambuf>
#include <string>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#endif

// ── Tee streambuf: duplicates writes to two streams ──────────────────────────

class TeeBuf : public std::streambuf {
    std::streambuf* a_;
    std::streambuf* b_;
public:
    TeeBuf(std::streambuf* a, std::streambuf* b) : a_(a), b_(b) {}
protected:
    int overflow(int c) override {
        if (c == EOF) return !EOF;
        if (a_->sputc((char)c) == EOF || b_->sputc((char)c) == EOF) return EOF;
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        a_->sputn(s, n);
        b_->sputn(s, n);
        return n;
    }
};

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr uint32_t kBenchBank = 0;

// CUD element count: NUM_COL columns × 64 bits per uint64_t = 65536 elements.
static constexpr size_t N_ELEM = (size_t)NUM_COL * 64;

// CPU element count: BENCH_CPU_SCALE × N_ELEM.
// First N_ELEM elements are identical to CUD input for comparison.
static constexpr size_t CPU_N = (size_t)BENCH_CPU_SCALE * N_ELEM;

// Bit-serial row bases (same layout as cud_compute.cpp)
static constexpr uint32_t kBaseA   =  0;
static constexpr uint32_t kBaseNA  =  8;
static constexpr uint32_t kBaseB   = 16;
static constexpr uint32_t kBaseNB  = 24;
static constexpr uint32_t kBaseOut = 32;   // max 32+16=48 for MUL W=8

// ── Timing ────────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;

static double us_since(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// ── Fast CXL.mem access (bank=0 only) ────────────────────────────────────────
//
// PA layout (bank=0):
//   PA[5:3]  = col[2:0]   (low 3 col bits → 8-byte stride within a cache line)
//   PA[9:6]  = bank       (0 for benchmark)
//   PA[16:10]= col[9:3]   (upper 7 col bits → 1024-byte stride between groups)
//   PA[33:17]= row
//
// Columns 0-7 share one 64-byte cache line, columns 8-15 the next (1024B away), etc.
// 128 cache lines per row, each 1024 bytes apart.

static inline uint64_t col_pa_off(uint32_t col) {
    return ((uint64_t)(col >> 3) << 10) | ((uint64_t)(col & 7) << 3);
}
static inline uint64_t row_pa(uint32_t row) { return (uint64_t)row << 17; }

// NT-store one uint64_t word to (bank=0, row, col) — no clflush needed.
static inline void nt_store(void* base, uint32_t row, uint32_t col, uint64_t word) {
    auto* ptr = reinterpret_cast<long long*>(
        static_cast<char*>(base) + row_pa(row) + col_pa_off(col));
    _mm_stream_si64(ptr, static_cast<long long>(word));
}

// Invalidate one cache-line group g of (bank=0, row) before a CPU read.
static inline void clflush_group(void* base, uint32_t row, uint32_t g) {
    _mm_clflush(static_cast<char*>(base) + row_pa(row) + ((uint64_t)g << 10));
}

// Read one uint64_t from (bank=0, row, col) — caller must have flushed first.
static inline uint64_t read_col(const void* base, uint32_t row, uint32_t col) {
    return *reinterpret_cast<const volatile uint64_t*>(
        static_cast<const char*>(base) + row_pa(row) + col_pa_off(col));
}

// ── Bit-serial packing / unpacking ───────────────────────────────────────────
//
// Optimizations vs. CudWriteRow path:
//   • No PA vector allocation per row (computed inline).
//   • NT stores bypass cache → no clflush needed after write.
//   • Packing loop parallelised over columns (no write-races between threads).

// Pack first N_ELEM elements of data into W bit-plane rows via NT stores.
static void pack_write(void* mem_base, uint32_t base_row,
                        uint8_t W, const std::vector<uint8_t>& data) {
    for (uint32_t k = 0; k < W; ++k) {
        #pragma omp parallel for schedule(static) num_threads(BENCH_CPU_THREADS)
        for (int col = 0; col < NUM_COL; ++col) {
            const uint8_t* d = data.data() + (size_t)col * 64;
            uint64_t word = 0;
            for (int b = 0; b < 64; ++b)
                if ((d[b] >> k) & 1u)
                    word |= 1ULL << b;
            nt_store(mem_base, base_row + k, (uint32_t)col, word);
        }
        _mm_sfence();   // ensure all NT stores for this bit-plane are visible
    }
}

// Batch-flush all cache lines of W_out rows, then unpack into uint32_t values.
static std::vector<uint32_t> read_unpack(void* mem_base, uint32_t base_row,
                                          uint8_t W_out) {
    // Flush all rows at once before reading (one mfence for the entire batch)
    for (uint32_t k = 0; k < W_out; ++k)
        for (uint32_t g = 0; g < NUM_COL / 8; ++g)
            clflush_group(mem_base, base_row + k, g);
    _mm_mfence();

    std::vector<uint32_t> out(N_ELEM, 0u);
    for (uint32_t k = 0; k < W_out; ++k) {
        #pragma omp parallel for schedule(static) num_threads(BENCH_CPU_THREADS)
        for (int col = 0; col < NUM_COL; ++col) {
            const uint64_t word = read_col(mem_base, base_row + k, (uint32_t)col);
            uint32_t* op = out.data() + (size_t)col * 64;
            for (int b = 0; b < 64; ++b)
                if ((word >> b) & 1u)
                    op[b] |= (1u << k);
        }
    }
    return out;
}

// ── Result printer ────────────────────────────────────────────────────────────

static void print_result(const std::vector<uint32_t>& cpu_out,
                          const std::vector<uint32_t>& cud_out,
                          const std::vector<uint8_t>&  a,
                          const std::vector<uint8_t>&  b,
                          uint32_t mask_out, uint8_t W_out, bool use_dec) {
    size_t n_err = 0;
    size_t ex_ok = N_ELEM, ex_bad = N_ELEM;
    for (size_t i = 0; i < N_ELEM; ++i) {
        const bool match = (cpu_out[i] & mask_out) == (cud_out[i] & mask_out);
        if (!match) { ++n_err; if (ex_bad == N_ELEM) ex_bad = i; }
        else         {          if (ex_ok  == N_ELEM) ex_ok  = i; }
    }

    const int fw = (W_out <= 8) ? 2 : 4;
    auto print_elem = [&](size_t i) {
        if (use_dec) {
            std::cout << "    [elem " << std::setw(6) << i << "]"
                      << "  a="   << std::setw(3) << (unsigned)a[i]
                      << "  b="   << std::setw(3) << (unsigned)b[i]
                      << "  cpu=" << std::setw(5) << (cpu_out[i] & mask_out)
                      << "  cud=" << std::setw(5) << (cud_out[i] & mask_out)
                      << "\n";
        } else {
            std::cout << std::hex << std::setfill('0')
                      << "    [elem " << std::dec << std::setw(6) << i << "]"
                      << "  a=0x"   << std::hex << std::setw(fw) << (unsigned)a[i]
                      << "  b=0x"   << std::setw(fw) << (unsigned)b[i]
                      << "  cpu=0x" << std::setw(fw) << (cpu_out[i] & mask_out)
                      << "  cud=0x" << std::setw(fw) << (cud_out[i] & mask_out)
                      << std::dec << std::setfill(' ') << "\n";
        }
    };

    if (n_err == 0) {
        std::cout << "  Result: MATCH  (" << N_ELEM << "/" << N_ELEM << " correct)\n";
    } else {
        std::cout << "  Result: MISMATCH  (" << (N_ELEM - n_err) << "/" << N_ELEM
                  << " correct, " << n_err << " errors)\n";
    }
    if (ex_ok  < N_ELEM) { std::cout << "  [match]   "; print_elem(ex_ok);  }
    if (ex_bad < N_ELEM) { std::cout << "  [mismatch]"; print_elem(ex_bad); }
}

// ── Per-operation benchmark ───────────────────────────────────────────────────

enum class BenchOp { XOR, ADD, MUL };

// cpu_a / cpu_b: CPU_N elements; first N_ELEM used for CUD.
static void bench_one(CxlMem& mem, CxlIo& io, BenchOp op, uint8_t W,
                       const std::vector<uint8_t>& cpu_a,
                       const std::vector<uint8_t>& cpu_b) {
    const uint8_t  W_out    = (op == BenchOp::MUL) ? (uint8_t)(2u * W) :
                              (op == BenchOp::ADD) ? (uint8_t)(W + 1u) : W;
    const uint32_t mask_in  = (1u << W) - 1u;
    const uint32_t mask_out = (W_out < 32u) ? ((1u << W_out) - 1u) : ~0u;

    // Mask CPU inputs to W bits (CPU_N elements)
    std::vector<uint8_t> am(CPU_N), bm(CPU_N);
    for (size_t i = 0; i < CPU_N; ++i) {
        am[i] = cpu_a[i] & (uint8_t)mask_in;
        bm[i] = cpu_b[i] & (uint8_t)mask_in;
    }

    // ── 1. CPU: N = CPU_N elements ────────────────────────────────────────────
    std::vector<uint32_t> cpu_out(CPU_N);
    auto t_cpu = Clock::now();
    if (op == BenchOp::XOR) {
        #pragma omp parallel for schedule(static) num_threads(BENCH_CPU_THREADS)
        for (size_t i = 0; i < CPU_N; ++i)
            cpu_out[i] = am[i] ^ bm[i];
    } else if (op == BenchOp::ADD) {
        #pragma omp parallel for schedule(static) num_threads(BENCH_CPU_THREADS)
        for (size_t i = 0; i < CPU_N; ++i)
            cpu_out[i] = (uint32_t)am[i] + bm[i];
    } else {
        #pragma omp parallel for schedule(static) num_threads(BENCH_CPU_THREADS)
        for (size_t i = 0; i < CPU_N; ++i)
            cpu_out[i] = (uint32_t)am[i] * bm[i];
    }
    const double cpu_us = us_since(t_cpu);

    // ── 2. CUD: N = N_ELEM elements (first N_ELEM of am/bm) ──────────────────

    // Precompute na, nb for CUD (first N_ELEM elements)
    std::vector<uint8_t> na(N_ELEM), nb(N_ELEM);
    for (size_t i = 0; i < N_ELEM; ++i) {
        na[i] = (~am[i]) & (uint8_t)mask_in;
        nb[i] = (~bm[i]) & (uint8_t)mask_in;
    }

    // Setup (not timed): kZeroRow, kOnesRow, zero output rows
    ScratchAllocator scratch(kBenchBank, row_to_mat(kBaseA));
    CudWriteRow(mem, kBenchBank, scratch.abs_row(kZeroRow),  0ULL);
    CudWriteRow(mem, kBenchBank, scratch.abs_row(kOnesRow), ~0ULL);
    for (uint32_t k = 0; k < W_out; ++k)
        CudWriteRow(mem, kBenchBank, kBaseOut + k, 0ULL);

    // CXL.mem write (includes bit-serial format conversion)
    auto t_write = Clock::now();
    pack_write(mem.base(), kBaseA,  W, am);
    pack_write(mem.base(), kBaseNA, W, na);
    pack_write(mem.base(), kBaseB,  W, bm);
    pack_write(mem.base(), kBaseNB, W, nb);
    const double write_us = us_since(t_write);

    // Instruction generation
    const BitSerialLayout la   = {kBenchBank, kBaseA,   W,     NUM_COL};
    const BitSerialLayout lna  = {kBenchBank, kBaseNA,  W,     NUM_COL};
    const BitSerialLayout lb   = {kBenchBank, kBaseB,   W,     NUM_COL};
    const BitSerialLayout lnb  = {kBenchBank, kBaseNB,  W,     NUM_COL};
    const BitSerialLayout lout = {kBenchBank, kBaseOut, W_out, NUM_COL};

    auto t_gen = Clock::now();
    std::vector<CudInst> insts;
    switch (op) {
    case BenchOp::XOR: insts = gen_xor(la, lna, lb, lnb, lout, scratch); break;
    case BenchOp::ADD: insts = gen_add(la, lna, lb, lnb, lout, W, scratch); break;
    case BenchOp::MUL: insts = gen_mul(la, lna, lb, lnb, lout, W, scratch); break;
    }
    const double gen_us = us_since(t_gen);

    // CXL.io execute (instruction transfer + hardware execution + done poll)
    auto t_exec = Clock::now();
    const bool ok = CudExecute(io, insts);
    const double exec_us = us_since(t_exec);

    if (!ok) {
        std::cout << "  [CUD TIMEOUT]\n";
        return;
    }

    // CXL.mem read (includes bit-serial→packed conversion)
    auto t_read = Clock::now();
    const auto cud_out = read_unpack(mem.base(), kBaseOut, W_out);
    const double read_us = us_since(t_read);

    const double cud_total = write_us + gen_us + exec_us + read_us;

    // ── Print ─────────────────────────────────────────────────────────────────
    const char* opstr = (op == BenchOp::XOR) ? "XOR" :
                        (op == BenchOp::ADD) ? "ADD" : "MUL";

    std::cout << std::fixed << std::setprecision(1)
              << "\n  ── " << opstr << "  W=" << (int)W
              << "  (out=" << (int)W_out << "bit, " << insts.size() << " insts) ──\n"
              << "  CPU  (" << BENCH_CPU_THREADS << " threads, N="
              << CPU_N << ")  : " << std::setw(9) << cpu_us    << " us\n"
              << "  CUD  (N=" << N_ELEM << ")\n"
              << "       mem-write           : " << std::setw(9) << write_us  << " us\n"
              << "       inst-gen            : " << std::setw(9) << gen_us    << " us\n"
              << "       io-exec             : " << std::setw(9) << exec_us   << " us\n"
              << "       mem-read            : " << std::setw(9) << read_us   << " us\n"
              << "       total               : " << std::setw(9) << cud_total << " us\n";

    // Compare first N_ELEM elements (shared input between CPU and CUD)
    print_result(cpu_out, cud_out, am, bm, mask_out, W_out, op != BenchOp::XOR);
}

// ── Top-level entry point ─────────────────────────────────────────────────────

void run_benchmark(CxlMem& mem, CxlIo& io) {
    // Build log filename: bench_YYYYMMDD_HHMMSS.txt
    char fname[32];
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::strftime(fname, sizeof(fname), "bench_%Y%m%d_%H%M%S.txt",
                      std::localtime(&t));
    }
    std::ofstream logfile(fname);

    // Tee: all std::cout output goes to both terminal and log file
    TeeBuf tee(std::cout.rdbuf(), logfile.rdbuf());
    std::streambuf* orig = std::cout.rdbuf(&tee);

    std::cout << "\n===== CUD vs CPU Benchmark =====\n"
              << "  Log    : " << fname << "\n"
              << "  CPU    : " << BENCH_CPU_THREADS << " OMP threads"
              << ", N=" << CPU_N << " elements\n"
              << "  CUD    : N=" << N_ELEM << " elements  (1/" << BENCH_CPU_SCALE
              << " of CPU)\n"
              << "  Compare: first " << N_ELEM << " elements (shared input)\n"
              << "  Note   : mem-write/read times include bit-serial conversion\n";

    // Random data for CPU_N elements; first N_ELEM are fed to CUD as well
    std::mt19937 rng(42u);
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    std::vector<uint8_t> a(CPU_N), b(CPU_N);
    for (auto& x : a) x = (uint8_t)dist(rng);
    for (auto& x : b) x = (uint8_t)dist(rng);

    static constexpr uint8_t kWidths[] = {1, 2, 4, 8};

    std::cout << "\n[XOR]\n";
    for (uint8_t W : kWidths) bench_one(mem, io, BenchOp::XOR, W, a, b);

    std::cout << "\n[ADD]\n";
    for (uint8_t W : kWidths) bench_one(mem, io, BenchOp::ADD, W, a, b);

    std::cout << "\n[MUL]\n";
    for (uint8_t W : kWidths) bench_one(mem, io, BenchOp::MUL, W, a, b);

    std::cout << "\n================================\n"
              << "  Saved: " << fname << "\n";

    std::cout.rdbuf(orig);  // restore cout
}
