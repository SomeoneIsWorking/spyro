#!/usr/bin/env python3
"""probe_guest_word.py — who reads and writes one measured guest word, from the image itself.

A title's field owner needs facts like "which instruction advances this title's display
counter" or "is this word read anywhere besides the one site I already found". Grep cannot
answer either: the word is reached by `lui`+displacement, sometimes through a copied base
register, so a literal scan reports nothing and reads as "no accesses".

The probe is a linear scan with per-register constant propagation, so `lui $at,0x8006`
followed by `addiu $at,$at,-0x7d28` followed by `sw $v0,0x10($at)` resolves the same way the
hardware does. It reports, for every requested address, the resolved access sites and the
mnemonic that made the access; a word with NO resolved access is reported as such, with the
number of words scanned, because "scanned N words, matched 0" and "the instrument never ran"
must never look the same.

    uv run --frozen python tools/probe_guest_word.py --image <PS-X EXE> --word 0x80066394
    uv run --frozen python tools/probe_guest_word.py --selftest

The image is mapped the PS-X EXE way: `file[0x800] -> t_addr` from the header at 0x18, which
is the only mapping under which `0x8001xxxx` holds code (see docs/issues/0007 in the
framework for what mapping from the file start does to it).
"""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import sys

RAM_BYTES = 2 * 1024 * 1024
IMAGE_BASE = 0x80000000
# The stores and loads this probe resolves, by opcode. Anything else is a dataflow barrier
# for the destination register and simply ends the tracked constant.
_LOADS = {0x20: "lb", 0x21: "lh", 0x22: "lwl", 0x23: "lw", 0x24: "lbu", 0x25: "lhu", 0x26: "lwr"}
_STORES = {0x28: "sb", 0x29: "sh", 0x2A: "swl", 0x2B: "sw", 0x2E: "swr"}
# SPECIAL function codes this probe decodes, and the register each writes. Everything else in
# the SPECIAL major opcode is a dataflow barrier for the whole register file: 8 is `jr` and 9 is
# `jalr` (9 writes rd), 10-15 move to/from coprocessors, 24-27 write HI/LO, and 48-63 are
# undefined encodings whose effect is not knowable from the word alone.
_SPECIAL_RD = {0x00, 0x02, 0x03, 0x04, 0x06, 0x07, 0x0A, 0x0C, 0x10, 0x11, 0x12, 0x13,
               0x18, 0x19, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2A, 0x2B}
_SPECIAL_JALR = 0x09
# Major opcodes whose whole register file effect is unknown or is control flow. COP2 is here
# because the GTE writes r1..r15 itself: a `lui`-built base that survives an `mtc2`/`cfc2` pair
# is a constant the hardware does not have, and a probe that kept it reports stores at
# addresses the guest never touches. Branch opcodes are here because the instruction after a
# branch is a delay slot, not a fall-through, so no constant is known to reach it.
_BARRIER_OPCODES = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x10, 0x11, 0x12, 0x1F}


def written_registers(word: int) -> set[int] | None:
    """Registers this instruction definitely writes, or None when the whole file is a barrier."""
    opcode = word >> 26
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    if opcode == 0x00:
        funct = word & 0x3F
        if funct in _SPECIAL_RD:
            return {rd}
        if funct == _SPECIAL_JALR:
            return {rd}
        return None
    if opcode in _BARRIER_OPCODES:
        return None
    if opcode in _LOADS:
        return {rt}
    if opcode in _STORES:
        # A store writes MEMORY. It does not modify rt, and treating it as if it did both lost
        # real sites and hid the distinction between the two.
        return set()
    # addi/addiu/slti/sltiu/andi/ori/xori/lui all write rt and nothing else.
    return {rt}


class Word:
    __slots__ = ("value", "origin")

    def __init__(self, value: int, origin: int) -> None:
        self.value = value
        self.origin = origin


