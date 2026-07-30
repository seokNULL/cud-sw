#include "bench.h"
#include "cxl/address_map.h"
#include "cud/compute_lib/data_mapper.h"
#include "cud/compute_lib/inst_gen.h"
#include "cud/compute_lib/scratch.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#endif

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr uint32_t kBenchBank = 0;

// 65536 logical elements: NUM_COL columns × 64 bits per uint64_t.
// Each element occupies one bit position across W bit-plane rows.
static constexpr size_t N_ELEM = (size_t)NUM_COL * 64;

// Bit-serial row bases (same layout as cud_compute.cpp)
static constexpr uint32_t kBaseA   =  0;   // W rows for a
static constexpr uint32_t kBaseNA  =  8;   // W rows for ~a
static constexpr uint32_t kBaseB   = 16;   // W rows for b
static constexpr uint32_t kBaseNB  = 24;   // W rows for ~b
static constexpr uint32_t kBaseOut = 32;   // W_out rows (max 32+16=48 for MUL W=8)

// ── Timing ────────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;

static double us_since(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// ── Bit-serial packing / unpacking ───────────────────────────────────────────

// Pack N_ELEM uint8_t elements into W bit-plane rows on CXL.mem.
// Element i, bit k → column (i/64), bit (i%64) of row (base + k).
static void pack_write(CxlMem& mem, uint32_t bank, uint32_t base,
                        uint8_t W, const std::vector<uint8_t>& data) {
    std::vector<uint64_t> row(NUM_COL);
    for (uint32_t k = 0; k < W; ++k) {
        std::fill(row.begin(), row.end(), 0ULL);
        for (size_t i = 0; i < N_ELEM; ++i)
            if ((data[i] >> k) & 1u)
                row[i >> 6] |= 1ULL << (i & 63u);
        CudWriteRow(mem, bank, base + k, row);
    }
}

// Unpack W_out bit-plane rows from CXL.mem into N_ELEM uint32_t values.
// Supports W_out up to 32 (covers ADD W=8 → 9 bits, MUL W=8 → 16 bits).
static std::vector<uint32_t> read_unpack(CxlMem& mem, uint32_t bank,
                                          uint32_t base, uint8_t W_out) {
    std::vector<uint32_t> out(N_ELEM, 0u);
    for (uint32_t k = 0; k < W_out; ++k) {
        auto row = CudReadRow(mem, bank, base + k);
        for (size_t i = 0; i < N_ELEM; ++i)
            if ((row[i >> 6] >> (i & 63u)) & 1u)
                out[i] |= (1u << k);
    }
    return out;
}

// ── Per-operation benchmark ───────────────────────────────────────────────────

enum class BenchOp { XOR, ADD, MUL };

static void bench_one(CxlMem& mem, CxlIo& io, BenchOp op, uint8_t W,
                       const std::vector<uint8_t>& raw_a,
                       const std::vector<uint8_t>& raw_b) {
    // W_out: XOR=W, ADD=W+1, MUL=2W
    const uint8_t  W_out    = (op == BenchOp::MUL) ? (uint8_t)(2u * W) :
                              (op == BenchOp::ADD) ? (uint8_t)(W + 1u) : W;
    const uint32_t mask_in  = (1u << W) - 1u;
    const uint32_t mask_out = (W_out < 32u) ? ((1u << W_out) - 1u) : ~0u;

    // Mask inputs to W bits
    std::vector<uint8_t> a(N_ELEM), b(N_ELEM), na(N_ELEM), nb(N_ELEM);
    for (size_t i = 0; i < N_ELEM; ++i) {
        a[i]  = raw_a[i] & (uint8_t)mask_in;
        b[i]  = raw_b[i] & (uint8_t)mask_in;
        na[i] = (~a[i])  & (uint8_t)mask_in;
        nb[i] = (~b[i])  & (uint8_t)mask_in;
    }

    // ── 1. CPU: malloc + init already done (a/b/na/nb above = effective init)

    // ── 2. CPU benchmark ──────────────────────────────────────────────────────
    // OpenMP parallel for (compiler auto-vectorises with -O2 -fopenmp).
    // No standard BLAS covers element-wise sub-byte arithmetic.
    std::vector<uint32_t> cpu_out(N_ELEM);
    auto t_cpu = Clock::now();
    if (op == BenchOp::XOR) {
        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < N_ELEM; ++i)
            cpu_out[i] = a[i] ^ b[i];
    } else if (op == BenchOp::ADD) {
        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < N_ELEM; ++i)
            cpu_out[i] = (uint32_t)a[i] + b[i];
    } else {
        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < N_ELEM; ++i)
            cpu_out[i] = (uint32_t)a[i] * b[i];
    }
    const double cpu_us = us_since(t_cpu);

    // ── 3. CUD benchmark ─────────────────────────────────────────────────────

    // Setup: kZeroRow, kOnesRow, clear output (not included in timed phases)
    ScratchAllocator scratch(kBenchBank, row_to_mat(kBaseA));
    CudWriteRow(mem, kBenchBank, scratch.abs_row(kZeroRow),  0ULL);
    CudWriteRow(mem, kBenchBank, scratch.abs_row(kOnesRow), ~0ULL);
    for (uint32_t k = 0; k < W_out; ++k)
        CudWriteRow(mem, kBenchBank, kBaseOut + k, 0ULL);

    // CXL.mem write (includes bit-serial format conversion)
    auto t_write = Clock::now();
    pack_write(mem, kBenchBank, kBaseA,  W, a);
    pack_write(mem, kBenchBank, kBaseNA, W, na);
    pack_write(mem, kBenchBank, kBaseB,  W, b);
    pack_write(mem, kBenchBank, kBaseNB, W, nb);
    const double write_us = us_since(t_write);

    // Instruction generation (software, CPU side)
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
    const auto cud_out = read_unpack(mem, kBenchBank, kBaseOut, W_out);
    const double read_us = us_since(t_read);

    const double cud_total = write_us + gen_us + exec_us + read_us;

    // ── 4. Compare results ────────────────────────────────────────────────────
    size_t errors = 0;
    for (size_t i = 0; i < N_ELEM; ++i)
        if ((cpu_out[i] & mask_out) != (cud_out[i] & mask_out))
            ++errors;

    const std::string match_str = (errors == 0)
        ? "MATCH"
        : "MISMATCH (" + std::to_string(errors) + " errors)";

    // ── Print ─────────────────────────────────────────────────────────────────
