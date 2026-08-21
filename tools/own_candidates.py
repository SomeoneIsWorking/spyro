#!/usr/bin/env python3
"""own_candidates.py — rank guest functions by how safe and worthwhile they are to OWN natively.

WHY. Native ownership is growing bottom-up, and the bottleneck is choosing the next body without
guessing. Picking by eye already went wrong once — 0x8001ED5C was filed as "small, exactly specified"
because the first dozen instructions are a buffer flip, when the function continues into the whole
per-frame stage dispatcher. Owning it would have meant owning the frame loop by accident.

WHAT MAKES A GOOD TARGET, and why each column is here:

  LEAF (no jal/jalr)   A function that calls nothing is self-contained: its entire effect is registers
                       plus memory, which is exactly what the per-call differential compares.
  READY NON-LEAF       A non-leaf is eligible only when it has no indirect call and every direct
                       callee is already owned, so the port grows bottom-up rather than importing an
                       unverified child graph.
  SIZE                 Instructions to the `jr ra`. Small enough to transcribe exactly, and the whole
                       point is exactness: the differential compares every GPR, so "roughly right" is
                       a fail.
  CALLERS              Static call sites. This is the VALUE axis — owning a function nothing calls
                       buys nothing. It is a lower bound: indirect calls are invisible here, so a low
                       count is not proof of coldness (tools/callgraph.py carries the same caveat).
  MEM                  Distinct guest addresses it loads/stores. High memory traffic means more state
                       to reproduce and more ways to be subtly wrong.

The default ranking deliberately favours LEAF + many CALLERS + small SIZE; `--ready-nonleaf` selects
the dependency-valid next phase. Either output is a queue to review, not a work order: prove dynamic
reach, read the body, and let the differential decide whether the implementation matched.

Usage:
  own_candidates.py                 # top 20 leaf candidates by caller count
  own_candidates.py --all --top 40  # include non-leaves, marked
  own_candidates.py --ready-nonleaf # non-leaves whose direct callees are all already owned
  own_candidates.py --addr 0x8006272C   # explain one function's numbers
"""

import argparse
import os
import re
import sys
from collections import Counter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "external", "psxport", "tools", "recomp"))
import psexe  # noqa: E402
from decode import decode  # noqa: E402

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
        elif (
            getattr(i, "rs", None) in hi_regs
            and hasattr(i, "simm")
            and i.kind in ("load", "store")
        ):
            mem.add((hi_regs[i.rs] + i.simm) & 0xFFFFFFFF)
    return n, leaf, len(mem)


def dependencies(exe, lo, hi):
    """Return direct callees and whether the body contains an unresolved indirect call."""
    callees = set()
    has_jalr = False
    for pc in range(lo, hi, 4):
        insn = decode(pc, exe.word(pc))
        if insn.op == "jal":
            callees.add(insn.target)
        elif insn.op == "jalr":
            has_jalr = True
    return callees, has_jalr


def already_owned():
    """Addresses this port ALREADY owns, read from the ndiff_run sites in game/core/.

    Without this the ranking keeps recommending functions that are done — it listed all seven owned
    bodies at the top of its own queue, which would waste a pick every iteration. Derived from the
    source rather than a hand-kept list, so it cannot drift out of date."""
    owned = set()
    d = os.path.join(REPO, "game", "core")
    for fn in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if not fn.endswith(".cpp"):
            continue
        with open(os.path.join(d, fn), encoding="utf-8") as source:
            text = source.read()
        for m in re.finditer(r"ndiff_run\(c,\s*\"[^\"]*@0x([0-9A-Fa-f]+)\"", text):
            owned.add(int(m.group(1), 16))
    return owned


