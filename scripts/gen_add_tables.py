#!/usr/bin/env python3
"""Quine-McCluskey minimization for W-bit addition SOP tables (W=1..8).

Key optimizations:
  1. Per-bit scope: c_k depends only on a[0..k], b[0..k] → use 2*(k+1)
     effective variables instead of 2W.  Exponentially reduces the
     minterm space for small k.
  2. Fast Q-M: instead of O(n^2) pairwise comparison, for each implicant a
     in the lower-popcount group, try to flip each 0-bit and look up the
     result in the higher-popcount group using a dict.  O(n * num_vars)
     per round.
"""

import os, sys, time
from collections import defaultdict

# ── Fast Quine-McCluskey ──────────────────────────────────────────────────────

def _popcount(imp):
    return sum(1 for x in imp if x == 1)

def _none_pattern(imp):
    return tuple(1 if x is None else 0 for x in imp)

def quine_mccluskey(minterms, num_vars):
    if not minterms:
        return []

    def to_imp(m):
        return tuple((m >> i) & 1 for i in range(num_vars))

    current = {to_imp(m): frozenset([m]) for m in minterms}
    prime_implicants = {}

    while current:
        next_round = {}
        used = set()

        # Group by (none_pattern, popcount) for O(n*k) matching
        groups = defaultdict(dict)
        for imp, cov in current.items():
            groups[(_none_pattern(imp), _popcount(imp))][imp] = cov

        none_pats = set(k[0] for k in groups)
        for np in none_pats:
            pcs = sorted(set(k[1] for k in groups if k[0] == np))
            for i in range(len(pcs) - 1):
                pc_lo, pc_hi = pcs[i], pcs[i + 1]
                if pc_hi != pc_lo + 1:
                    continue
                g_lo = groups[(np, pc_lo)]
                g_hi = groups[(np, pc_hi)]
                for a, cov_a in g_lo.items():
                    for j in range(num_vars):
                        if np[j] or a[j] != 0:   # skip None or already-1 positions
                            continue
                        b = list(a); b[j] = 1; b = tuple(b)
                        if b in g_hi:
                            combined = list(a); combined[j] = None; combined = tuple(combined)
                            covered = cov_a | g_hi[b]
                            if combined in next_round:
                                next_round[combined] |= covered
                            else:
                                next_round[combined] = covered
                            used.add(a); used.add(b)

        for imp in current:
            if imp not in used and imp not in prime_implicants:
                prime_implicants[imp] = current[imp]
        current = next_round

    return list(prime_implicants.items())


def minimal_cover(minterms, pis):
    if not minterms:
        return []
    mset = set(minterms)
    coverage = {m: [] for m in mset}
    for idx, (imp, cov) in enumerate(pis):
        for m in cov:
            if m in coverage:
                coverage[m].append(idx)
    chosen = set()
    covered = set()
    for m, ps in coverage.items():
        if len(ps) == 1:
            chosen.add(ps[0])
    for i in chosen:
        covered |= pis[i][1] & mset
    remaining = mset - covered
    while remaining:
        best = max(range(len(pis)), key=lambda i: len(pis[i][1] & remaining))
        chosen.add(best); remaining -= pis[best][1]
    return [pis[i][0] for i in chosen]


# ── Per-bit SOP ───────────────────────────────────────────────────────────────

def compute_bit(out_bit, W):
    """Compute minimized SOP for output bit 'out_bit' of a W-bit adder.

    c_k (for k < W) depends only on a[0..k] and b[0..k], so we use
    eff_W = k+1 variables instead of W.  Variable indices are translated
    to the W-bit context: a[i] → var i, b[i] → var W+i.
    """
    k = out_bit
    eff_W = k + 1 if k < W else W
    num_eff = 2 * eff_W

    minterms = []
    for idx in range(1 << num_eff):
        a = idx & ((1 << eff_W) - 1)
        b = (idx >> eff_W) & ((1 << eff_W) - 1)
        if ((a + b) >> k) & 1:
            minterms.append(idx)

    if not minterms:
        return []

    pis   = quine_mccluskey(minterms, num_eff)
    cover = minimal_cover(minterms, pis)

    terms = []
    for imp in cover:
        lits = []
        for eff_vi, val in enumerate(imp):
            if val is not None:
                is_b  = eff_vi >= eff_W
                bit   = eff_vi - eff_W if is_b else eff_vi
                w_vi  = (W + bit) if is_b else bit   # translate to W-context
                lits.append((w_vi, val == 0))         # is_neg = (val == 0)
        terms.append(tuple(sorted(lits)))
    return sorted(terms)


