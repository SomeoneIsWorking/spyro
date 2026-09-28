#!/usr/bin/env python3
"""overlay_image.py — the bytes a WAD overlay address names, and the census over them.

ONE CONCEPT: which bytes a guest address in the overlay arena names, and what is in them. Spyro 1
keeps every runtime overlay inside `WAD.WAD` on the disc, not as files, and every one of them loads
at ONE guest address (`ARENA_BASE`), so a bare address is meaningless without the resident image.
`docs/issues/0138` ended with "the WAD overlays were not scanned" as the one unmeasured place a
guest-owned skip for `GS_EntranceAnimation` could hide; this module is what closes that.

THE MAIN-IMAGE FORMULA IS NOT RESTATED HERE. `tools/probe_guest_disasm.py` owns the executable's
offset arithmetic and is checked against the decompiled listing (`--verify-only`); this module
imports its image handle rather than writing a second copy, for the reason `tools/decomp_image.py`
gives: a second copy of a formula is a second unverified claim about the same bytes.

THE OVERLAY FORMULA IS A CLAIM, AND THIS MODULE CHECKS IT. A PSX executable stores INSTRUCTIONS
little-endian; an overlay entry does too, and the entry's word 0 at byte 0 lands at the arena base.
The one number that settles it is recorded in claim C111 — guest `0x8007CBA0` holds `0x16020029` —
so `--selftest` requires that word to come out right here AND requires a wrong base to fail. A tool
that cannot fail is not a check.

`--selftest` also runs the census against a POSITIVE control that is known to exist: the main
image's stage-14 held-Start acceleration at `0x80033354` and the pause menu's confirm at
`0x8002E988`. A census instrument that finds neither those nor a planted overlay site is broken,
and uniform output across a corpus is the tell.

RESIDENCY IS MEASURED, NOT ASSUMED. `--residency RAMDUMP` reports, for each WAD entry, the share of
its words that the arena of a real RAM dump actually holds, so "this overlay is the resident one" is
a measurement with a denominator instead of a belief.

    uv run --frozen python tools/overlay_image.py --selftest
    uv run --frozen python tools/overlay_image.py --census
    uv run --frozen python tools/overlay_image.py --residency scratch/ov9/live_title.bin
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

# The one owner of the MAIN-image arithmetic, imported not copied.
import probe_guest_disasm as _main_image  # noqa: E402

# Every Spyro 1 WAD overlay loads here. Not a guess: `docs/issues/0138` records the arena as the
# address the observed title overlay (OV_5B800, WAD entry 2) was seen at, and the header jump table
# of 31 of the 36 code entries lands inside the image at this base.
ARENA_BASE = 0x8007AA38
ARENA_LAST = 0x800FFFFF
SECTOR = 0x800

# The two pad globals (claim C110) and the two words stage 9's gate reads and terminal writes.
PAD_EDGE = 0x80077378
PAD_HELD = 0x80077380
G_GAMESTATE = 0x800757D8
G_ROT_Y = 0x80076E1E
G_PRESET = 0x80076EA8
G_CAM_STATE = 0x80076E28
# Start or Cross, the mask `docs/findings/start-skip-map.md` establishes for this title.
START_MASK = 0x840
# Circle or Start. A SECOND mask, found here and recorded in docs/issues/0141: the same title-screen
# block tests 0xA000 before the 0x840 it is credited with, and a census that only looks for 0x840
# cannot see it.
CIRCLE_START_MASK = 0xA000

# The one word claim C111 records, and the only independent ground truth for the overlay formula.
KNOWN_GOOD = (0x8007CBA0, 0x16020029)


class OverlayRefusal(Exception):
    """The archive or the dump is absent. Raised, never answered with an empty census."""


@dataclass(frozen=True)
class WadEntry:
    index: int
    offset: int
    length: int


def read_index(path: Path) -> list[WadEntry]:
    """WAD.WAD's first sector is a flat array of (byte offset, length) pairs, sector-aligned.

    This reads the SAME table `tools/wad_index.py` documents and scores. It is re-implemented rather
    than imported because that tool's table walk lives inside its `main()`; if you change one, change
    both, and `--selftest` re-checks entry 2's (0x5B800, 0x3800) against claim C111's overlay.
    """
    if not path.is_file():
        raise OverlayRefusal(
            f"no WAD archive at {path}. It is not provisioned by tools/provision_title.py (that "
            "extracts the boot executable only); read it out of the disc, and authenticate it.")
    size = path.stat().st_size
    words = struct.unpack(f"<{SECTOR // 4}I", path.open("rb").read(SECTOR))
    out: list[WadEntry] = []
    for i in range(0, len(words) - 1, 2):
        off, length = words[i], words[i + 1]
        if off == 0 or length == 0:
            break
        if off % SECTOR or off + length > size:
            break
        out.append(WadEntry(i // 2, off, length))
    if not out:
        raise OverlayRefusal(f"{path} yielded no index entries; the index layout is not what "
                             "tools/wad_index.py documents")
    return out


class Archive:
    """One WAD.WAD, plus the per-entry code score that decides which entries are worth scanning."""

    # Opcodes that dominate real MIPS, borrowed from tools/wad_index.py's own justification: scoring
    # on "did the word decode" is useless, because the decoder names unknown encodings instead of
    # failing and the first version of that tool scored every entry 100%.
    CODE_OPS = frozenset({
        "nop", "sll", "srl", "sra", "addiu", "addu", "addi", "subu", "and", "andi", "or", "ori",
        "xor", "xori", "lui", "lw", "lh", "lhu", "lb", "lbu", "sw", "sh", "sb", "jal", "jr",
        "jalr", "j", "beq", "bne", "blez", "bgtz", "slt", "sltu", "slti", "sltiu", "mult", "multu",
        "div", "divu", "mfhi", "mflo", "move"})

    def __init__(self, path: Path, min_score: float = 90.0):
        self.path = path
        self.entries = read_index(path)
        self.min_score = min_score
        self._data: dict[int, bytes] = {}

    def digest(self) -> str:
        h = hashlib.sha256()
        with self.path.open("rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                h.update(block)
        return h.hexdigest()

    def bytes(self, index: int) -> bytes:
        if index not in self._data:
            entry = self.entries[index]
            with self.path.open("rb") as f:
                f.seek(entry.offset)
                self._data[index] = f.read(entry.length)
        return self._data[index]

    def code_score(self, index: int) -> float:
        sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools" / "mips"))
        from decode import decode  # noqa: PLC0415
        data = self.bytes(index)
        n = len(data) // 4
        if not n:
            return 0.0
        good = 0
        for k in range(n):
            word = int.from_bytes(data[k * 4:k * 4 + 4], "little")
            if decode(ARENA_BASE + k * 4, word).op in self.CODE_OPS:
                good += 1
        return 100.0 * good / n

    def code_entries(self) -> list[WadEntry]:
        out = [e for e in self.entries if self.code_score(e.index) >= self.min_score]
        if not out:
            raise OverlayRefusal(
                f"no WAD entry scored >= {self.min_score}% code opcodes; a corpus that scanned "
                "nothing is not a negative result")
        return out

    # -- the mapping ----------------------------------------------------------------------------

    def contains(self, address: int, index: int) -> bool:
        return ARENA_BASE <= address < ARENA_BASE + self.entries[index].length

    def offset_of(self, address: int) -> int:
        return address - ARENA_BASE

    def word(self, index: int, address: int) -> int:
        """The 32-bit instruction word at an arena address, read LITTLE-endian.

        Little-endian because that is how a PSX executable stores instructions; its DATA half is
        big-endian, so the wrong choice here decodes into valid-looking garbage rather than failing.
        `selftest` pins it against claim C111's known-good word.
        """
        if not self.contains(address, index):
            raise OverlayRefusal(
                f"0x{address:08X} is outside entry {index} "
                f"(0x{ARENA_BASE:08X}..0x{ARENA_BASE + self.entries[index].length:08X})")
        data = self.bytes(index)
        return struct.unpack_from("<I", data, self.offset_of(address))[0]

    def residency(self, dump: bytes) -> list[tuple[float, int, int, int]]:
        """(share, index, matched_words, total_words) per entry, descending, above 50% only."""
        rows = []
        # The arena's offset into a flat RAM dump. Parenthesised, because `&` binds LOOSER than `+`
        # in Python: without them the expression is ARENA_BASE & (0x1FFFFF + k*4), which is right
        # for k == 0 and wrong for every word after it. That bug reported a 100%-resident overlay as
        # a 0.03% match, and a uniform near-zero across every entry is exactly the shape of a broken
        # instrument rather than a negative result.
        base = (ARENA_BASE & 0x1FFFFF)
        for entry in self.entries:
            if entry.length > 200000:
                continue
            data = self.bytes(entry.index)
            n = len(data) // 4
            matched = sum(
                1 for k in range(n)
                if dump[base + k * 4:base + k * 4 + 4] == data[k * 4:k * 4 + 4])
            share = 100.0 * matched / n
            if share > 50.0:
                rows.append((share, entry.index, matched, n))
        rows.sort(reverse=True)
        return rows


# -- the census ------------------------------------------------------------------------------

BRANCH_OPS = frozenset({0x02, 0x03, 0x04, 0x05, 0x06, 0x07})
LOAD_OPS = {0x20: "lb", 0x21: "lh", 0x23: "lw", 0x24: "lbu", 0x25: "lhu"}
STORE_OPS = {0x28: "sb", 0x29: "sh", 0x2B: "sw"}


def resolve_address(archive: Archive, index: int, address: int, reg: int, depth: int = 16):
    """Walk BACKWARD from `address` for the absolute address `reg` held, or (None, why).

    `address` must be an instruction that does NOT write `reg` (the `andi` under test, say). The walk
    skips instructions that do not write the register and STOPS at a branch, because past a branch
    the straight-line predecessor is not the only way to arrive — reporting a value across one would
    be a second unverified claim. This stopping rule is load-bearing: the first version of this walk
    ran past branches and reported all 14 `andi 0x840` sites as UNRESOLVED, a uniform result that
    read as "no pad word here" and was wrong about four of them.
    """
    at = address
    for _ in range(depth):
        offset = archive.offset_of(at)
        if offset < 0:
            return None, "off the front of the image"
        word = archive.word(index, at)
        op = (word >> 26) & 0x3F
        rs, rt = (word >> 21) & 31, (word >> 16) & 31
        imm = word & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op in BRANCH_OPS and at != address:
            return None, f"branch at 0x{at - 4:08X} ends the straight-line run"
        if op == 0x0F and rt == reg and rs == 0:
            return (imm << 16) & 0xFFFFFFFF, f"lui 0x{imm << 16:08X}"
        if op == 0x09 and rt == reg:
            if rs == 0:
                return simm & 0xFFFFFFFF, f"addiu $zero,{simm}"
            base, why = resolve_address(archive, index, at - 4, rs, depth - 1)
            if base is None:
                return None, f"addiu base ${rs} unresolved ({why})"
            return (base + simm) & 0xFFFFFFFF, f"addiu ${rs},{simm} on 0x{base:08X}"
        if op in LOAD_OPS and rt == reg:
            base, why = resolve_address(archive, index, at - 4, rs, depth - 1)
            if base is None:
                return None, f"load base ${rs} unresolved ({why})"
            return (base + simm) & 0xFFFFFFFF, f"{LOAD_OPS[op]} 0x{(base + simm) & 0xFFFFFFFF:08X}"
        at -= 4
    return None, f"depth {depth} exhausted"


def function_start(archive: Archive, index: int, address: int, max_back: int = 8192) -> int | None:
    """The nearest `addiu $sp,$sp,-N` prologue at or before `address`, or None."""
    steps = 0
    at = address
    while steps < max_back:
        word = archive.word(index, at)
        if ((word >> 26) == 0x09 and ((word >> 21) & 31) == 29 and ((word >> 16) & 31) == 29
                and (word & 0x8000)):
            return at
        offset = archive.offset_of(at)
        if offset < 0:
            return None
        at -= 4
        steps += 1
    return None


def last_definition(archive: Archive, index: int, site: int, reg: int) -> int | None:
    """The address of the LAST instruction at or before `site` that writes `reg`, searched back to
    the enclosing function's prologue.

    This is what makes the census complete. The straight-line walk in `resolve_address` STOPS at a
    branch, which is right for a single basic block but leaves a quarter of the `andi 0x840` sites
    unresolved: this title loads a pad word ONCE into $v1 and then tests it from several blocks, so
    the defining load is across a branch. A branch is not a redefinition, so a function-scoped
    search is the correct granularity — the cost is that the result is a "last definition in this
    function", which is a weaker claim than "in this block", and `resolve_definition` names the
    definition's own address so a reader can check it.
    """
    start = function_start(archive, index, site)
    if start is None:
        return None
    at = site
    while at >= start:
        word = archive.word(index, at)
        op = (word >> 26) & 0x3F
        rt = (word >> 16) & 31
        writes = (op in LOAD_OPS or op in STORE_OPS or op in BRANCH_OPS
                  or op in (0x00, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F))
        if writes and rt == reg and op != 0x00:
            return at
        if op == 0x00 and rt == reg:            # `sll rd,rs,0` is a move, which writes rd
            return at
        at -= 4
    return None


def resolve_definition(archive: Archive, index: int, definition: int, reg: int):
    """Resolve the ABSOLUTE ADDRESS loaded by the instruction at `definition`, or (None, why)."""
    return resolve_address(archive, index, definition, reg)


def andi_sites(archive: Archive, index: int, mask: int) -> list[int]:
    data = archive.bytes(index)
    n = len(data) // 4
    return [ARENA_BASE + k * 4 for k in range(n)
            if ((int.from_bytes(data[k * 4:k * 4 + 4], "little") >> 26) & 0x3F) == 0x0C
            and (int.from_bytes(data[k * 4:k * 4 + 4], "little") & 0xFFFF) == mask]


def classify_andi(archive: Archive, index: int, site: int, reg: int) -> tuple[int | None, str]:
    return resolve_address(archive, index, site, reg)


def census(archive: Archive) -> dict:
    """The pad / Start / gamestate census over every code entry, with its denominators."""
    entries = archive.code_entries()
    words = 0
    andi_total: Counter[int] = Counter()
    pad_reads: list[tuple[int, int, str]] = []
    gamestate_reads = 0
    for entry in entries:
        data = archive.bytes(entry.index)
        n = len(data) // 4
        words += n
        for k in range(n):
            word = int.from_bytes(data[k * 4:k * 4 + 4], "little")
            op = (word >> 26) & 0x3F
            if op == 0x0C:
                andi_total[word & 0xFFFF] += 1
            if op not in LOAD_OPS and op not in STORE_OPS:
                continue
            rs, rt = (word >> 21) & 31, (word >> 16) & 31
            imm = word & 0xFFFF
            simm = imm - 0x10000 if imm & 0x8000 else imm
            target = (0x80070000 + simm) & 0xFFFFFFFF
            if target not in (PAD_EDGE, PAD_HELD, G_GAMESTATE, G_ROT_Y, G_PRESET, G_CAM_STATE):
                continue
            # Prove the base register: a `lui rt, 0x8007` must write it within 8 instructions.
            for j in range(1, 9):
                if k - j < 0:
                    break
                back = int.from_bytes(data[(k - j) * 4:(k - j) * 4 + 4], "little")
                if ((back >> 26) & 0x3F) == 0x0F and ((back >> 16) & 31) == rs and ((back >> 21) & 31) == 0:
                    if (back & 0xFFFF) == 0x8007:
                        if op in LOAD_OPS and target in (PAD_EDGE, PAD_HELD):
                            pad_reads.append((entry.index, ARENA_BASE + k * 4,
                                              "EDGE" if target == PAD_EDGE else "HELD"))
                        if op in LOAD_OPS and target == G_GAMESTATE:
                            gamestate_reads += 1
                    break
    return {"entries": entries, "words": words, "andi": andi_total, "pad_reads": pad_reads,
            "gamestate_reads": gamestate_reads}


# -- checks ---------------------------------------------------------------------------------

def selftest(archive: Archive) -> int:
    """Both answers. It must produce the known-good word, must FAIL on a wrong base, and must find
    the main image's two recorded Start sites — a census that finds neither is not a census."""
    failures = 0
    address, expected = KNOWN_GOOD
    entry2 = next((e for e in archive.entries if e.offset == 0x5B800 and e.length == 0x3800), None)
    if entry2 is None:
        print("[overlay] selftest FAIL: claim C111's entry (0x5B800, 0x3800) is not in the index")
        return 1
    got = archive.word(entry2.index, address)
    if got == expected:
        print(f"[overlay] selftest 0x{address:08X} = 0x{got:08X} — matches claim C111")
    else:
        print(f"[overlay] selftest FAIL 0x{address:08X} = 0x{got:08X}, claim C111 says 0x{expected:08X}")
        failures += 1

    # The discriminator: a base 4 bytes out must NOT reproduce the known-good word.
    shifted = struct.unpack_from(
        "<I", archive.bytes(entry2.index), archive.offset_of(address) - 4)[0]
    verdict = "does NOT match" if shifted != expected else "MATCHES (a shifted base is undetectable)"
    print(f"[overlay] selftest base 0x{ARENA_BASE - 4:08X} gives 0x{shifted:08X} -> {verdict} "
          "claim C111's word")
    if shifted == expected:
        failures += 1

    # The main image is the POSITIVE control for the census shape, read through the module that owns
    # the executable's offset arithmetic rather than through a second copy of that formula.
    exe = _main_image.EXE
    if not Path(exe).is_file():
        print(f"[overlay] selftest REFUSED: no guest executable at {exe}; the census positive "
              "control cannot run, so this is a missing corpus and not a pass")
        return 2
    image = Path(exe).read_bytes()
    for site in (0x8002E988, 0x80033354):
        word = struct.unpack_from("<I", image, _main_image.file_offset(site))[0]
        ok = ((word >> 26) & 0x3F) == 0x0C and (word & 0xFFFF) == START_MASK
        print(f"[overlay] selftest main-image positive control 0x{site:08X} = 0x{word:08X} "
              f"-> {'found' if ok else 'NOT FOUND'}")
        if not ok:
            failures += 1
    print(f"[overlay] selftest {'PASS' if not failures else 'FAILED'}: {failures} failure(s)")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--wad", type=Path,
                        default=ROOT / "scratch" / "assets" / "spyro1" / "WAD.WAD",
                        help="the authenticated WAD.WAD")
    parser.add_argument("--min-score", type=float, default=90.0)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--census", action="store_true")
    parser.add_argument("--residency", type=Path, help="a 2 MB guest RAM dump")
    args = parser.parse_args()
    try:
        archive = Archive(args.wad, args.min_score)
    except OverlayRefusal as refusal:
        print(f"REFUSED: {refusal}", file=sys.stderr)
        return 2
    if args.selftest:
        return selftest(archive)
    if args.residency:
        if not args.residency.is_file():
            print(f"REFUSED: no RAM dump at {args.residency}", file=sys.stderr)
            return 2
        dump = args.residency.read_bytes()
        if len(dump) < 0x200000:
            print(f"REFUSED: {args.residency} is {len(dump)} bytes, short of a 2 MB guest RAM window",
                  file=sys.stderr)
            return 2
        print(f"[overlay] residency of {args.residency} at base 0x{ARENA_BASE:08X}, "
              f"{len(archive.entries)} index entries scanned:")
        rows = archive.residency(dump)
        if not rows:
            print("[overlay]   no entry above 50% — that is a MISSING CORPUS, not 'no overlay'")
            return 2
        for share, index, matched, total in rows:
            print(f"[overlay]   entry {index:2d}  {archive.entries[index].length:7d} B  "
                  f"{share:5.1f}%  {matched}/{total} words")
        return 0
    if args.census:
        result = census(archive)
        print(f"[overlay] corpus: {len(result['entries'])} code entries, {result['words']} words "
              f"scanned at base 0x{ARENA_BASE:08X}")
        print(f"[overlay] `andi` masks that carry a button bit "
              f"({START_MASK:#06x} Start/Cross, {CIRCLE_START_MASK:#06x} Circle/Start):")
        for mask, count in sorted(result["andi"].items()):
            if mask & 0x8F0 or mask in (0x8, 0x800, START_MASK, CIRCLE_START_MASK):
                print(f"[overlay]   andi rt,rs,{mask:#06x}  : {count} site(s)")
        by_entry: Counter[int] = Counter(e for e, _, _ in result["pad_reads"])
        print(f"[overlay] pad-global reads: {len(result['pad_reads'])} across "
              f"{len(by_entry)} entries")
        print(f"[overlay] g_Gamestate reads: {result['gamestate_reads']}")
        return 0
    parser.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
