#!/usr/bin/env python3
"""wad_index.py — enumerate WAD.WAD's entries and score each for whether it holds CODE.

WHY. Spyro's overlays are not files on the disc; they live inside WAD.WAD and are loaded into the
heap (docs/issues/0001). One was found the slow way — by watching a running port make a load, then
verifying the bytes decoded as MIPS. The public decomps describe ~37 overlays, so finding the rest
one observed load at a time would be both slow and incomplete (a code path never exercised never
reveals its overlay).

The archive's first sector is an INDEX: a flat array of (byte offset, length) pairs, all
sector-aligned. Confirmed against ground truth — entry 2 is (0x5B800, 0x3800), exactly the overlay
already located in runtime evidence, and every load the running port made corresponds to an entry.

So the whole set can be enumerated statically. Which entries are CODE is then scored by the SHARE of
words whose opcode is one that dominates real MIPS (see CODE_OPS). Do NOT score on "did the word
decode": the decoder names unrecognised encodings op:0x2F / regimm:0x04 rather than failing, so that
test returns ~100% for arbitrary data — the first version of this tool did exactly that and scored
every entry 100%, which is the classic uniform-output tell of a broken instrument. The share metric
discriminates: the already-verified overlay scores 99.5% while neighbouring entries score 64% and 88%.
The metric is base-independent, so no load address is needed to classify. Runtime activation still
needs the exact load base to assign image identity and invalidation ranges.

Usage:
  wad_index.py [--wad PATH] [--min-score 90] [--max-entries N]

Output: one line per entry — index, offset, length, code-opcode share, and a CODE marker. Entries
flagged CODE are runtime-image candidates; each still needs its load base established so the JIT
cache, native override keys, and invalidation owner refer to the correct resident image.
"""
import argparse
import os
import struct
import sys
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "external", "psxport", "tools", "mips"))
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


def structure(buf):
    """Function prologues / `jr ra` epilogues / jal count — a STRUCTURAL check on the opcode-share score.

    The share metric can only say "these words look like common opcodes". It cannot distinguish code
    from data that happens to be opcode-shaped, and the CODE-flagged level entries have a different
    opcode profile from the one verified overlay (`lb`-topped rather than `nop`/`lui`-topped), which is
    reason enough not to trust the share alone. Real code has function boundaries; data does not.
    Verified OVL0 scores 4 prologues / 5 `jr ra`, while data entry 0 scores 1 / 0."""
    pro = jr = jal = 0
    for i in range(0, len(buf) - 3, 4):
        w = int.from_bytes(buf[i:i + 4], "little")
        # addiu sp, sp, -N  — the stack-frame prologue
        if (w >> 26) == 0x09 and ((w >> 21) & 31) == 29 and ((w >> 16) & 31) == 29 and (w & 0x8000):
            pro += 1
        if w == 0x03E00008:          # jr ra
            jr += 1
        if (w >> 26) == 3:           # jal
            jal += 1
    return pro, jr, jal


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wad", default="scratch/wad/WAD.WAD")
    ap.add_argument("--min-score", type=float, default=90.0)
    ap.add_argument("--max-entries", type=int, default=0, help="0 = all")
    a = ap.parse_args()

    if not os.path.isfile(a.wad):
        sys.exit(f"wad_index: {a.wad} not found (provision the authenticated title archive first)")
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
        print(f"{'#':>4} {'offset':>10} {'length':>9} {'valid':>7} {'pro':>4}{'ret':>4}{'jal':>6}")
        code = []
        for idx, off, ln in entries:
            f.seek(off)
            # Score a bounded prefix: enough to classify, cheap enough to scan the whole table.
            buf = f.read(min(ln, 8192))
            pct, hist = score_code(buf)
            # Structure needs the WHOLE entry, not the prefix. A module's first function can be longer
            # than 8KB, so an 8KB window found no prologue/epilogue in 20 of the 36 real code entries
            # and would have mislabelled them "share only". The extra read costs one pass over the
            # archive and buys a signal that actually agrees with the share metric on every entry.
            f.seek(off)
            pro, jr, jal = structure(f.read(ln))
            # Both signals must agree. The share says "opcode-shaped"; the structure says "has function
            # boundaries". Either alone has a plausible false positive; together they have not disagreed
            # on any entry so far, which is what makes the CODE flag worth acting on.
            is_code = pct >= a.min_score and pro > 0 and jr > 0
            mark = "  <== CODE" if is_code else ("  (share only)" if pct >= a.min_score else "")
            print(f"{idx:>4} 0x{off:08X} {ln:>9} {pct:>6.1f}% {pro:>4}p{jr:>4}r{jal:>6}j{mark}")
            if is_code:
                code.append((idx, off, ln))

        print(f"\n{len(code)} entr(ies) score >= {a.min_score}% valid opcodes:")
        for idx, off, ln in code:
            print(f"  entry {idx}: WAD +0x{off:X}, {ln} bytes"
                  + ("   <-- observed title overlay" if off == 0x5B800 else ""))
        print("\nEach still needs its LOAD BASE established before runtime activation. NOTE (claim C034):\n"
              "for the level entries this cannot be\n"
              "done from their own bytes. They contain NO internal direct calls (zero jal targets above\n"
              "text_end), so there is nothing to triangulate from, and their embedded absolute constants\n"
              "spread over ~1.6MB. Do NOT assume the arena 0x8007AA38 just because OVL0 lands there. The\n"
              "one line that settles it is an observed load: a CD/archive trace must report a3 (the\n"
              "WAD byte offset) next to dest, so reaching a level names the base outright.")


if __name__ == "__main__":
    main()
