#!/usr/bin/env python3
"""wad_index.py — enumerate WAD.WAD's entries and score each for whether it holds CODE.

WHY. Spyro's overlays are not files on the disc; they live inside WAD.WAD and are loaded into the
heap (docs/issues/0001). One was found the slow way — by watching a running port make a load, then
verifying the bytes decoded as MIPS. The public decomps describe ~37 overlays, so finding the rest
one observed load at a time would be both slow and incomplete (a code path never exercised never
reveals its overlay).

The archive's first sector is an INDEX: a flat array of (byte offset, length) pairs, all
sector-aligned. Confirmed against ground truth — entry 2 is (0x5B800, 0x3800), exactly the overlay
already located and recompiled, and every load the running port made corresponds to an entry.

So the whole set can be enumerated statically. Which entries are CODE is then scored by the SHARE of
words whose opcode is one that dominates real MIPS (see CODE_OPS). Do NOT score on "did the word
decode": the decoder names unrecognised encodings op:0x2F / regimm:0x04 rather than failing, so that
test returns ~100% for arbitrary data — the first version of this tool did exactly that and scored
every entry 100%, which is the classic uniform-output tell of a broken instrument. The share metric
discriminates: the already-verified overlay scores 99.5% while neighbouring entries score 64% and 88%.
The metric is base-independent, so no load address is needed to classify — only to recompile.

Usage:
  wad_index.py [--wad PATH] [--min-score 90] [--max-entries N]

Output: one line per entry — index, offset, length, code-opcode share, and a CODE marker. Entries
flagged CODE are candidates for extraction into tools/ensure_recomp.py's OVERLAYS list; each still
needs its LOAD BASE established before it can be recompiled, because a wrong base emits a whole
module at wrong addresses.
"""
import argparse
import os
import struct
import sys
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "external", "psxport", "tools", "recomp"))
from decode import decode  # noqa: E402

SECTOR = 0x800


# Opcodes that dominate real MIPS code. Scoring on "did it decode" is USELESS: the decoder names
# unrecognised encodings op:0x2F / regimm:0x04 rather than reporting failure, so a naive validity
# test returns ~100% for arbitrary DATA. (First version of this tool did exactly that and scored
# every entry 100% — uniform output being the classic tell of a broken instrument.) Score instead on
# the SHARE of words that are common code opcodes: real code is dominated by them, data is not.
CODE_OPS = {
    "nop", "sll", "srl", "sra", "addiu", "addu", "addi", "subu", "and", "andi", "or", "ori",
    "xor", "xori", "lui", "lw", "lh", "lhu", "lb", "lbu", "sw", "sh", "sb", "jal", "jr", "jalr",
    "j", "beq", "bne", "blez", "bgtz", "slt", "sltu", "slti", "sltiu", "mult", "multu", "div",
    "divu", "mfhi", "mflo", "move",
}


def score_code(buf, base=0x80000000):
    """Percentage of words whose opcode is one that dominates real MIPS code, plus the histogram.

    Base only affects branch/jump TARGETS, not decoding, so classification needs no load address."""
    n = len(buf) // 4
    if n == 0:
        return 0.0, Counter()
    hist = Counter()
    for i in range(n):
        w = int.from_bytes(buf[i * 4:i * 4 + 4], "little")
        hist[decode(base + i * 4, w).op] += 1
    good = sum(v for k, v in hist.items() if k in CODE_OPS)
    return 100.0 * good / n, hist


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wad", default="scratch/wad/WAD.WAD")
    ap.add_argument("--min-score", type=float, default=90.0)
    ap.add_argument("--max-entries", type=int, default=0, help="0 = all")
    a = ap.parse_args()

    if not os.path.isfile(a.wad):
        sys.exit(f"wad_index: {a.wad} not found (ensure_recomp.py extracts it)")
    size = os.path.getsize(a.wad)
    with open(a.wad, "rb") as f:
        head = f.read(SECTOR)
        words = struct.unpack(f"<{SECTOR // 4}I", head)

        # Pairs of (offset, length). Stop at the first entry that cannot be one: a zero/misaligned
        # offset, or a span running past the archive. Trailing table slots are zero-filled.
        entries = []
        for i in range(0, len(words) - 1, 2):
            off, ln = words[i], words[i + 1]
            if off == 0 or ln == 0:
                break
            if off % SECTOR or off + ln > size:
                break
            entries.append((i // 2, off, ln))
        if a.max_entries:
            entries = entries[:a.max_entries]

        print(f"{a.wad}: {size} bytes, {len(entries)} index entries\n")
        print(f"{'#':>4} {'offset':>10} {'length':>9} {'valid':>7}  top opcodes")
        code = []
        for idx, off, ln in entries:
            f.seek(off)
            # Score a bounded prefix: enough to classify, cheap enough to scan the whole table.
            buf = f.read(min(ln, 8192))
            pct, hist = score_code(buf)
            top = ", ".join(f"{k}={v}" for k, v in hist.most_common(4))
            mark = "  <== CODE" if pct >= a.min_score else ""
            print(f"{idx:>4} 0x{off:08X} {ln:>9} {pct:>6.1f}%  {top}{mark}")
            if pct >= a.min_score:
                code.append((idx, off, ln))

        print(f"\n{len(code)} entr(ies) score >= {a.min_score}% valid opcodes:")
        for idx, off, ln in code:
            print(f"  entry {idx}: WAD +0x{off:X}, {ln} bytes"
                  + ("   <-- already recompiled as OVL0" if off == 0x5B800 else ""))
        print("\nEach still needs its LOAD BASE established before recompiling — a wrong base emits a\n"
              "whole module at wrong addresses. Find it the way OVL0's was: a running port's loader\n"
              "call whose offset matches, or a hardcoded jal that lands inside the loaded span.")


if __name__ == "__main__":
    main()