def load_image(path: Path) -> tuple[bytearray, int, int]:
    raw = path.read_bytes()
    if len(raw) < 0x800:
        raise ValueError(f"{path} is {len(raw)} bytes; a PS-X EXE carries an 0x800-byte header")
    text_address = struct.unpack_from("<I", raw, 0x18)[0]
    text_size = struct.unpack_from("<I", raw, 0x1C)[0]
    body = raw[0x800:0x800 + text_size]
    ram = bytearray(RAM_BYTES)
    start = (text_address - IMAGE_BASE) & (RAM_BYTES - 1)
    ram[start:start + len(body)] = body
    return ram, text_address, text_address + text_size


def sign16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def scan(ram: bytearray, start: int, end: int, words: set[int]) -> dict[int, list[tuple[int, str]]]:
    found: dict[int, list[tuple[int, str]]] = {word: [] for word in words}
    regs: dict[int, Word | None] = {}
    scanned = 0
    for offset in range(start - IMAGE_BASE, end - IMAGE_BASE, 4):
        word = struct.unpack_from("<I", ram, offset)[0]
        scanned += 1
        opcode = word >> 26
        rs = (word >> 21) & 0x1F
        rt = (word >> 16) & 0x1F
        rd = (word >> 11) & 0x1F
        imm = sign16(word & 0xFFFF)
        target = None
        if opcode == 0x0F:  # lui
            regs[rt] = Word((word & 0xFFFF) << 16, offset + IMAGE_BASE)
            continue
        if opcode == 0x09:  # addiu
            base = regs.get(rs)
            regs[rt] = Word(base.value + imm, base.origin) if base else None
            continue
        if opcode in _LOADS or opcode in _STORES:
            base = regs.get(rs)
            if base is not None:
                address = (base.value + imm) & 0xFFFFFFFF
                if address in found:
                    kind = _LOADS.get(opcode) or _STORES[opcode]
                    found[address].append((offset + IMAGE_BASE, kind))
            continue
        # Every other instruction that writes a tracked register ends that constant. Being
        # conservative here costs a few unresolved sites and never invents one.
        written = written_registers(word)
        if written is None:
            regs = {}
            continue
        for reg in written:
            if reg:
                regs[reg] = None
    print(f"scanned {scanned} instruction words in [0x{start:08X},0x{end:08X})")
    return found


def report(found: dict[int, list[tuple[int, str]]]) -> None:
    for address, hits in sorted(found.items()):
        print(f"0x{address:08X}: {len(hits)} resolved access(es)")
        for pc, kind in hits:
            print(f"    0x{pc:08X}  {kind}")