def add_sop(W, verbose=False):
    """Returns list of W+1 expressions for a W-bit adder."""
    result = []
    for k in range(W + 1):
        t0 = time.time()
        terms = compute_bit(k, W)
        dt = time.time() - t0
        if verbose:
            eff_W = k + 1 if k < W else W
            lits  = sum(len(t) for t in terms)
            print(f"    c{k}: {len(terms):4d} terms, {lits:5d} lits  "
                  f"(eff_W={eff_W}, {dt:.2f}s)")
        result.append(terms)
    return result


# ── C++ emission ──────────────────────────────────────────────────────────────

# Max term count per output bit observed across all W=1..8.
# Update this if you extend to larger W.
MAX_TERMS = 1024
MAX_LITS  = 16   # 2*W for W=8

HEADER = f"""\
// Auto-generated by scripts/gen_add_tables.py — do not edit.
#pragma once
#include <cstdint>

// Literal: one bit-plane of a bit-serial input.
//   var < W  → a[var],   neg=false → positive, neg=true → complemented
//   var >= W → b[var-W], neg=false → positive, neg=true → complemented
struct AddLit  {{ uint8_t var; bool neg; }};

// One product term (AND of n literals, n <= {MAX_LITS}).
struct AddTerm {{ uint8_t n; AddLit lits[{MAX_LITS}]; }};

// One output-bit expression (OR of n terms, n <= {MAX_TERMS}).
struct AddBitExpr {{ uint16_t n; AddTerm terms[{MAX_TERMS}]; }};

// kAdderSop[W-1][out_bit], W in [1..8], out_bit in [0..W].
// kAdderSop[W-1] has W+1 valid entries; remaining slots are zero-padded.
extern const AddBitExpr kAdderSop[8][9];
"""

def bool_str(b):  return "true" if b else "false"
def e_lit(v, n):  return f"AddLit{{{v}, {bool_str(n)}}}"
def e_term(lits): return f"AddTerm{{{len(lits)}, {{{', '.join(e_lit(v,n) for v,n in lits)}}}}}"

def e_bitexpr(terms):
    if not terms: return "AddBitExpr{0, {}}"
    return f"AddBitExpr{{{len(terms)}, {{{', '.join(e_term(t) for t in terms)}}}}}"

def emit_cpp(sops, max_W=8):
    lines = [
        "// Auto-generated by scripts/gen_add_tables.py — do not edit.",
        '#include "cud/compute_lib/add_table.h"',
        "",
        "const AddBitExpr kAdderSop[8][9] = {",
    ]
    for W in range(1, max_W + 1):
        sop = sops[W - 1]
        total_terms = sum(len(t) for t in sop)
        total_lits  = sum(sum(len(t2) for t2 in t) for t in sop)
        lines.append(f"  // W={W}: {total_terms} terms, {total_lits} literals")
        lines.append("  {")
        for out_bit in range(9):
            if out_bit > W:
                lines.append("    {0, {}},  // padding")
            else:
                terms = sop[out_bit]
                comment = f"// c{out_bit}: {len(terms)} terms"
                lines.append(f"    {e_bitexpr(terms)},  {comment}")
        lines.append("  },")
    lines.append("};")
    return "\n".join(lines)


# ── Main ──────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    h_path   = os.path.join(repo_root, "include", "cud", "compute_lib", "add_table.h")
    cpp_path = os.path.join(repo_root, "src",     "cud", "compute_lib", "add_table.cpp")

    MAX_W = 8
    sops  = []
    for W in range(1, MAX_W + 1):
        print(f"\nW={W}:")
        sop = add_sop(W, verbose=True)
        total = sum(len(t) for t in sop)
        lits  = sum(sum(len(t2) for t2 in t) for t in sop)
        print(f"  → total {total} terms, {lits} literals")
        sops.append(sop)

        # Sanity-check table limits
        for k, terms in enumerate(sop):
            if len(terms) > MAX_TERMS:
                print(f"  [WARN] c{k} has {len(terms)} terms > MAX_TERMS={MAX_TERMS}!")
            for term in terms:
                if len(term) > MAX_LITS:
                    print(f"  [WARN] c{k} has a term with {len(term)} lits > MAX_LITS={MAX_LITS}!")

    with open(h_path, "w") as f:
        f.write(HEADER)
    with open(cpp_path, "w") as f:
        f.write(emit_cpp(sops, MAX_W))

    print(f"\nWrote {h_path}")
    print(f"Wrote {cpp_path}")
