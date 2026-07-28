#!/usr/bin/env python3
"""own_candidates.py — rank guest functions by how safe and worthwhile they are to OWN natively.

WHY. This port is 20 observation wrappers to 2 native bodies (C075): almost nothing is actually
reimplemented. Converting that is the work, and the bottleneck is choosing what to convert. Picking by
eye already went wrong once — 0x8001ED5C was filed as "small, exactly specified" because the first
dozen instructions are a buffer flip, when the function continues into the whole per-frame stage
dispatcher. Owning it would have meant owning the frame loop by accident.

WHAT MAKES A GOOD TARGET, and why each column is here:

  LEAF (no jal/jalr)   A function that calls nothing is self-contained: its entire effect is registers
                       plus memory, which is exactly what the per-call differential compares. A
                       non-leaf drags its callees' behaviour into the replacement, so a native body
                       has to reproduce them too — that is not a first target, it is a project.
  SIZE                 Instructions to the `jr ra`. Small enough to transcribe exactly, and the whole
                       point is exactness: the differential compares every GPR, so "roughly right" is
                       a fail.
  CALLERS              Static call sites. This is the VALUE axis — owning a function nothing calls
                       buys nothing. It is a lower bound: indirect calls are invisible here, so a low
                       count is not proof of coldness (tools/callgraph.py carries the same caveat).
  MEM                  Distinct guest addresses it loads/stores. High memory traffic means more state
                       to reproduce and more ways to be subtly wrong.

The ranking deliberately favours LEAF + many CALLERS + small SIZE. It is a queue to review, not a
work order: read the body before transcribing it, and let the differential decide whether you got it
right — reading is how the $at clobber in the first native function got missed (I019).

Usage:
  own_candidates.py                 # top 20 leaf candidates by caller count
  own_candidates.py --all --top 40  # include non-leaves, marked
  own_candidates.py --addr 0x8006272C   # explain one function's numbers
"""
import argparse
import os
import sys
from collections import Counter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "external", "psxport", "tools", "recomp"))
import psexe                                    # noqa: E402
from decode import decode                       # noqa: E402

EXE = "scratch/bin/spyro/SCUS_942.28"
JR_RA = 0x03E00008


def functions(exe):
    """Split text on `jr ra` + delay slot — the same boundary rule every other tool here uses.

    Approximate at data/code boundaries, and it says so rather than pretending otherwise: a function
    whose body contains a mid-body `jr ra` is split, and one followed by data runs long. Both show up
    as an implausible SIZE, which is why SIZE is printed rather than hidden."""
    out, start = [], exe.load
    a = exe.load
    while a < exe.text_end - 4:
        if exe.word(a) == JR_RA:
            out.append((start, a + 8))
            start = a + 8
            a += 8
            continue
        a += 4
    return out


def analyse(exe, lo, hi):
    n = (hi - lo) // 4
    leaf = True
    mem = set()
    hi_regs = {}
    for a in range(lo, hi, 4):
        i = decode(a, exe.word(a))
        if i.op in ("jal",) or i.op == "jalr":
            leaf = False
        if i.op == "lui":
            hi_regs[i.rt] = i.imm << 16
        elif getattr(i, "rs", None) in hi_regs and hasattr(i, "simm") and i.kind in ("load", "store"):
            mem.add((hi_regs[i.rs] + i.simm) & 0xFFFFFFFF)
    return n, leaf, len(mem)


def call_counts(exe):
    c = Counter()
    for a in range(exe.load, exe.text_end - 4, 4):
        i = decode(a, exe.word(a))
        if i.op == "jal":
            c[i.target] += 1
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--all", action="store_true", help="include non-leaf functions")
    ap.add_argument("--maxsize", type=int, default=60, help="skip functions longer than this")
    ap.add_argument("--addr", help="explain one function instead of ranking")
    a = ap.parse_args()

    exe = psexe.load(os.path.join(REPO, EXE))
    calls = call_counts(exe)
    funcs = functions(exe)

    if a.addr:
        want = int(a.addr, 0)
        for lo, hi in funcs:
            if lo <= want < hi:
                n, leaf, mem = analyse(exe, lo, hi)
                print(f"0x{lo:08X}..0x{hi:08X}  size={n} instr  leaf={'yes' if leaf else 'NO'}  "
                      f"distinct mem addrs={mem}  static callers={calls.get(lo,0)}")
                if not leaf:
                    print("  NOT A LEAF — it calls other functions, so a native body must reproduce")
                    print("  their behaviour too. Own those first, or pick something else.")
                return 0
        print("no function contains that address (boundaries are jr-ra based; it may be data)")
        return 1

    rows = []
    for lo, hi in funcs:
        n, leaf, mem = analyse(exe, lo, hi)
        if n > a.maxsize or n < 3:
            continue
        if not leaf and not a.all:
            continue
        rows.append((calls.get(lo, 0), -n, lo, n, leaf, mem))
    rows.sort(reverse=True)

    print(f"{'addr':<12}{'callers':>8}{'size':>6}{'leaf':>6}{'mem':>5}   "
          f"(callers is a LOWER BOUND — indirect calls are invisible)")
    for callers, _, lo, n, leaf, mem in rows[:a.top]:
        print(f"0x{lo:08X}{callers:>8}{n:>6}{'yes' if leaf else 'NO':>6}{mem:>5}")
    print(f"\n{len(rows)} candidate(s) at size<={a.maxsize}"
          f"{'' if a.all else ' (leaf only; pass --all to include non-leaves)'}.")
    print("Review the body before transcribing — and let PSXPORT_NDIFF decide whether it matched.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