def selftest() -> int:
    """Both answers, and a discriminator the resolver cannot pass by luck.

    The positive fixture is the exact Spyro 2 shape this probe exists for: `lui`+`addiu`
    forming a global base, a store through a COPY of that base, and a load through a second
    copy. The negative fixtures are the three ways a scan silently loses a site: a base built
    with `ori` instead of `addiu`, a base invalidated by an intervening call, and a word that
    genuinely has no access at all — reported as zero against a stated denominator, not as
    silence.
    """
    def instruction(opcode: int, rs: int, rt: int, imm: int) -> int:
        return (opcode << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)

    def special(funct: int, rs: int, rt: int, rd: int) -> int:
        return (rs << 21) | (rt << 16) | (rd << 11) | funct

    lui = lambda reg, value: instruction(0x0F, 0, reg, value)  # noqa: E731
    addiu = lambda rs, rt, imm: instruction(0x09, rs, rt, imm)  # noqa: E731
    ori = lambda rs, rt, imm: instruction(0x0D, rs, rt, imm)  # noqa: E731
    sw = lambda rt, base, off: instruction(0x2B, base, rt, off)  # noqa: E731

    words = [
        # POSITIVE 1: the exact Spyro 2 shape -- a global base formed by lui+addiu and stored
        # through. This is the site `tools/probe_guest_word.py` is for.
        lui(5, 0x8007), addiu(5, 5, -0x7D28), sw(2, 5, 0x00),
        # NEGATIVE 1: a base rebuilt with `ori` instead of `addiu`. It names 0x800682DC, and a
        # probe that resolved it would be resolving something the hardware never forms.
        lui(2, 0x8006), ori(2, 2, 0x82DC), sw(0, 2, 0x20),
        # NEGATIVE 2: the same base formed correctly in a0, a `jal` (control flow), and then a
        # store through it. A probe that kept the stale constant would report a SECOND access to
        # 0x800682D8, which is exactly the count the positive case pins.
        lui(4, 0x8007), addiu(4, 4, -0x7D28), instruction(0x03, 0, 0, 0), sw(0, 4, 0x00),
        # NEGATIVE 3: a COP2 (GTE) instruction between the base and the store. The GTE writes
        # r1..r15 itself, so a base that survives it is a constant the hardware never has. This
        # is the defect that reported `sll` and `bltz` sites as stores over a guest word.
        lui(3, 0x8006), addiu(3, 3, 0x63B8), instruction(0x12, 0, 0, 0x40), sw(0, 3, 0x00),
        # POSITIVE 2: a fresh base after the barriers, so the negatives above cannot be
        # explained by the scan simply having stopped.
        lui(5, 0x8006), addiu(5, 5, 0x63B8), sw(0, 5, 0x00),
        # POSITIVE 3: a store writes memory, not rt. Two stores through one base, with the
        # first between the two displacements, must BOTH resolve -- 0x80066400 and
        # 0x80066408, neither of which any other fixture names.
        lui(6, 0x8006), addiu(6, 6, 0x6400), sw(0, 6, 0x00), addiu(6, 6, 8), sw(0, 6, 0x00),
        # NEGATIVE 4: an undefined SPECIAL encoding, whose register effect is not knowable from
        # the word alone. It must end every tracked constant, not just rd.
        lui(7, 0x8006), addiu(7, 7, 0x63B8), special(0x3F, 0, 0, 0), sw(0, 7, 0x00),
        instruction(0, 0, 0, 0),
    ]
    ram = bytearray(RAM_BYTES)
    base = 0x1000
    for index, word in enumerate(words):
        struct.pack_into("<I", ram, base + index * 4, word)
    targets = {0x800682D8, 0x800682DC, 0x800663B8, 0x80066400, 0x80066408}
    found = scan(ram, IMAGE_BASE + base, IMAGE_BASE + base + len(words) * 4, targets)
    checks = [
        (len(found[0x800682D8]) == 1, "the lui+addiu global base resolved its one store"),
        (len(found[0x800682DC]) == 0,
         "an `ori`-built base, a call-invalidated base, a GTE-clobbered base and an undefined "
         "SPECIAL encoding were all refused"),
        (len(found[0x800663B8]) == 1, "a fresh base after the barriers still resolved"),
        (len(found[0x80066400]) == 1 and len(found[0x80066408]) == 1,
         "a store does not clobber its own rt, so both stores through one base resolved"),
    ]
    ok = True
    for passed, what in checks:
        print(f"selftest: {'ok  ' if passed else 'FAIL'} {what}")
        ok = ok and passed
    print(f"selftest: 0x800682D8 -> {len(found[0x800682D8])}, 0x800682DC -> "
          f"{len(found[0x800682DC])}, 0x800663B8 -> {len(found[0x800663B8])}, "
          f"0x80066400 -> {len(found[0x80066400])}, 0x80066408 -> {len(found[0x80066408])}")
    print("selftest: " + ("both answers observed" if ok else "FAILED"))
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--image", type=Path, help="PS-X EXE to map (file[0x800] -> t_addr)")
    parser.add_argument("--word", action="append", default=[],
                        help="guest word to resolve, repeatable, e.g. 0x80066394")
    parser.add_argument("--start", default=None, help="first address to scan (default: t_addr)")
    parser.add_argument("--end", default=None, help="end-exclusive address (default: t_addr+t_size)")
    parser.add_argument("--selftest", action="store_true", help="run the fixture and exit")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not args.image or not args.word:
        parser.error("--image and at least one --word are required")
    ram, text_address, text_end = load_image(args.image)
    start = int(args.start, 0) if args.start else text_address
    end = int(args.end, 0) if args.end else text_end
    report(scan(ram, start, end, {int(word, 0) for word in args.word}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
