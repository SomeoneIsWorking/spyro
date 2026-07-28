#!/usr/bin/env python3
"""callsite_args.py — recover the ARGUMENT VALUES at every static call site of a function.

WHY. Spyro's overlays live inside WAD.WAD and are loaded by one loader (0x80016500). An overlay is
keyed BY ITS LOAD ADDRESS — a wrong base emits a whole module of correctly-decoded instructions at
wrong addresses, so every jal target, pointer test and router lookup is then silently wrong. That
makes the load base the one value that must never be guessed.

The slow route is to run the port and watch a load happen. That only ever reveals overlays whose code
path the run actually exercised, and this session has repeatedly shown runtime sampling answering
questions that only the binary can settle. The static route: at a call site the arguments are usually
either immediate (lui/ori/addiu) or loaded from a fixed global (lui/lw) whose contents are IN the
image. Both are readable without running anything.

Validated against ground truth before use: at site 0x80012924 this recovers a1 = [0x800113A0] =
0x8007AA38 — exactly the OVL0 base observed from a running port and already recompiled (claim C031).
An instrument that cannot show the other answer is worthless, so note what it does NOT resolve: it
reports `?` for a register whose value is computed (arithmetic on another unknown, a function return,
a loop-carried value). Those sites genuinely need the observed-load route. A `?` is an honest miss,
not a zero.

Method. For each `jal <target>` in the text, walk BACKWARD up to --window instructions and forward-
simulate that straight-line window with a tiny abstract interpreter over 32 registers holding either
a constant or UNKNOWN. Only the straight-line window is simulated: a branch INTO the middle of it
would invalidate the reasoning, so any incoming branch target inside the window truncates it (the
scan is deliberately conservative — it would rather say `?` than assert a wrong address).

Usage:
  callsite_args.py --exe scratch/bin/spyro/SCUS_942.28 --target 0x80016500
  callsite_args.py --target 0x80016500 --window 60 --args a0,a1,a2,a3
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "external", "psxport", "tools", "recomp"))
import psexe  # noqa: E402
from decode import decode, REG  # noqa: E402

UNK = None  # a register whose value this scan cannot justify


class Regs:
    """32 registers, each a constant or UNKNOWN. r0 is always 0."""

    def __init__(self):
        self.v = [UNK] * 32
        self.v[0] = 0

    def get(self, i):
        return 0 if i == 0 else self.v[i]

    def set(self, i, val):
        if i:
            self.v[i] = val


def simulate(exe, start, end, deref, skip=()):
    """Forward-simulate [start,end) as straight-line code; return the register file at `end`.

    `deref(addr)` reads a word from the static image, or returns None when the address is outside it
    (uninitialised .bss, a heap pointer, a hardware register) — all cases where the static value is
    genuinely unknowable rather than zero.

    `skip` lists addresses to step over. The caller uses it for the CALL BEING ANALYSED: that jal sits
    inside the window (its delay slot is argument setup) but simulating it would apply the caller-saved
    clobber to the very argument registers being reported — which produced an all-`?` first run, the
    classic uniform-output tell."""
    # Straight-line store forwarding. Spyro's call sites routinely materialise an argument by storing
    # a constant to a global and immediately loading it back (site 0x8001253C stores 0x25 to
    # 0x80076B90, then loads a0 from it) — without this the arg that identifies WHICH overlay reads
    # as '?'. Only stores whose ADDRESS and VALUE are both known are recorded; anything else POISONS
    # the entry to UNKNOWN rather than leaving a stale one, and an unknown store address invalidates
    # the whole table (it could have hit any of it).
    mem = {}
    r = Regs()
    pc = start
    while pc < end:
        if pc in skip:
            pc += 4
            continue
        ins = decode(pc, exe.word(pc))
        k, op = ins.kind, ins.op
        if op == "lui":
            r.set(ins.rt, (ins.imm << 16) & 0xFFFFFFFF)
        elif k == "load":
            base = r.get(ins.rs)
            val = UNK
            if base is not None and op == "lw":
                addr = (base + ins.simm) & 0xFFFFFFFF
                val = mem[addr] if addr in mem else deref(addr)
            r.set(ins.rt, val)
        elif k == "alu_rri":
            a = r.get(ins.rs)
            if a is None:
                r.set(ins.rt, UNK)
            elif op in ("addiu", "addi"):
                r.set(ins.rt, (a + ins.simm) & 0xFFFFFFFF)
            elif op == "ori":
                r.set(ins.rt, a | ins.imm)
            elif op == "andi":
                r.set(ins.rt, a & ins.imm)
            elif op == "xori":
                r.set(ins.rt, a ^ ins.imm)
            else:
                r.set(ins.rt, UNK)
        elif k == "alu_rrr":
            a, b = r.get(ins.rs), r.get(ins.rt)
            if a is None or b is None:
                r.set(ins.rd, UNK)
            elif op in ("addu", "add"):
                r.set(ins.rd, (a + b) & 0xFFFFFFFF)
            elif op in ("subu", "sub"):
                r.set(ins.rd, (a - b) & 0xFFFFFFFF)
            elif op == "or":
                r.set(ins.rd, a | b)
            elif op == "and":
                r.set(ins.rd, a & b)
            else:
                r.set(ins.rd, UNK)
        elif k == "shift_i":
            a = r.get(ins.rt)
            if a is None:
                r.set(ins.rd, UNK)
            elif op == "sll":
                r.set(ins.rd, (a << ins.shamt) & 0xFFFFFFFF)
            elif op == "srl":
                r.set(ins.rd, a >> ins.shamt)
            else:
                r.set(ins.rd, UNK)
        elif k == "nop":
            pass
        elif k == "store":
            base = r.get(ins.rs)
            if base is None:
                mem.clear()  # unknown address — it could have overwritten any tracked slot
            elif op == "sw":
                mem[(base + ins.simm) & 0xFFFFFFFF] = r.get(ins.rt)
            else:
                mem.pop((base + ins.simm) & 0xFFFFFFFF, None)  # sb/sh: partial write, no longer known
        elif k in ("jump", "jumpr"):
            # A call clobbers the caller-saved set. v0/v1 become the callee's return value, and a0-a3
            # are argument registers the callee may reuse — none survive as a justified constant.
            if op in ("jal", "jalr"):
                for i in list(range(2, 16)) + [24, 25]:
                    r.set(i, UNK)
        else:
            # branch, cop0, gte, syscall, unknown — conservatively clobber the destination if any
            if k in ("hilo", "gte_move", "cop0"):
                r.set(ins.rd or ins.rt, UNK)
        pc += 4
    return r


def branch_targets(exe, lo, hi):
    """Addresses in [lo,hi) that are the target of a branch/jump from anywhere in the window's
    vicinity. Any such target means control can ENTER mid-window, so the straight-line assumption
    fails from there back."""
    tgts = set()
    scan_lo = max(exe.load, lo - 0x400)
    for pc in range(scan_lo, hi, 4):
        ins = decode(pc, exe.word(pc))
        if ins.kind in ("branch", "jump") and lo <= ins.target < hi:
            tgts.add(ins.target)
    return tgts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="scratch/bin/spyro/SCUS_942.28")
    ap.add_argument("--target", required=True, help="callee address, e.g. 0x80016500")
    ap.add_argument("--window", type=int, default=48, help="instructions to look back")
    ap.add_argument("--args", default="a0,a1,a2,a3")
    a = ap.parse_args()

    exe = psexe.load(a.exe)
    target = int(a.target, 0)
    want = [REG.index(x.strip()) for x in a.args.split(",")]

    def deref(addr):
        try:
            return exe.word(addr)
        except IndexError:
            return UNK

    sites = []
    for pc in range(exe.load, exe.text_end - 4, 4):
        ins = decode(pc, exe.word(pc))
        if ins.kind == "jump" and ins.op == "jal" and ins.target == target:
            sites.append(pc)

    print(f"{a.exe}: {len(sites)} static call site(s) of 0x{target:08X}\n")
    for pc in sites:
        lo = max(exe.load, pc - a.window * 4)
        # The delay slot executes BEFORE the call, so it is part of the argument setup.
        hi = pc + 8
        entered = branch_targets(exe, lo, hi)
        if entered:
            lo = max(lo, max(entered))
        r = simulate(exe, lo, hi, deref, skip={pc})
        parts = []
        for i in want:
            v = r.get(i)
            parts.append(f"{REG[i]}={'?' if v is None else f'0x{v:08X}'}")
        span = (pc + 8 - lo) // 4
        print(f"  0x{pc:08X}  {'  '.join(parts)}   (window {span} insn"
              + (", truncated at branch target" if entered else "") + ")")

    print("\n'?' means the value is COMPUTED, not loaded from a constant — those sites need the\n"
          "observed-load route (run the port with PSXPORT_DEBUG=cd and read the real destination).")


if __name__ == "__main__":
    main()