#ifdef _OPENMP
    const int nthreads = omp_get_max_threads();
#else
    const int nthreads = 1;
#endif
    const char* names[] = {"XOR", "ADD", "MUL"};
    const char* opstr = names[(int)op];

    std::cout << std::fixed << std::setprecision(1)
              << "\n  ── " << opstr << "  W=" << (int)W
              << "  (out=" << (int)W_out << "bit, " << insts.size() << " insts) ──\n"
              << "  CPU  (" << nthreads << " OMP threads) : "
              << std::setw(9) << cpu_us    << " us\n"
              << "  CUD  mem-write          : "
              << std::setw(9) << write_us  << " us\n"
              << "       inst-gen           : "
              << std::setw(9) << gen_us    << " us\n"
              << "       io-exec            : "
              << std::setw(9) << exec_us   << " us\n"
              << "       mem-read           : "
              << std::setw(9) << read_us   << " us\n"
              << "       total              : "
              << std::setw(9) << cud_total << " us\n"
              << "  Result: " << match_str << "\n";
}

// ── Top-level entry point ─────────────────────────────────────────────────────

void run_benchmark(CxlMem& mem, CxlIo& io) {
    std::cout << "\n===== CUD vs CPU Benchmark =====\n";
#ifdef _OPENMP
    std::cout << "  OpenMP : " << omp_get_max_threads() << " threads\n";
#else
    std::cout << "  OpenMP : not available (single-threaded)\n";
#endif
    std::cout << "  Data   : N=" << N_ELEM << " elements per operation\n"
              << "  Note   : CXL.mem write/read times include bit-serial"
                 " format conversion\n";

    // Random data shared across all widths so results are reproducible
    std::mt19937 rng(42u);
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    std::vector<uint8_t> a(N_ELEM), b(N_ELEM);
    for (auto& x : a) x = (uint8_t)dist(rng);
    for (auto& x : b) x = (uint8_t)dist(rng);

    static constexpr uint8_t kWidths[] = {1, 2, 4, 8};

    std::cout << "\n[XOR]\n";
    for (uint8_t W : kWidths) bench_one(mem, io, BenchOp::XOR, W, a, b);

    std::cout << "\n[ADD]\n";
    for (uint8_t W : kWidths) bench_one(mem, io, BenchOp::ADD, W, a, b);

    std::cout << "\n[MUL]\n";
    for (uint8_t W : kWidths) bench_one(mem, io, BenchOp::MUL, W, a, b);

    std::cout << "\n================================\n";
}
