#!/usr/bin/env python3
"""callgraph.py — direct-call reachability over the resident text (and optionally an overlay).

WHY. "Does this stage handler ever reach the loader?" keeps coming up, and each time it has been
answered by hand-disassembling and eyeballing `jal`s. That is slow and, worse, it is the kind of
question where missing one edge inverts the answer.

WHAT IT DOES NOT SEE — read this before trusting a negative result. Only DIRECT `jal`/`j` edges are
followed. A call through a function pointer (`jalr`) is invisible, and Spyro leans on those heavily:
the stage dispatcher's mode-13 arm calls [0x800758CC] indirectly, which is exactly the edge that
matters most right now. So an empty path means "no DIRECT path", never "cannot happen". A found path,
on the other hand, is real.

Usage:
  callgraph.py --from 0x80032B08 --to 0x80016500
  callgraph.py --from 0x80032B08 --to 0x80016500 --overlay OVL0   # include OVL0's edges
  callgraph.py --calls-into 0x80016500 --depth 2                  # who reaches a target, by depth
"""
import argparse
import os
import sys
from collections import deque

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "external", "psxport", "tools", "recomp"))
import psexe  # noqa: E402
from decode import decode  # noqa: E402

OVERLAYS = {"OVL0": (0x5B800, 14336, 0x8007AA38)}


def edges(exe, wad=None, overlay=None):
    """addr -> set(direct call targets), keyed by the ENCLOSING function entry.

    Function entries are recovered by splitting on `jr ra` + delay slot, which is how every other tool
    here does it; it is approximate at data/code boundaries but consistent with them."""
    g, cur = {}, exe.load
    words = [(a, exe.word(a)) for a in range(exe.load, exe.text_end - 4, 4)]
    if overlay and wad:
        off, ln, base = OVERLAYS[overlay]
        b = wad[off:off + ln]
        words += [(base + i, int.from_bytes(b[i:i + 4], "little")) for i in range(0, len(b) - 3, 4)]
        cur = exe.load
    prev_jr = False
    for addr, w in words:
        if prev_jr:
            cur = addr + 4      # the instruction after the delay slot starts the next function
            prev_jr = False
        i = decode(addr, w)
        if i.kind == "jump":
            g.setdefault(cur, set()).add(i.target)
        if w == 0x03E00008:
            prev_jr = True
    return g


def entry_of(exe, a):
    p = a
    while p > exe.load:
        p -= 4
        if exe.word(p) == 0x03E00008:
            return p + 8
    return exe.load


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="scratch/bin/spyro/SCUS_942.28")
    ap.add_argument("--wad", default="scratch/wad/WAD.WAD")
    ap.add_argument("--overlay", help="also include this overlay's edges (e.g. OVL0)")
    ap.add_argument("--from", dest="src")
    ap.add_argument("--to", dest="dst")
    ap.add_argument("--calls-into")
    ap.add_argument("--depth", type=int, default=3)
    a = ap.parse_args()

    exe = psexe.load(a.exe)
    wad = open(a.wad, "rb").read() if (a.overlay and os.path.isfile(a.wad)) else None
    g = edges(exe, wad, a.overlay)

    if a.calls_into:
        t = int(a.calls_into, 0)
        rev = {}
        for s, ts in g.items():
            for x in ts:
                rev.setdefault(x, set()).add(s)
        seen, frontier = {t}, {t}
        for d in range(1, a.depth + 1):
            nxt = set()
            for x in frontier:
                nxt |= rev.get(x, set())
            nxt -= seen
            if not nxt:
                break
            print(f"depth {d}: {len(nxt)} caller(s)")
            for x in sorted(nxt):
                print(f"   0x{x:08X}")
            seen |= nxt
            frontier = nxt
        return

    src, dst = int(a.src, 0), int(a.dst, 0)
    src = src if src in g else entry_of(exe, src)
    q, seen, par = deque([src]), {src}, {}
    while q:
        cur = q.popleft()
        if cur == dst:
            path, x = [], dst
            while x != src:
                path.append(x)
                x = par[x]
            path.append(src)
            print("DIRECT path found:")
            for x in reversed(path):
                print(f"   0x{x:08X}")
            return
        for t in g.get(cur, ()):
            if t not in seen:
                seen.add(t)
                par[t] = cur
                q.append(t)
    print(f"NO DIRECT path 0x{src:08X} -> 0x{dst:08X} ({len(seen)} functions explored).")
    print("Remember this tool cannot see jalr/function-pointer edges — a negative is not a proof.")


if __name__ == "__main__":
    main()
