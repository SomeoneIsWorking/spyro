#!/usr/bin/env python3
"""xrefs.py — who branches or jumps to this guest address?

WHY THIS EXISTS. "This address is a branch target, so where does control come from, and what are the
registers when it arrives?" is a question that comes up constantly in this port, and both obvious ways
of answering it are wrong:

  * GREPPING A DISASSEMBLY LISTING. Capstone disassembled linearly from the image base desyncs the
    moment it hits data, so the listing's text near a branch may be garbage even though the WORD is a
    perfectly good branch. Matching the target address as a STRING in that listing reported ZERO
    predecessors for 0x8007CBA0 (issue 0027) when there is exactly one. A wrong answer of "nothing
    reaches this" is worse than no answer: it reads as "this code is dead".
  * A lui/addiu IMMEDIATE SCAN. That is the store-side equivalent mistake and has already cost this
    project two failed searches for a writer that stored through a register (issue 0027 again).

So this decodes every 4-byte-aligned word NUMERICALLY and computes its branch/jump target from the
encoding. It never needs the surrounding instruction stream to be valid, which is exactly the property
a data-bearing overlay image breaks.

  J / JAL          target = (pc+4 & 0xF0000000) | (imm26 << 2)
  branches/REGIMM  target = pc + 4 + (simm16 << 2)

VALIDATE IT BEFORE YOU TRUST IT — the tool prints how. A scan that returns nothing is indistinguishable
from a broken scan, so point it at an address you already know the answer for (a function's common exit
usually has many predecessors) and check it finds them.

THE DELAY SLOT IS PRINTED WITH EVERY HIT, and that is the point. The instruction after a branch runs
whether or not the branch is taken, so it decides the register state at the TARGET. Reading a gate
without it inverts the meaning: at 0x8007CAA8 the delay slot sets v0 = 5 unconditionally, which turned
"identify s0 and v0 at runtime" into the static fact "the gate is s0 == 5".

Usage:
  xrefs.py 0x8007CBA0                                  # search the resident MAIN executable
  xrefs.py 0x8007CBA0 --img scratch/bin/overlays/OV_5B800.BIN --base 0x8007AA38
  xrefs.py 0x8007CC48 --img ... --base ...             # a known-busy address, to validate the scan

An overlay is keyed BY its load address, so --base is required with --img and is never guessed: a wrong
base silently reports correctly-decoded branches at wrong addresses.
"""
import argparse
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_EXE = os.path.join(REPO, "scratch", "bin", "spyro", "SCUS_942.28")

# op codes whose target is pc-relative (simm16 << 2): REGIMM, beq/bne/blez/bgtz and their _L forms,
# plus the COP branch opcodes (bc0f/bc1t/...), which are encoded in the same shape.
REL_OPS = {0x01, 0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17}


def branch_target(w, pc):
    """Target of this word if it is a branch/jump, else None. Encoding only — no context needed."""
    op = w >> 26
    if op in (0x02, 0x03):                      # j / jal
        return ((pc + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
    if op in REL_OPS:
        imm = w & 0xFFFF
        if imm & 0x8000:
            imm -= 0x10000
        return pc + 4 + (imm << 2)
    return None


def load_image(path, base):
    """(bytes, base). For the PS-EXE, the text base and payload come from the header, not guessed."""
    data = open(path, "rb").read()
    if base is not None:
        return data, base
    if data[:8] == b"PS-X EXE":
        # PS-EXE header: t_addr at 0x18, text starts at file offset 0x800.
        t_addr = struct.unpack_from("<I", data, 0x18)[0]
        return data[0x800:], t_addr
    sys.exit(f"{path}: not a PS-EXE, so --base is required (an overlay is keyed BY its load address; "
             f"guessing one reports correctly-decoded branches at wrong addresses)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", help="guest address that is branched/jumped to, e.g. 0x8007CBA0")
    ap.add_argument("--img", default=DEFAULT_EXE, help=f"image to scan (default {DEFAULT_EXE})")
    ap.add_argument("--base", help="load address of --img; required for a raw overlay image")
    a = ap.parse_args()

    want = int(a.target, 16)
    base = int(a.base, 16) if a.base else None
    data, base = load_image(a.img, base)

    try:
        from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN
        md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 + CS_MODE_LITTLE_ENDIAN)
    except ImportError:
        md = None

    def text(off):
        """Disassemble ONE word in isolation — no stream, so data cannot desync it."""
        if md is None or off + 4 > len(data):
            return ""
        for ins in md.disasm(data[off:off + 4], base + off):
            return f"{ins.mnemonic} {ins.op_str}".strip()
        return "(not an instruction)"

    hits = []
    for off in range(0, len(data) - 3, 4):
        w = struct.unpack_from("<I", data, off)[0]
        if branch_target(w, base + off) == want:
            hits.append(off)

    print(f"scanning {os.path.basename(a.img)} @ 0x{base:08X} ({len(data)} bytes) for -> 0x{want:08X}\n")
    for off in hits:
        pc = base + off
        # The delay slot decides the register state AT THE TARGET, so it is part of the answer.
        print(f"  0x{pc:08X}: {text(off)}")
        print(f"              delay: {text(off + 4)}")
    print(f"\n{len(hits)} branch/jump(s) reach 0x{want:08X}")
    if not hits:
        print("\nNOTHING REACHES IT — treat that as a claim about your INPUTS until you validate the scan.\n"
              "  * Wrong image? An address in the overlay arena lives in whichever overlay is RESIDENT;\n"
              "    check with tools/whatis.py --ram <a fresh dump> before believing any overlay read.\n"
              "  * Wrong --base? Every decoded target shifts with it.\n"
              "  * Reached indirectly (jr/jalr through a table or pointer)? This scan cannot see that;\n"
              "    it finds only branches whose target is in the encoding. Use PSXPORT_WWATCH or a probe.\n"
              "  Validate by scanning an address you already know is busy, such as a function's common exit.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
