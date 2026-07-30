#!/usr/bin/env python3
"""
sys_info.py  –  Server workstation hardware snapshot
Outputs: CPU topology / frequency, cache hierarchy, DRAM configuration / NUMA.
Run as root (or with sudo) for full DIMM detail via dmidecode.
"""

import re
import subprocess
from pathlib import Path


# ── helpers ───────────────────────────────────────────────────────────────────

def run(cmd, use_sudo=False):
    """Execute a shell command; return stdout string or None on failure."""
    if use_sudo:
        cmd = f"sudo {cmd}"
    try:
        r = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=15)
        return r.stdout.strip() if r.returncode == 0 and r.stdout.strip() else None
    except Exception:
        return None


def read(path, default=''):
    try:
        return Path(path).read_text().strip()
    except Exception:
        return default


def gib(kb):
    return f"{int(kb) * 1024 / 2**30:.1f} GiB"


def hdr(title):
    bar = '─' * 64
    print(f"\n{bar}")
    print(f"  {title}")
    print(bar)


# ── CPU topology ──────────────────────────────────────────────────────────────

def cpu_info():
    hdr("CPU")
    cpuinfo = Path('/proc/cpuinfo').read_text()
    blocks  = [b for b in cpuinfo.split('\n\n') if 'processor' in b]

    # Model name
    m = re.search(r'^model name\s*:\s*(.+)$', cpuinfo, re.M)
    if m:
        print(f"  Model name   : {m.group(1).strip()}")

    # Vendor / stepping
    vendor = re.search(r'^vendor_id\s*:\s*(.+)$', cpuinfo, re.M)
    step   = re.search(r'^stepping\s*:\s*(.+)$',  cpuinfo, re.M)
    microc = re.search(r'^microcode\s*:\s*(.+)$', cpuinfo, re.M)
    if vendor: print(f"  Vendor       : {vendor.group(1).strip()}")
    if step:   print(f"  Stepping     : {step.group(1).strip()}")
    if microc: print(f"  Microcode    : {microc.group(1).strip()}")

    # Topology
    pkg_ids      = set(re.findall(r'^physical id\s*:\s*(\d+)', cpuinfo, re.M))
    n_sockets    = len(pkg_ids) if pkg_ids else 1
    logical_cpus = len(blocks)

    cores_per_pkg = {}
    for b in blocks:
        pm = re.search(r'^physical id\s*:\s*(\d+)', b, re.M)
        cm = re.search(r'^core id\s*:\s*(\d+)',    b, re.M)
        if pm and cm:
            cores_per_pkg.setdefault(pm.group(1), set()).add(cm.group(1))

    if cores_per_pkg:
        total_cores      = sum(len(v) for v in cores_per_pkg.values())
        cores_per_socket = max(len(v) for v in cores_per_pkg.values())
    else:
        total_cores      = logical_cpus // n_sockets
        cores_per_socket = total_cores  // n_sockets

    threads_per_core = logical_cpus // total_cores if total_cores else 1

    print(f"\n  Sockets      : {n_sockets}")
    print(f"  Cores/socket : {cores_per_socket}")
    print(f"  Total cores  : {total_cores}")
    print(f"  Logical CPUs : {logical_cpus}  "
          f"(HT {'ON' if threads_per_core > 1 else 'OFF'}, {threads_per_core} thread/core)")

    # Frequency
    freqs = [float(f) for f in re.findall(r'^cpu MHz\s*:\s*([\d.]+)', cpuinfo, re.M)]
    if freqs:
        print(f"  Freq (cur)   : min {min(freqs)/1000:.3f} GHz  "
              f"max {max(freqs)/1000:.3f} GHz  avg {sum(freqs)/len(freqs)/1000:.3f} GHz")

    # Max freq from cpufreq
    max_freq_path = '/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq'
    base_freq_path = '/sys/devices/system/cpu/cpu0/cpufreq/base_frequency'
    mf = read(max_freq_path)
    bf = read(base_freq_path)
    if mf: print(f"  Freq (max)   : {int(mf)/1e6:.3f} GHz")
    if bf: print(f"  Freq (base)  : {int(bf)/1e6:.3f} GHz")

    # Key ISA extensions
    flags = set(re.search(r'^flags\s*:\s*(.+)$', cpuinfo, re.M).group(1).split()
                if re.search(r'^flags\s*:', cpuinfo, re.M) else [])
    ext_groups = {
        'SIMD' : ['sse4_1','sse4_2','avx','avx2','avx512f','avx512bw','avx512vl'],
        'Crypto': ['aes','sha_ni','rdrand','rdseed'],
        'Virt'  : ['vmx','svm'],
    }
    for grp, exts in ext_groups.items():
        present = [e for e in exts if e in flags]
        if present:
            print(f"  {grp:<12} : {' '.join(present)}")


# ── Cache hierarchy ───────────────────────────────────────────────────────────

def cache_info():
    hdr("Cache Hierarchy")

    # Prefer sysfs (most detail, no external tool)
    cache_root = Path('/sys/devices/system/cpu/cpu0/cache')
    if cache_root.exists():
        seen = {}
        for idx in sorted(cache_root.iterdir()):
            if not (idx / 'level').exists():
                continue
            level = read(idx / 'level')
            kind  = read(idx / 'type')
            size  = read(idx / 'size')
            sets  = read(idx / 'number_of_sets')
            ways  = read(idx / 'ways_of_associativity')
            lsize = read(idx / 'coherency_line_size')
            cpus  = read(idx / 'shared_cpu_list')
            key   = (level, kind)
            if key not in seen:
                seen[key] = (size, sets, ways, lsize, cpus)

        for (level, kind), (size, sets, ways, lsize, cpus) in sorted(seen.items()):
            detail = []
            if ways:  detail.append(f"{ways}-way")
            if lsize: detail.append(f"{lsize}B line")
            if sets:  detail.append(f"{sets} sets")
            detail_str = '  (' + ', '.join(detail) + ')' if detail else ''
            print(f"  L{level} {kind:<12}: {size:>8}{detail_str}  shared [{cpus}]")
        return

    # Fallback: lscpu
    out = run('lscpu')
    if out:
        for line in out.splitlines():
            if re.match(r'\s*L[123][id]?\s*(cache|Cache)', line, re.I):
                k, _, v = line.partition(':')
                print(f"  {k.strip():<20}: {v.strip()}")
    else:
        print("  (unavailable)")


