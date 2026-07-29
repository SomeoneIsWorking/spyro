#!/usr/bin/env python3
"""writers.py — which code stores to this guest address, and what value does it store?

THE COMPANION TO xrefs.py: that one answers "who branches HERE", this one answers "who writes THAT".

WHY IT PRINTS THE STORED VALUE. "Find the writer" is almost never the real question. The real question
is "which writer produces the value the gate wants", and with a global that has nineteen writers the
list alone is useless. Issue 0027 turned on exactly this: the title-screen exit gate needs
[0x80078D7C] == 5, and of the nineteen sites that store to it, exactly ONE stores 5. Finding that site
took seconds once the immediately-preceding `addiu rt, zero, N` was printed alongside each store; the
bare address list would have meant reading nineteen call sites by hand.

KNOWN BLIND SPOT, and it is a big one — SAY SO RATHER THAN TRUSTING A CLEAN RESULT. This finds only
stores whose address is formed by a `lui`/immediate pair. A store through a computed pointer
(`sw rt, 0(rs)` where rs was loaded from somewhere) is INVISIBLE here. That is not hypothetical: two
separate static scans in this project missed the writer of the stage sub-state for precisely that
reason, and it took a write-watchpoint to find it in one run. So:

    an empty or short result means "no IMMEDIATE-FORM writer", never "nothing writes this".

Reach for PSXPORT_WWATCH=<lo>,<hi> (plus PSXPORT_WWATCH_BT=1) when this comes up empty, or when you
need to know which writer actually EXECUTES rather than which ones exist — those are different
questions, and this tool only answers the second. In issue 0027 nineteen writers existed and a
watchpoint showed that none of them ran.

Usage:
  writers.py 0x80078D7C                 # scan the main executable and every overlay image
  writers.py 0x80078D7C --value 5       # only sites whose stored value is a visible constant 5
"""
import argparse
import glob
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(REPO, "scratch", "bin", "spyro", "SCUS_942.28")
OVDIR = os.path.join(REPO, "scratch", "bin", "overlays")
ARENA_BASE = 0x8007AA38          # every Spyro overlay loads here — see CLAUDE.md / the router

REG = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
       "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]
STORE = {0x28: "sb", 0x29: "sh", 0x2B: "sw"}


def simm(w):
    v = w & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def const_into(data, off, reg, back=6):
    """If `reg` was loaded with a small constant just before this store, return it.

    This is what makes the output answerable rather than merely enumerative: the value a store writes
    is nearly always set by an `addiu rt, zero, N` a few instructions earlier."""
    for k in range(1, back + 1):
        o = off - 4 * k
        if o < 0:
            break
        w = struct.unpack_from("<I", data, o)[0]
        if (w >> 26) == 0x09 and ((w >> 16) & 31) == reg and ((w >> 21) & 31) == 0:   # addiu rt, zero, N
            return simm(w)
        if (w >> 26) == 0x0D and ((w >> 16) & 31) == reg and ((w >> 21) & 31) == 0:   # ori rt, zero, N
            return w & 0xFFFF
    return None


def scan(data, base, target):
    hits = []
    for off in range(0, len(data) - 3, 4):
        w = struct.unpack_from("<I", data, off)[0]
        op = w >> 26
        if op not in STORE:
            continue
        rs, rt = (w >> 21) & 31, (w >> 16) & 31
        # Walk back for the `lui rs, hi` that forms this store's base register.
        for k in range(1, 9):
            o = off - 4 * k
            if o < 0:
                break
            w2 = struct.unpack_from("<I", data, o)[0]
            if (w2 >> 26) == 0x0F and ((w2 >> 16) & 31) == rs:          # lui rs, imm
                if (((w2 & 0xFFFF) << 16) + simm(w)) & 0xFFFFFFFF == target:
                    hits.append((base + off, STORE[op], rt, const_into(data, off, rt)))
                break
    return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target")
    ap.add_argument("--value", type=lambda s: int(s, 0), help="only report stores of this constant")
    a = ap.parse_args()
    target = int(a.target, 16)

    images = []
    exe = open(EXE, "rb").read()
    images.append(("MAIN", exe[0x800:], struct.unpack_from("<I", exe, 0x18)[0]))
    for f in sorted(glob.glob(os.path.join(OVDIR, "OV_*.BIN"))):
        images.append((os.path.basename(f), open(f, "rb").read(), ARENA_BASE))

    total = 0
    for tag, data, base in images:
        for pc, mn, rt, val in scan(data, base, target):
            if a.value is not None and val != a.value:
                continue
            v = f"= {val}" if val is not None else "= (computed)"
            print(f"  {tag:16s} 0x{pc:08X}  {mn} ${REG[rt]}   {v}")
            total += 1
    sel = f" storing {a.value}" if a.value is not None else ""
    print(f"\n{total} immediate-form store(s) to 0x{target:08X}{sel}")
    print("\nThis lists writers that EXIST, not writers that RUN, and it cannot see stores through a\n"
          "computed pointer. Use PSXPORT_WWATCH=<lo>,<hi> PSXPORT_WWATCH_BT=1 for either question —\n"
          "an empty result here means 'no immediate-form writer', never 'nothing writes this'.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
