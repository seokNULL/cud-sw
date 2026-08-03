#include "bench.h"
#include "cxl/address_map.h"
#include "cud/compute_lib/data_mapper.h"
#include "cud/compute_lib/inst_gen.h"
#include "cud/compute_lib/scratch.h"

#include <chrono>
#include <ctime>
#include <emmintrin.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#endif

// ── Tee streambuf ─────────────────────────────────────────────────────────────

class TeeBuf : public std::streambuf {
    std::streambuf* a_; std::streambuf* b_;
public:
    TeeBuf(std::streambuf* a, std::streambuf* b) : a_(a), b_(b) {}
protected:
    int overflow(int c) override {
        if (c == EOF) return !EOF;
        if (a_->sputc((char)c) == EOF || b_->sputc((char)c) == EOF) return EOF;
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        a_->sputn(s, n); b_->sputn(s, n); return n;
    }
};

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr uint32_t kBenchBank = 0;
static constexpr size_t   N_ELEM     = (size_t)NUM_COL * 64;   // 65 536 per tile
static constexpr size_t   CPU_N      = (size_t)BENCH_CPU_SCALE * N_ELEM;

// Row offsets within a mat for one tile's data
static constexpr uint32_t kOffA   =  0;
static constexpr uint32_t kOffNA  =  8;
static constexpr uint32_t kOffB   = 16;
static constexpr uint32_t kOffNB  = 24;
static constexpr uint32_t kOffOut = 32;  // up to +15 for MUL W=8

static constexpr uint8_t kWidths[] = {1, 2, 4, 8};

// ── Timing ────────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;
static double us_since(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// ── Fast CXL.mem access (bank = 0) ───────────────────────────────────────────

static inline uint64_t col_pa_off(uint32_t col) {
    return ((uint64_t)(col >> 3) << 10) | ((uint64_t)(col & 7) << 3);
}
static inline uint64_t row_pa(uint32_t row) { return (uint64_t)row << 17; }

static inline void nt_store(void* base, uint32_t row, uint32_t col, uint64_t w) {
    _mm_stream_si64(reinterpret_cast<long long*>(
        static_cast<char*>(base) + row_pa(row) + col_pa_off(col)),
        static_cast<long long>(w));
}
static inline void clflush_group(void* base, uint32_t row, uint32_t g) {
    _mm_clflush(static_cast<char*>(base) + row_pa(row) + ((uint64_t)g << 10));
}
static inline uint64_t read_col(const void* base, uint32_t row, uint32_t col) {
    return *reinterpret_cast<const volatile uint64_t*>(
        static_cast<const char*>(base) + row_pa(row) + col_pa_off(col));
}

// ── Bit-serial packing / unpacking ───────────────────────────────────────────

static void pack_write(void* mem_base, uint32_t base_row,
                       uint8_t W, const std::vector<uint8_t>& data) {
    for (uint32_t k = 0; k < W; ++k) {
        #pragma omp parallel for schedule(static) num_threads(BENCH_CPU_THREADS)
        for (int col = 0; col < NUM_COL; ++col) {
            const uint8_t* d = data.data() + (size_t)col * 64;
            uint64_t word = 0;
            for (int b = 0; b < 64; ++b)
                if ((d[b] >> k) & 1u) word |= 1ULL << b;
            nt_store(mem_base, base_row + k, (uint32_t)col, word);
        }
        _mm_sfence();
    }
}

static std::vector<uint32_t> read_unpack(void* mem_base, uint32_t base_row,
                                          uint8_t W_out) {
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
                if ((word >> b) & 1u) op[b] |= (1u << k);
        }
    }
    return out;
}

// ── Tile helpers ──────────────────────────────────────────────────────────────

static uint32_t tile_row(uint32_t tile_idx, uint32_t off) {
    return mat_to_row_start(tile_idx) + off;
}

static void tile_setup(CxlMem& mem, uint8_t W_out) {
    CudWriteRow(mem, kBenchBank, tile_row(0, kZeroRow), 0ULL);
    CudWriteRow(mem, kBenchBank, tile_row(0, kOnesRow), ~0ULL);
    for (uint32_t k = 0; k < W_out; ++k)
        CudWriteRow(mem, kBenchBank, tile_row(0, kOffOut + k), 0ULL);
}

