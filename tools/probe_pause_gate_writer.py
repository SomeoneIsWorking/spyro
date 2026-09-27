#!/usr/bin/env python3
"""probe_pause_gate_writer.py — who can WRITE the pause-menu gate global 0x800758B8?

WHY THIS EXISTS. The stage-2 producer's GUI gate is [0x800758B8], read by 0x8001A40C at
0x8001A410 and tested by `bnez` at 0x8001A43C. A driven pause menu spends its whole life with that
word reading 0, so EITHER the writer is not running under this port, OR the word is not what the
gate is. The existing tools cannot separate those:

  * tools/probe_store_sites.py answers from external/spyro-1/asm, a SPARSE listing — it reported
    0 stores over 17.2% listing coverage and said so itself, which cannot rule a writer out.
  * a raw literal grep for 0x58B8 cannot resolve a base register, so it finds the read sites and
    nothing else.

So this asks the IMAGE: every instruction in the main image whose SW/C SD/SH target can resolve to
the watched word, by the same base-register resolution the static probe uses, but over the real
bytes rather than the listing. `scanned` and `matched` are both reported, and a scan that resolved
no base register at all is reported as a scan that could not have answered.

Not a permanent repo tool: it is one question about one global. Run from the repo root:
    uv run --frozen python tools/probe_pause_gate_writer.py
"""
from __future__ import annotations

import argparse
import pathlib
import re
import struct
import sys

IMAGE = pathlib.Path(__file__).resolve().parent.parent / "scratch/assets/spyro1/SCUS_942.28"
# The main image's own load geometry, the same one tools/probe_guest_disasm.py verifies on every
# run: file_offset = 0x800 + (addr - 0x80010000), and it reported 62183/62183 agreement.
FILE_BASE = 0x800
VADDR_BASE = 0x80010000

# lui/addiu pairs are the only way this compiler builds a 0x8007xxxx global access, and the
# matching `sw`/`sh`/`sb` is the write. Both are required: a store alone cannot name the address.
LUI = re.compile(r"^lui\s+\$(\w+),\s*0x([0-9a-f]+)$")
ADDIU = re.compile(r"^addiu\s+\$(\w+),\s*\$(\w+),\s*(0x[0-9a-f]+|-?\d+)$")
STORE = re.compile(r"^s([whb])\s+\$\w+,\s*(0x[0-9a-f]+|-?\d+)\(\$(\w+)\)$")

SW = 0
SH = 1
SB = 2


def disasm(word: int) -> tuple[int, int, int, int, int, int] | None:
    """Return (op, rs, rt, imm, imm_is_signed, funct) for the encodings this scan needs."""
    op = word >> 26
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    imm = word & 0xFFFF
    funct = word & 0x3F
    if word == 0:
        return None
    return op, rs, rt, imm, 1 if imm & 0x8000 else 0, funct


def scan(watched: int) -> tuple[int, int]:
    data = IMAGE.read_bytes()
    total_words = (len(data) - FILE_BASE) // 4
    # reg name -> last lui high half. BOTH address-forming idioms this compiler emits must resolve
    # through it, and getting that wrong is silent: 0x8001A40C reads the watched word as
    # `lui $v0,0x8007` / `lw $v0,0x58b8($v0)`, with NO addiu anywhere, so a resolver that only
    # understood `lui`+`addiu`+`sw` reported ZERO reads of a word this scan's own subject is read
    # by. That is a guaranteed-wrong answer wearing a denominator, which is why `reads` is printed
    # and why the caller can see a scan that found nothing to ask about.
    lui_regs: dict[str, int] = {}
    # (reg, offset) -> the address that register+offset denotes, when both are known
    resolved: dict[tuple[str, int], int] = {}
    matched = 0
    read_sites = 0
    loads = 0

    def target(base: str, offset: int) -> int | None:
        """The address `offset(base)` denotes, by either address-forming idiom."""
        if (base, offset) in resolved:
            return resolved[(base, offset)]
        hi = lui_regs.get(base)
        return None if hi is None else hi + offset

    for index in range(total_words):
        # file_offset = 0x800 + (addr - 0x80010000), so addr = 0x80010000 + index*4 and the
        # FILE_BASE cancels. Getting this wrong shifts every PRINTED label by 0x800 while leaving
        # the scanned bytes correct, which is how a scan of the right bytes reports a write at an
        # address holding an unrelated instruction.
        addr = VADDR_BASE + index * 4
        word = struct.unpack_from("<I", data, FILE_BASE + index * 4)[0]
        d = disasm(word)
        if d is None:
            continue
        op, rs, rt, imm, signed, funct = d
        simm = imm - 0x10000 if signed else imm
        if op == 0x0F:  # lui
            lui_regs[f"${rt}"] = imm << 16
            continue
        if op == 0x09:  # addiu
            hi = lui_regs.get(f"${rs}")
            if hi is not None:
                resolved[(f"${rt}", simm)] = hi + simm
            continue
        if op not in (0x23, 0x2B, 0x29, 0x28):  # lw, sw, sh, sb
            continue
        loads += 1
        where = target(f"${rs}", simm)
        if where != watched:
            continue
        if op == 0x23:
            read_sites += 1
        else:
            matched += 1
            kind = {0x2B: "sw", 0x29: "sh", 0x28: "sb"}[op]
            print(f"  WRITE 0x{addr:08X}  {kind}  -> 0x{watched:08X}")
    print(f"watched address : 0x{watched:08X}")
    print(f"image           : {IMAGE}")
    print(
        f"words scanned   : {total_words}  (file_offset = 0x{FILE_BASE:X} + (addr - 0x{VADDR_BASE:08X}))"
    )
    print(f"load/stores seen: {loads}")
    print(f"resolved addrs  : {len(resolved)} distinct (register, offset) pairs")
    print(f"reads of it     : {read_sites} lw sites")
    print(f"WRITES of it    : {matched}")
    return matched, read_sites


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", default="0x800758B8", help="guest word to find writers of")
    args = parser.parse_args()
    if not IMAGE.exists():
        print(f"REFUSED: no provisioned image at {IMAGE}", file=sys.stderr)
        return 2
    matched, reads = scan(int(args.address, 16))
    if reads == 0:
        print("verdict: NO lw in the MAIN IMAGE resolves to this word — a scan that found no READ")
        print("         of its own subject cannot be used to argue about writes either. The resolver")
        print("         is wrong, or the word is only reached from a WAD overlay.")
        return 1
    if matched == 0:
        print("verdict: this word is READ but never WRITTEN by a sw/sh/sb in the MAIN IMAGE.")
        print("         That is a real statement about the main image and NOT about the game: a WAD")
        print("         overlay reusing these addresses, a computed store target, or a bulk copy are")
        print("         all invisible here, and the overlays are where a per-level menu state machine")
        print("         would live.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
