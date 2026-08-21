#!/usr/bin/env python3
"""computed_jumps.py — find COMPUTED-OFFSET jumps (`jr base+idx*stride`) and enumerate their case labels.

WHY. psxport's recompiler recovers C `switch` jump tables, but only the TABLE idiom: the target address
is read out of a table with `lw rN,OFF(base)`. Spyro also uses a different shape —

    lui/addiu  rB, <immediate base>      ; the base is an IMMEDIATE, there is no table
    sll        rI, rI, k                 ; index scaled to a byte offset
    add        rD, rB, rI
    jr         rD                        ; target = base + idx*2^k

— a jump INTO AN UNROLLED RUN of fixed-size blocks (GTE code here). The recompiler cannot see it, so the
jump routes to rec_dispatch and fail-fasts at runtime, one case at a time. Finding them by running the
port and reading each new [recomp-MISS] costs a full build+run per case; this enumerates them statically.

The case COUNT is the hard part and this tool does NOT invent one. It reports the base and stride, and
lists candidate case addresses only as far as a STOP heuristic: blocks are emitted until one does not
look like a case body (see looks_like_case). Treat the output as candidates to review, not as a seed
list to paste — a wrong case address seeded as a function carves up real code, which is the corruption
game/recomp_seeds.json exists to prevent.

Usage:
  computed_jumps.py [--exe PATH] [--lo 0x8004A000] [--hi 0x8004D000] [--max-cases 32]
"""

import argparse
import os
import sys

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..",
        "external",
        "psxport",
        "tools",
        "recomp",
    ),
)
import psexe
from decode import decode


def looks_like_case(exe, a):
    """Does `a` start something that could be one block of an unrolled run?

    Deliberately loose — it only has to reject obvious non-code — because the real bound is the guard the
    compiler emitted, which this tool does not recover. A block that decodes and ends in a transfer
    (j/jr/branch) within a few instructions is case-shaped."""
    try:
        for k in range(6):
            i = decode(a + k * 4, exe.word(a + k * 4))
            if i.kind == "unknown":
                return False
            if i.kind in ("jump", "jumpr", "branch"):
                return True
        return True
    except IndexError:
        return False


def scan(exe, lo, hi, max_cases):
    """Find `jr rD` where rD = <immediate base> + (idx << k)."""
    found = []
    for pc in range(lo, hi - 4, 4):
        i = decode(pc, exe.word(pc))
        if i.op != "jr" or i.rs == 31:  # jr ra is a return, not a dispatch
            continue
        d = i.rs
        # walk back a short window for `add d, B, I` then the lui/addiu that set B and the sll that set I
        base = shift = None
        for back in range(1, 24):
            a = pc - back * 4
            if a < exe.load:
                break
            p = decode(a, exe.word(a))
            if p.kind == "alu_rrr" and p.op in ("add", "addu") and p.rd == d:
                srcs = (p.rs, p.rt)
                hi_val = {}
                for b2 in range(1, 40):
                    a2 = a - b2 * 4
                    if a2 < exe.load:
                        break
                    q = decode(a2, exe.word(a2))
                    if q.op == "lui":
                        hi_val[q.rt] = q.imm << 16
                    elif q.op == "addiu" and q.rs in hi_val and q.rt in srcs:
                        base = (hi_val[q.rs] + q.simm) & 0xFFFFFFFF
                    elif q.op == "sll" and q.rd in srcs:
                        shift = q.shamt
                    if base is not None and shift is not None:
                        break
                break
        if base is None or shift is None:
            continue
        if not (exe.load <= base < exe.text_end):
            continue
        stride = 1 << shift
        cases = []
        a = base
        while len(cases) < max_cases and looks_like_case(exe, a) and a < exe.text_end:
            cases.append(a)
            a += stride
        found.append((pc, base, stride, cases))
    return found


def seeding_guidance():
    return (
        "REVIEW, DO NOT SEED THESE RAW CANDIDATES. Computed-offset case addresses are labels "
        "inside an existing function and belong in the recompiler's recognizer, not in per-game "
        "seeds. For a separately measured true mid-function re-entry, use `main_reentry` only: "
        "the current emitter treats it as both the resident discovery root and the fallthrough "
        "boundary. Do not duplicate that address in `main`."
    )


def guidance_errors(guidance):
    rules = (
        (
            "computed cases are recognizer-owned",
            "belong in the recompiler's recognizer" in guidance,
        ),
        ("main_reentry is authoritative", "use `main_reentry` only" in guidance),
        ("main_reentry creates discovery", "resident discovery root" in guidance),
        (
            "duplicate main authority refused",
            "BOTH `main` and `main_reentry`" not in guidance,
        ),
    )
    return [name for name, passed in rules if not passed]


def selftest():
    current_errors = guidance_errors(seeding_guidance())
    stale_errors = guidance_errors(
        "A mid-function entry is seeded in BOTH `main` and `main_reentry`."
    )
    checks = (
        ("shipping guidance accepted", not current_errors),
        ("stale duplicate guidance rejected", bool(stale_errors)),
        (
            "negative control names duplicate authority",
            "duplicate main authority refused" in stale_errors,
        ),
        (
            "negative control names missing discovery root",
            "main_reentry creates discovery" in stale_errors,
        ),
    )
    for name, passed in checks:
        print(f"[computed-jumps:selftest] {'PASS' if passed else 'FAIL'}  {name}")
    failures = sum(not passed for _, passed in checks)
    print(f"[computed-jumps:selftest] {len(checks) - failures}/{len(checks)} passed")
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="scratch/bin/spyro/SCUS_942.28")
    ap.add_argument("--lo", default="0x80010000")
    ap.add_argument("--hi", default="0x80075800")
    ap.add_argument("--max-cases", type=int, default=32)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    exe = psexe.load(a.exe)
    hits = scan(exe, int(a.lo, 0), min(int(a.hi, 0), exe.text_end), a.max_cases)
    print(f"{len(hits)} computed-offset jump(s) in [{a.lo},{a.hi})\n")
    allc = []
    for pc, base, stride, cases in hits:
        print(
            f"  jr @0x{pc:08X}  base=0x{base:08X}  stride={stride}  {len(cases)} candidate case(s)"
        )
        print(
            "     "
            + " ".join(f"0x{c:08X}" for c in cases[:12])
            + (" ..." if len(cases) > 12 else "")
        )
        allc += cases
    print(f"\n{len(set(allc))} distinct candidate case addresses.")
    print(
        "The case COUNT is not recovered here — the stop is a heuristic, so the tail of each run "
        "may be over-reported."
    )
    print(seeding_guidance())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