static void tile_write(void* mem_base, uint8_t W,
                       const std::vector<uint8_t>& a,  const std::vector<uint8_t>& na,
                       const std::vector<uint8_t>& b,  const std::vector<uint8_t>& nb) {
    pack_write(mem_base, tile_row(0, kOffA),  W, a);
    pack_write(mem_base, tile_row(0, kOffNA), W, na);
    pack_write(mem_base, tile_row(0, kOffB),  W, b);
    pack_write(mem_base, tile_row(0, kOffNB), W, nb);
}

enum class BenchOp { XOR, AND, OR, ADD, MUL };

static std::vector<CudInst> tile_gen_insts(BenchOp op, uint8_t W, uint8_t W_out) {
    ScratchAllocator scratch(kBenchBank, 0);
    const BitSerialLayout la   = {kBenchBank, tile_row(0, kOffA),   W,     NUM_COL};
    const BitSerialLayout lna  = {kBenchBank, tile_row(0, kOffNA),  W,     NUM_COL};
    const BitSerialLayout lb   = {kBenchBank, tile_row(0, kOffB),   W,     NUM_COL};
    const BitSerialLayout lnb  = {kBenchBank, tile_row(0, kOffNB),  W,     NUM_COL};
    const BitSerialLayout lout = {kBenchBank, tile_row(0, kOffOut), W_out, NUM_COL};
    switch (op) {
    case BenchOp::AND: return gen_and(la, lb, lout, scratch);
    case BenchOp::OR:  return gen_or (la, lb, lout, scratch);
    case BenchOp::XOR: return gen_xor(la, lna, lb, lnb, lout, scratch);
    case BenchOp::ADD: return gen_add(la, lna, lb, lnb, lout, W, scratch);
    case BenchOp::MUL: return gen_mul(la, lna, lb, lnb, lout, W, scratch);
    }
    return {};
}

static std::vector<uint32_t> tile_read(void* mem_base, uint8_t W_out) {
    return read_unpack(mem_base, tile_row(0, kOffOut), W_out);
}

// ── Result verification ───────────────────────────────────────────────────────

static void print_result(const std::vector<uint32_t>& cpu_out,
                         const std::vector<uint32_t>& cud_out,
                         const std::vector<uint8_t>& a,
                         const std::vector<uint8_t>& b,
                         uint32_t mask_out, uint8_t W_out, bool use_dec) {
    size_t n_err = 0, ex_ok = N_ELEM, ex_bad = N_ELEM;
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
                      << "  cud=" << std::setw(5) << (cud_out[i] & mask_out) << "\n";
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
    if (n_err == 0)
        std::cout << "  MATCH  (" << N_ELEM << "/" << N_ELEM << " correct)\n";
    else
        std::cout << "  MISMATCH  (" << (N_ELEM - n_err) << "/" << N_ELEM
                  << " correct, " << n_err << " errors)\n";
    if (ex_ok  < N_ELEM) { std::cout << "    [match]  "; print_elem(ex_ok);  }
    if (ex_bad < N_ELEM) { std::cout << "    [error]  "; print_elem(ex_bad); }
}

// ── Per-case benchmark ────────────────────────────────────────────────────────

