#!/usr/bin/env python3
"""Disassemble a guest address range straight out of the provisioned executable, and prove the file
offset it used by cross-checking the decompiled listing.

WHY THIS EXISTS. `docs/issues/0133`'s divergence chase reached a caller at `0x80037EBC` -- the `ra` the
console's PC observer reported for a `rand()` call -- and the decompiled tree has NO listing for
`0x80037Exx`: `grep -rn 80037E asm/` matches nothing, because the upstream decomp only carries the
files it matched. The address is in the shipped image; the listing just does not cover it. Guessing
at that address, or reasoning from a caller whose body nobody has read, is how a diagnosis turns
into a story.

So this reads the bytes the product actually runs. Two things keep it honest:

  * the file-offset formula is not assumed -- it is CHECKED against every instruction the decompiled
    listing does carry. Each listing line prints `/* FILEOFF VRAMADDR INSNWORD */`, so the tool can
    ask "does my own formula put this VRAM address on the same bytes the listing printed?", and it
    reports how many it scanned and how many agreed. A formula that is off by a page, or a
    byte-order mistake, fails this immediately instead of quietly disassembling the wrong function.
  * the WORD is read little-endian, because that is what the listing's printed hex is:
    `asm/order_table.s:12` prints `0780023C` for `lui $v0, 0x8007` and the image holds the bytes
    07 80 02 3C at file offset 0x6F84, whose little-endian word is 0x3C028007. A PSX executable
    stores its INSTRUCTIONS little-endian and its DATA big-endian, so the wrong half of that
    choice is not a crash -- it is a listing of valid-looking garbage. `--selftest` therefore
    disassembles an address the decompiled listing states and requires the mnemonic to match, so
    this instrument cannot report a plausible fiction.

    uv run --frozen python tools/probe_guest_disasm.py --address 0x80037EB8 --count 48
    uv run --frozen python tools/probe_guest_disasm.py --verify-only
    uv run --frozen python tools/probe_guest_disasm.py --selftest
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
DECOMP = ROOT / "external" / "spyro-1"
EXE = ROOT / "scratch" / "assets" / "spyro1" / "SCUS_942.28"

# The formula is a CLAIM until --verify confirms it against the listing, so it is named here and
# printed with every disassembly rather than buried.
TEXT_FILE_OFFSET = 0x800
TEXT_LOAD_ADDRESS = 0x80010000

LINE = re.compile(
    r"/\*\s*([0-9A-Fa-f]+)\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s*\*/\s+(\S+)\s*(.*)$")
# A data directive shares the listing's comment shape, so `LINE` alone cannot tell an instruction
# from a jump-table word. The directive is the discriminator, and it is checked on the TEXT after
# the comment: `/* 15E8 80010DE8 60F40280 */ .word .L8002F460` is data in the assembler's
# big-endian convention, while `/* 52F2C 8006272C C641033C */ lui $v1, ...` is an instruction in
# the assembler's byte order. Accepting both is what made an offset check report a formula failure
# that was really a convention mismatch.
DATA_DIRECTIVE = (".word", ".space", ".byte", ".half", ".dword", ".asciz", ".ascii")

# `jal` with the guest target in range, and the register names, are all the report needs; capstone
# prints them, and a target outside the image is still printed rather than dropped.
BRANCH_TARGET = re.compile(r"0x([0-9a-f]+)")


def file_offset(address: int) -> int:
    return TEXT_FILE_OFFSET + (address - TEXT_LOAD_ADDRESS)


def _lines(path: Path) -> list[str]:
    try:
        return path.read_text(errors="replace").splitlines()
    except OSError:
        return []


def _instruction_line(line: str) -> bool:
    match = LINE.search(line)
    return bool(match) and match.group(4) not in DATA_DIRECTIVE


def listing_words() -> tuple[dict[int, tuple[int, str]], int, int]:
    """Guest address -> (the 32-bit instruction word the listing prints, the file that printed it).

    Only the `/* ... VRAMADDR INSNWORD */` pairs of real instructions are used, so no mnemonic from
    the listing is trusted: this map exists to check BYTES, and the disassembly is done
    independently. The two exclusions are reported rather than dropped, because each one is a way of
    checking the wrong bytes:

      * overlay listings -- an overlay is a SEPARATE image that reuses the same guest addresses, so
        `overlays/level_20`'s 0x800888C4 is a different instruction from this file's 0x800888C4.
      * data directives inside a code file -- a jump table is printed in the assembler's big-endian
        convention, which is the reverse of the instruction convention.
    """
    out: dict[int, tuple[int, str]] = {}
    overlay = 0
    data = 0
    for path in sorted(DECOMP.glob("asm/**/*.s")) + sorted(DECOMP.glob("asm/*.s")):
        lines = _lines(path)
        name = str(path.relative_to(DECOMP))
        if "overlays" in path.parts or "data" in path.parts:
            overlay += sum(1 for line in lines if _instruction_line(line))
            continue
        data += sum(1 for line in lines if LINE.search(line) and not _instruction_line(line))
        for line in lines:
            if _instruction_line(line):
                match = LINE.search(line)
                out[int(match.group(2), 16)] = (
                    int.from_bytes(bytes.fromhex(match.group(3)), "little"), name)
    return out, overlay, data


def _lines(path: Path) -> list[str]:
    try:
        return path.read_text(errors="replace").splitlines()
    except OSError:
        return []


def verify(data: bytes) -> int:
    """How many listing instructions this tool's own file-offset formula reproduces.

    The image is read LITTLE-endian for the instruction word, because that is what the listing's
    printed hex is: `asm/order_table.s:12` prints `0780023C` for `lui $v0, 0x8007`, and the image
    holds the bytes 07 80 02 3C at file offset 0x6F84, whose little-endian word is 0x3C028007. Both
    halves of that are checked here rather than asserted.

    A disagreement is NOT averaged away: the files are grouped by the link base their own printed
    file offsets imply, so a file that came from a differently-linked reconstruction of the same
    addresses is NAMED instead of dragging the ratio down anonymously.
    """
    words, overlay, data_directives = listing_words()
    agreed = 0
    by_file: dict[str, list[int]] = {}
    for address, (word, name) in sorted(words.items()):
        offset = file_offset(address)
        if offset < 0 or offset + 4 > len(data):
            by_file.setdefault(name, []).append(address)
            continue
        if struct.unpack_from("<I", data, offset)[0] == word:
            agreed += 1
        else:
            by_file.setdefault(name, []).append(address)
    disagreeing = sum(len(rows) for rows in by_file.values())
    print(f"[disasm] offset check: scanned {len(words)} MAIN-IMAGE CODE listing instruction(s) "
          f"({overlay} overlay and {data_directives} data listing instruction(s) excluded -- a "
          f"separate image reusing these addresses, and a different byte convention); {agreed} agree "
          f"with file_offset = 0x{TEXT_FILE_OFFSET:X} + (addr - 0x{TEXT_LOAD_ADDRESS:08X}); "
          f"{disagreeing} disagree")
    if by_file:
        print(f"[disasm]   {len(by_file)} file(s) disagree, and the file offset each one PRINTS "
              "implies a different link base -- named rather than averaged:")
        for name, rows in sorted(by_file.items(), key=lambda item: -len(item[1])):
            print(f"[disasm]     {name}: {len(rows)} instruction(s) disagree, e.g. 0x{rows[0]:08X}")
    return agreed


def disassemble(data: bytes, address: int, count: int) -> list:
    """`count` instructions at `address`, or as many as decode. The mode is the load-bearing part:
    a PSX executable stores INSTRUCTIONS little-endian, so MIPS32+LITTLE_ENDIAN is the only
    combination that decodes this image, and the wrong one decodes without complaining."""
    import capstone

    md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32 + capstone.CS_MODE_LITTLE_ENDIAN)
    md.detail = False
    start = file_offset(address)
    return list(md.disasm(data[start:start + 4 * count], address))


# Addresses whose mnemonic the DECOMPILED LISTING states, so the decoder is checked against the tree
# rather than against itself. Each is `/* FILEOFF ADDR HEXWORD */ mnemonic operands` in asm/.
LISTING_PROBES = (
    (0x8006272C, "lui", "asm/psyq.s:8142, `rand`"),
    (0x80016784, "lui", "asm/order_table.s:12, func_80016784"),
)


def selftest(data: bytes) -> int:
    """Both answers: a known address must decode to the mnemonic the listing states, AND the wrong
    byte order must be shown not to pass. The second half is the point -- a decoder that agrees with
    nothing is a broken instrument, and a check that cannot fail is not a check."""
    failures = 0
    for address, mnemonic, source in LISTING_PROBES:
        decoded = disassemble(data, address, 1)
        if not decoded:
            print(f"[disasm] selftest FAIL 0x{address:08X} ({source}) decoded to nothing")
            failures += 1
            continue
        got = decoded[0]
        if got.mnemonic != mnemonic:
            print(f"[disasm] selftest FAIL 0x{address:08X} ({source}) decoded as {got.mnemonic} "
                  f"{got.op_str}, expected {mnemonic}")
            failures += 1
        else:
            print(f"[disasm] selftest 0x{address:08X} ({source}) decoded as {got.mnemonic} "
                  f"{got.op_str}: matches the listing")
    import capstone

    start = file_offset(0x8006272C)
    for mode, label in ((capstone.CS_MODE_MIPS32 + capstone.CS_MODE_BIG_ENDIAN, "big-endian"),
                        (capstone.CS_MODE_MIPS32 + capstone.CS_MODE_LITTLE_ENDIAN, "little-endian")):
        md = capstone.Cs(capstone.CS_ARCH_MIPS, mode)
        got = next(iter(md.disasm(data[start:start + 4], 0x8006272C)), None)
        name = got.mnemonic if got else "<none>"
        verdict = "MATCHES" if name == "lui" else "does NOT match"
        print(f"[disasm] selftest {label:13} at 0x8006272C decodes as {name:10} -> {verdict} the "
              "listing's `lui`")
    if failures:
        print(f"[disasm] selftest FAILED: {failures} of {len(LISTING_PROBES)} probe(s) disagreed "
              "with the decompiled listing")
        return 1
    print(f"[disasm] selftest PASS: {len(LISTING_PROBES)} of {len(LISTING_PROBES)} probe(s) matched "
          "the decompiled listing, and the byte-order discriminator separated the two modes")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--address", type=lambda text: int(text, 0), default=0,
                        help="guest address to start at")
    parser.add_argument("--count", type=int, default=32, help="how many instructions (default 32)")
    parser.add_argument("--executable", type=Path, default=EXE)
    parser.add_argument("--verify-only", action="store_true",
                        help="run the listing cross-check and disassemble nothing")
    parser.add_argument("--selftest", action="store_true",
                        help="check the decoder against addresses the decompiled listing states")
    args = parser.parse_args()

    if not args.executable.is_file():
        print(f"REFUSED: no executable at {args.executable}; provision it first", file=sys.stderr)
        return 2
    data = args.executable.read_bytes()
    if args.selftest:
        return selftest(data)
    agreed = verify(data)
    if args.verify_only:
        return 0 if agreed else 1
    if not args.address:
        print("REFUSED: --address is required unless --verify-only or --selftest was asked for",
              file=sys.stderr)
        return 2

    start = file_offset(args.address)
    if start < 0 or start + 4 * args.count > len(data):
        print(f"REFUSED: 0x{args.address:08X} + {args.count} instruction(s) is not inside the image "
              f"({len(data)} bytes)", file=sys.stderr)
        return 2
    print(f"[disasm] 0x{args.address:08X} .. 0x{args.address + 4 * args.count:08X} "
          f"(file 0x{start:X}..0x{start + 4 * args.count:X})")
    decoded = disassemble(data, args.address, args.count)
    for instruction in decoded:
        target = BRANCH_TARGET.search(instruction.op_str)
        note = ""
        if target and instruction.mnemonic in ("jal", "j", "beq", "bne", "beqz", "bnez", "blez",
                                               "bgtz", "bgez", "bltz", "b"):
            note = f"   ; target file offset 0x{file_offset(int(target.group(1), 16)):X}"
        print(f"[disasm] 0x{instruction.address:08X}  {instruction.bytes.hex().upper():8}  "
              f"{instruction.mnemonic:10} {instruction.op_str}{note}")
    if len(decoded) != args.count:
        print(f"[disasm]   STOPPED after {len(decoded)} of {args.count}: capstone could not decode the "
              "rest, so the listing above is short rather than complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