def compiled_entries():
    """MAIN entries the generated dispatcher can actually override.

    The jr-ra boundary scan is intentionally approximate and can split a body at a mid-function
    return. Such an address may look dependency-ready but FNTRACE and shard_set_override both refuse
    it. Intersecting with the generated declaration inventory keeps the ownership queue actionable.
    """
    path = os.path.join(REPO, "generated", "rec_decls.h")
    with open(path, encoding="utf-8") as source:
        text = source.read()
    return {
        int(m.group(1), 16) for m in re.finditer(r"\bgen_func_([0-9A-Fa-f]{8})\b", text)
    }


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
    ap.add_argument(
        "--ready-nonleaf",
        action="store_true",
        help="show only non-leaves with no jalr and only already-owned direct callees",
    )
    ap.add_argument(
        "--maxsize", type=int, default=60, help="skip functions longer than this"
    )
    ap.add_argument("--addr", help="explain one function instead of ranking")
    a = ap.parse_args()

    exe = psexe.load(os.path.join(REPO, EXE))
    calls = call_counts(exe)
    funcs = functions(exe)
    owned = already_owned()
    entries = compiled_entries()

    if a.addr:
        want = int(a.addr, 0)
        for lo, hi in funcs:
            if lo <= want < hi:
                n, leaf, mem = analyse(exe, lo, hi)
                callees, has_jalr = dependencies(exe, lo, hi)
                print(
                    f"0x{lo:08X}..0x{hi:08X}  size={n} instr  leaf={'yes' if leaf else 'NO'}  "
                    f"distinct mem addrs={mem}  static callers={calls.get(lo, 0)}"
                )
                if not leaf:
                    missing_children = callees - owned
                    child_list = (
                        ", ".join(f"0x{x:08X}" for x in sorted(callees)) or "none"
                    )
                    print("  direct callees: " + child_list)
                    print(
                        f"  owned callees: {len(callees & owned)}/{len(callees)}"
                        f"  indirect call: {'yes' if has_jalr else 'no'}"
                    )
                    if missing_children:
                        print(
                            "  not owned: "
                            + ", ".join(f"0x{x:08X}" for x in sorted(missing_children))
                        )
                    elif has_jalr:
                        print(
                            "  NOT DEPENDENCY-READY — jalr target cannot be proven owned statically."
                        )
                    else:
                        print(
                            "  dependency-ready — all direct callees are already owned."
                        )
                if lo not in entries:
                    print(
                        "  NOT OVERRIDABLE — this approximate jr-ra boundary is not a generated "
                        "MAIN entry."
                    )
                return 0
        print(
            "no function contains that address (boundaries are jr-ra based; it may be data)"
        )
        return 1

    rows = []
    for lo, hi in funcs:
        n, leaf, mem = analyse(exe, lo, hi)
        if n > a.maxsize or n < 3:
            continue
        if not leaf and not (a.all or a.ready_nonleaf):
            continue
        callees, has_jalr = dependencies(exe, lo, hi)
        if a.ready_nonleaf and (
            lo not in entries
            or leaf
            or has_jalr
            or not callees
            or not callees.issubset(owned)
        ):
            continue
        rows.append((calls.get(lo, 0), -n, lo, n, leaf, mem, lo in owned, callees))
    rows.sort(reverse=True)

    todo = [r for r in rows if not r[6]]
    print(
        f"{'addr':<12}{'callers':>8}{'size':>6}{'leaf':>6}{'mem':>5}   "
        f"(callers is a LOWER BOUND — indirect calls are invisible)"
    )
    for callers, _, lo, n, leaf, mem, _own, callees in todo[: a.top]:
        children = ""
        if a.ready_nonleaf:
            children = "   children=" + ",".join(f"0x{x:08X}" for x in sorted(callees))
        print(
            f"0x{lo:08X}{callers:>8}{n:>6}{'yes' if leaf else 'NO':>6}{mem:>5}{children}"
        )
    print(
        f"\n{len(owned)} already owned (all sizes — derived from ndiff_run sites in game/core/)"
    )
    if a.ready_nonleaf:
        qualifier = " (dependency-ready non-leaves only)"
    elif a.all:
        qualifier = ""
    else:
        qualifier = " (leaf only; pass --all to include non-leaves)"
    print(f"{len(todo)} candidate(s) remaining at size<={a.maxsize}{qualifier}.")
    print(
        "Review the body before transcribing — and let PSXPORT_NDIFF decide whether it matched."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