# ── DRAM configuration ────────────────────────────────────────────────────────

def dram_info():
    hdr("DRAM Configuration")

    # /proc/meminfo – always available
    meminfo = Path('/proc/meminfo').read_text()
    def mem_kb(key):
        m = re.search(rf'^{key}:\s+(\d+)\s+kB', meminfo, re.M)
        return int(m.group(1)) if m else None

    total = mem_kb('MemTotal')
    avail = mem_kb('MemAvailable')
    free  = mem_kb('MemFree')
    if total: print(f"  Total        : {gib(total)}")
    if avail: print(f"  Available    : {gib(avail)}")
    if free:  print(f"  Free         : {gib(free)}")

    # Huge pages
    hp_total = mem_kb('HugePages_Total')
    hp_size  = mem_kb('Hugepagesize')
    if hp_total and hp_total > 0 and hp_size:
        print(f"  HugePages    : {hp_total} × {hp_size} kB "
              f"= {hp_total * hp_size / 2**20:.1f} GiB")

    # NUMA topology from sysfs
    numa_root = Path('/sys/devices/system/node')
    nodes = sorted(numa_root.glob('node[0-9]*')) if numa_root.exists() else []
    if nodes:
        print(f"\n  NUMA nodes   : {len(nodes)}")
        for node in nodes:
            nm   = read(node / 'meminfo')
            m    = re.search(r'MemTotal:\s+(\d+) kB', nm)
            nmem = gib(m.group(1)) if m else '?'
            cpus = read(node / 'cpulist', '?')
            # distance
            dist_raw = read(node / 'distance', '')
            dist_str = f"  dist=[{dist_raw}]" if dist_raw else ''
            print(f"    {node.name:<8}: {nmem:>8}   cpus [{cpus}]{dist_str}")

    # dmidecode for DIMM-level detail
    dmi = run('dmidecode -t 17', use_sudo=True) or run('dmidecode -t 17')
    if dmi:
        blocks = dmi.split('Memory Device')[1:]   # index 0 is pre-header text
        print(f"\n  DIMM slots   : {len(blocks)}")

        dimms = []
        for b in blocks:
            def field(pat):
                fm = re.search(rf'^\s*{pat}:\s*(.+)$', b, re.M)
                return fm.group(1).strip() if fm else '?'

            size = field('Size')
            if 'No Module' in size or 'Not Installed' in size or size == '0':
                continue
            dimms.append({
                'loc'   : field('Locator'),
                'bank'  : field('Bank Locator'),
                'size'  : size,
                'type'  : field('Type'),
                'detail': field('Type Detail'),
                'speed' : field('Speed'),
                'rank'  : field('Rank'),
                'mfr'   : field('Manufacturer'),
                'part'  : field('Part Number'),
            })

        print(f"  Populated    : {len(dimms)}")
        if dimms:
            from collections import Counter
            profiles = Counter(
                (d['size'], d['type'], d['speed'], d['rank']) for d in dimms
            )
            print(f"  Config       :", end='')
            parts = [f"{n}× {sz} {typ} {spd} rank={rk}"
                     for (sz, typ, spd, rk), n in profiles.most_common()]
            print('  ' + ',  '.join(parts))

            print(f"\n  {'Locator':<28} {'Size':<10} {'Type':<10} "
                  f"{'Speed':<14} {'Rank':<6} {'Manufacturer'}")
            print(f"  {'-'*28} {'-'*10} {'-'*10} {'-'*14} {'-'*6} {'-'*16}")
            for d in dimms:
                print(f"  {d['loc']:<28} {d['size']:<10} {d['type']:<10} "
                      f"{d['speed']:<14} {d['rank']:<6} {d['mfr']}")
    else:
        print("\n  DIMM detail  : (run as root / sudo for dmidecode)")
        # Fallback: lsmem
        lsmem = run('lsmem --summary=never --output RANGE,SIZE,STATE,REMOVABLE,NODE')
        if lsmem:
            print("\n  Memory ranges (lsmem):")
            for line in lsmem.splitlines():
                print(f"    {line}")


# ── CXL presence (bonus, best-effort) ─────────────────────────────────────────

def cxl_info():
    cxl_devs = run("lspci | grep -i cxl")
    if not cxl_devs:
        cxl_devs = run("lspci | grep -i '0x0502\\|compute express'")
    if cxl_devs:
        hdr("CXL Devices (lspci)")
        for line in cxl_devs.splitlines():
            print(f"  {line}")


# ── entry point ───────────────────────────────────────────────────────────────

if __name__ == '__main__':
    import os, sys

    print('=' * 64)
    print('  Server Hardware Info')
    print(f"  Host : {run('hostname') or '?'}")
    os_line = read('/etc/os-release').splitlines()[0].split('=', 1)[-1].strip('"') \
              if Path('/etc/os-release').exists() else ''
    if os_line:
        print(f"  OS   : {os_line}")
    print(f"  Root : {'yes' if os.geteuid() == 0 else 'no  (run as root for DIMM detail)'}")
    print('=' * 64)

    cpu_info()
    cache_info()
    dram_info()
    cxl_info()
    print()