static void bench_one(CxlMem& mem, CxlIo& io, BenchOp op, uint8_t W,
                      const std::vector<uint8_t>& cpu_a, const std::vector<uint8_t>& cpu_b) {
    const uint8_t W_out = (op == BenchOp::ADD) ? (uint8_t)(W + 1u) :
                          (op == BenchOp::MUL) ? (W == 1 ? (uint8_t)1u : (uint8_t)(2u * W)) :
                          W;
    const uint32_t mask_in  = (1u << W) - 1u;
    const uint32_t mask_out = (W_out < 32u) ? ((1u << W_out) - 1u) : ~0u;

    std::vector<uint8_t> am(CPU_N), bm(CPU_N);
    for (size_t i = 0; i < CPU_N; ++i) {
        am[i] = cpu_a[i] & (uint8_t)mask_in;
        bm[i] = cpu_b[i] & (uint8_t)mask_in;
    }

    const std::vector<uint8_t> a_tile(am.begin(), am.begin() + N_ELEM);
    const std::vector<uint8_t> b_tile(bm.begin(), bm.begin() + N_ELEM);
    std::vector<uint8_t> na(N_ELEM), nb(N_ELEM);
    for (size_t i = 0; i < N_ELEM; ++i) {
        na[i] = (~am[i]) & (uint8_t)mask_in;
        nb[i] = (~bm[i]) & (uint8_t)mask_in;
    }

    // ── CPU baseline ──────────────────────────────────────────────────────────
    std::vector<uint32_t> cpu_out(CPU_N);
    const auto t_cpu = Clock::now();
    #pragma omp parallel for schedule(static) num_threads(BENCH_CPU_THREADS)
    for (size_t i = 0; i < CPU_N; ++i) {
        switch (op) {
        case BenchOp::AND: cpu_out[i] = am[i] & bm[i];           break;
        case BenchOp::OR:  cpu_out[i] = am[i] | bm[i];           break;
        case BenchOp::XOR: cpu_out[i] = am[i] ^ bm[i];           break;
        case BenchOp::ADD: cpu_out[i] = (uint32_t)am[i] + bm[i]; break;
        case BenchOp::MUL: cpu_out[i] = (uint32_t)am[i] * bm[i]; break;
        }
    }
    const double cpu_us = us_since(t_cpu);

    const size_t n_insts = tile_gen_insts(op, W, W_out).size();
    const char* opstr = (op == BenchOp::AND) ? "AND" :
                        (op == BenchOp::OR)  ? "OR"  :
                        (op == BenchOp::XOR) ? "XOR" :
                        (op == BenchOp::ADD) ? "ADD" : "MUL";

    std::cout << std::fixed << std::setprecision(1)
              << "\n  ── " << opstr << "  W=" << (int)W
              << "  (out=" << (int)W_out << "bit, " << n_insts << " insts) ──\n";

    std::cout << "  CPU  (" << BENCH_CPU_THREADS << " threads, N=" << CPU_N << ")  : "
              << std::setw(9) << cpu_us << " us\n";

    // ── CUD: write → gen → exec → read ───────────────────────────────────────
    tile_setup(mem, W_out);

    const auto tw = Clock::now();
    tile_write(mem.base(), W, a_tile, na, b_tile, nb);
    const double t_write = us_since(tw);

    const auto tg = Clock::now();
    const auto insts = tile_gen_insts(op, W, W_out);
    const double t_gen = us_since(tg);

    const auto te = Clock::now();
    if (!CudExecute(io, insts)) {
        std::cout << "  [CUD TIMEOUT]\n";
        return;
    }
    const double t_exec = us_since(te);

    const auto tr = Clock::now();
    const auto cud_out = tile_read(mem.base(), W_out);
    const double t_read = us_since(tr);

    const double t_total = t_write + t_gen + t_exec + t_read;
    std::cout << "  CUD:\n"
              << "       write_input         : " << std::setw(9) << t_write << " us\n"
              << "       generate_insts      : " << std::setw(9) << t_gen   << " us\n"
              << "       execute             : " << std::setw(9) << t_exec  << " us\n"
              << "       read_result         : " << std::setw(9) << t_read  << " us\n"
              << "       total               : " << std::setw(9) << t_total << " us\n";

    const bool use_dec = (op == BenchOp::ADD || op == BenchOp::MUL);
    print_result(cpu_out, cud_out, a_tile, b_tile, mask_out, W_out, use_dec);
}

// ── Entry point ───────────────────────────────────────────────────────────────

void run_benchmark(CxlMem& mem, CxlIo& io) {
    char fname[32];
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::strftime(fname, sizeof(fname), "bench_%Y%m%d_%H%M%S.txt", std::localtime(&t));
    }
    std::ofstream logfile(fname);
    TeeBuf tee(std::cout.rdbuf(), logfile.rdbuf());
    std::streambuf* orig = std::cout.rdbuf(&tee);

    std::cout << "\n===== CUD vs CPU Benchmark =====\n"
              << "  Log    : " << fname << "\n"
              << "  CPU    : " << BENCH_CPU_THREADS << " OMP threads, N=" << CPU_N << "\n"
              << "  Tile   : " << N_ELEM << " elem  (" << NUM_COL << " col x 64 bit)\n";

    std::mt19937 rng(42u);
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    std::vector<uint8_t> a(CPU_N), b(CPU_N);
    for (auto& x : a) x = (uint8_t)dist(rng);
    for (auto& x : b) x = (uint8_t)dist(rng);

    std::cout << "\n[Bitwise]\n";
    for (BenchOp op : {BenchOp::XOR, BenchOp::AND, BenchOp::OR})
        for (uint8_t W : kWidths)
            bench_one(mem, io, op, W, a, b);

    std::cout << "\n[Arithmetic]\n";
    for (BenchOp op : {BenchOp::ADD, BenchOp::MUL})
        for (uint8_t W : kWidths)
            bench_one(mem, io, op, W, a, b);

    std::cout << "\n================================\n"
              << "  Saved: " << fname << "\n";
    std::cout.rdbuf(orig);
}
