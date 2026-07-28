#!/usr/bin/env python3
"""prof_hot.py — resolve a host-PC profile into GUEST functions, ranked by real CPU time.

WHY THIS MATTERS FOR OWNERSHIP. Native-ownership targets have been chosen so far by counting `jal`
sites in the image. That proxy has two holes it cannot close: it cannot see indirect calls, and a
function called from 136 places may run less often than one called from 3 inside a loop. So "the
high-caller queue is exhausted" was a statement about the disassembly, not about the running port.
This closes the gap with a measurement.

HOW. The port samples its host program counter (PSXPORT_PROF=1, hostprof.cpp) and writes raw PCs
with counts. Every recompiled guest function is a real C symbol — `gen_func_<GUESTADDR>` for the
resident module, `ov_<tag>_gen_<GUESTADDR>` for an overlay — so `nm` on the binary maps host
addresses back to guest ones. Nothing is inferred: a sample is attributed to the symbol whose range
contains it, or to nothing.

READ THE OUTPUT HONESTLY:
  * Samples in NATIVE bodies are the port's own C, not guest code — they are marked, because a hot
    native function is a performance result, not an ownership target.
  * `(non-guest)` is everything else: the renderer, the CD model, libc. A port that spends most of
    its time there is telling you the guest code is not the bottleneck at all.
  * Sampling is CPU-time based, so time blocked on I/O or frame pacing does not appear.

Usage:
  prof_hot.py [--prof scratch/raw/prof_host.txt] [--bin scratch/bin/spyro_port] [--top 25]
"""
import argparse
import bisect
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def symbols(binary):
    """(start, name) sorted, from nm. Only symbols with a size are usable for range attribution."""
    out = subprocess.run(["nm", "-S", "--defined-only", binary],
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.decode(errors="replace")
    syms = []
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 4:
            try:
                addr, size = int(p[0], 16), int(p[1], 16)
            except ValueError:
                continue
            syms.append((addr, addr + size, p[3]))
        elif len(p) == 3:
            try:
                addr = int(p[0], 16)
            except ValueError:
                continue
            syms.append((addr, addr, p[2]))
    syms.sort()
    return syms


def guest_addr(name):
    """The guest address a recompiled symbol stands for, or None if it is not guest code.

    NOT anchored at end-of-string. These are C++ symbols, so nm reports the MANGLED form —
    `_Z17gen_func_800258F0P4Core`, with the parameter type appended after the address. Anchoring on
    `$` matched none of them and the report confidently announced "guest code: 0.0% of samples",
    which was an artifact of this regex rather than a fact about the port."""
    m = re.search(r"gen_(?:func_)?([0-9A-Fa-f]{8})", name)
    return int(m.group(1), 16) if m else None


def owned_set():
    """Guest addresses this port owns natively, from the ndiff_run sites — so hot NATIVE code can be
    distinguished from hot SUBSTRATE code, which are opposite conclusions."""
    owned = {}
    d = os.path.join(REPO, "game", "core")
    if os.path.isdir(d):
        for fn in sorted(os.listdir(d)):
            if fn.endswith(".cpp"):
                for m in re.finditer(r'ndiff_run\(c,\s*"([^"@]*)@0x([0-9A-Fa-f]+)"',
                                     open(os.path.join(d, fn)).read()):
                    owned[int(m.group(2), 16)] = m.group(1)
    return owned


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prof", default="scratch/raw/prof_host.txt")
    ap.add_argument("--bin", default="scratch/bin/spyro_port")
    ap.add_argument("--top", type=int, default=25)
    a = ap.parse_args()

    prof = os.path.join(REPO, a.prof) if not os.path.isabs(a.prof) else a.prof
    binary = os.path.join(REPO, a.bin) if not os.path.isabs(a.bin) else a.bin
    if not os.path.exists(prof):
        sys.exit(f"prof_hot: no profile at {a.prof} — run with PSXPORT_PROF=1 first")

    # A PROFILE ONLY MEANS ANYTHING AGAINST THE BINARY THAT PRODUCED IT. Symbol addresses move on
    # every relink, so resolving an old profile against a new build silently attributes samples to
    # whatever now occupies those addresses. I nearly reported a before/after comparison built exactly
    # that way — the "before" number changed from 6.06% to 4.88% purely because the binary had been
    # rebuilt in between, which is not a measurement of anything.
    if os.path.getmtime(binary) > os.path.getmtime(prof):
        print(f"REFUSING: {os.path.basename(binary)} is NEWER than {os.path.basename(prof)}.\n"
              f"  The binary was relinked after this profile was taken, so its symbol addresses no\n"
              f"  longer match the sampled PCs and every attribution below would be fiction.\n"
              f"  Re-run the port with PSXPORT_PROF=1 against the current build.", file=sys.stderr)
        return 2

    syms = symbols(binary)
    starts = [s[0] for s in syms]
    owned = owned_set()

    total = 0
    per_sym = {}
    for line in open(prof):
        if line.startswith("#"):
            continue
        p = line.split()
        if len(p) != 2:
            continue
        pc, n = int(p[0], 16), int(p[1])
        total += n
        i = bisect.bisect_right(starts, pc) - 1
        name = None
        if 0 <= i < len(syms):
            lo, hi, nm_ = syms[i]
            # ONLY attribute inside a symbol's real range. A sized symbol that does not contain the pc
            # means the sample is outside this binary's functions entirely — shared libraries (SDL, the
            # Vulkan driver) or the dynamic loader. Falling back to "the nearest preceding symbol"
            # dumped 35% of samples onto `_end`, the linker's end-of-image marker, which read as a
            # single monstrously hot function and is really "everything I cannot see".
            if hi > lo and pc < hi:
                name = nm_
        if name is None:
            name = "(outside this binary: shared libs / loader)"
        per_sym[name] = per_sym.get(name, 0) + n

    if not total:
        sys.exit("prof_hot: the profile has no samples — was the run long enough, and was it CPU-busy?")

    rows = []
    for name, n in per_sym.items():
        g = guest_addr(name) if name else None
        rows.append((n, name, g))
    rows.sort(reverse=True, key=lambda r: r[0])

    print(f"{total} sample(s), {len(per_sym)} symbol(s)\n")
    print(f"{'%':>6}  {'samples':>8}  guest      what")
    shown_guest = 0
    for n, name, g in rows[:a.top]:
        pct = 100.0 * n / total
        if g is None:
            what = f"(non-guest) {name or '?'}"
            guest = "-"
        else:
            shown_guest += n
            tag = f"  <== OWNED as {owned[g]}" if g in owned else ""
            what = (name or "?") + tag
            guest = f"0x{g:08X}"
        print(f"{pct:6.2f}  {n:8d}  {guest:<10} {what}")
    gtot = sum(n for n, _nm, g in rows if g is not None)
    otot = sum(n for n, _nm, g in rows if g is not None and g in owned)
    print(f"\nguest (recompiled) code: {100.0*gtot/total:.1f}% of samples")
    print(f"  of which already owned natively: {100.0*otot/total:.1f}%")
    print(f"non-guest (renderer / CD / libc / runtime): {100.0*(total-gtot)/total:.1f}%")
    print("\nA high non-guest share means guest code is NOT the bottleneck and further native"
          "\nownership buys correctness, not speed — which is a fine goal, but a different one.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
