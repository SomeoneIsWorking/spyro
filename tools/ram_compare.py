#!/usr/bin/env python3
"""ram_compare.py — did a skipped run land on the same guest state as an unskipped one?

WHY THIS EXISTS. A skip is claimed to be a COMPLETE transition, and the only way to know whether it
is one is to compare the guest's own memory at the point where the two runs are supposed to be
interchangeable. Counting log lines cannot do it: `level flyby cancelled (1)` is what a run prints
whether the state it left behind matches the state the game leaves behind or not, and the two cases
look identical in the log. Writing guest RAM is the only evidence available, and the port already
can (`drive.py --dumpram`, the REPL `dumpram` command: 2 MiB of main RAM, little-endian, plus a 1 KB
scratchpad sidecar).

WHAT IT COMPARES, and why this field list. Two runs that differ by a skip differ by a TIMING, so
almost every word in RAM is expected to differ (a clock advanced a different number of times). The
question is not "are the dumps equal" — they are not, and a tool that reported that would be
answering nothing. The question is whether the fields that DEFINE the state a transition hands over
agree: which gamestate, which level, which load phase, whose Spyro, how much health, which progress
counters, which demo/attract state. Those are named here, each with the owner it came from, so a
difference is a fact about the game and not a diff of two large binary files.

A difference is REPORTED, never reconciled. Two runs that are meant to agree and do not is the whole
point of the tool; an exit status of 1 with the differing rows named is the finding, and a caller
that wants to proceed anyway reads the rows, not the status.

    uv run --frozen python tools/ram_compare.py A.bin B.bin
    uv run --frozen python tools/ram_compare.py A.bin B.bin --field 800757D8:32:g_Gamestate
    uv run --frozen python tools/ram_compare.py --selftest
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import guest_globals  # noqa: E402  (the C++ owner's addresses, not a second copy of them)

RAM_BASE = 0x80000000
RAM_SIZE = 0x200000
SCRATCHPAD_BASE = 0x1F800000

SPYRO = guest_globals.kSpyro
CAMERA = guest_globals.kCamera


@dataclass(frozen=True)
class Field:
    """One named word (or halfword) in guest RAM, and where its address came from."""

    name: str
    address: int
    width: int = 32
    source: str = ""


def _fields() -> list[Field]:
    """The hand-off state, named. Addresses are the shipping owner's (game/core/guest_globals.h)
    or derived numerically from the admitted image with tools/re_globals.py, which is quoted per
    field so a reader can re-derive it rather than trust a hex literal."""
    spyro = SPYRO
    titlescreen = guest_globals.kTitlescreenState
    return [
        # -- the transition itself --------------------------------------------------
        Field("g_Gamestate", guest_globals.kGamestate, source="guest_globals.h kGamestate"),
        Field("g_LoadStage", guest_globals.kLoadStage, source="guest_globals.h kLoadStage"),
        Field("g_LevelId", guest_globals.kLevelId, source="guest_globals.h kLevelId"),
        Field("g_NextLevelId", guest_globals.kNextLevelId, source="guest_globals.h kNextLevelId"),
        Field("g_PortalLevelId", 0x800758AC,
              source="re_globals.py 0x8002C664: written by the return-home terminal and by the "
                     "respawn spiral, which the decomp names g_PortalLevelId"),
        Field("g_HasLevelTransition", 0x800756D0,
              source="re_globals.py 0x8002C664; func_8002C664 sets it to 1"),
        Field("g_LevelTransTicks", 0x800756AC,
              source="re_globals.py 0x8002C664; func_8002C664 re-zeroes it"),
        Field("g_LevelTransHudActive", 0x800756B0,
              source="re_globals.py 0x8002C664; func_8002C664 sets it to 1"),
        Field("g_StateSwitch", guest_globals.kStateSwitch, source="guest_globals.h kStateSwitch"),
        Field("D_800758B8", 0x800758B8,
              source="re_globals.py 0x8002C618 (the exit-level entry zeroes it); the glide's own "
                     "counter chain increments it to 2 and then calls the terminal"),
        Field("D_8007568C", 0x8007568C,
              source="re_globals.py 0x8002C618; the pause-menu no-button tick, zeroed on entry"),
        # -- Spyro -------------------------------------------------------------------
        Field("g_Spyro.m_Position.x", spyro + 0x00, source="spyro.h: m_Position is the first field"),
        Field("g_Spyro.m_Position.y", spyro + 0x04, source="spyro.h: m_Position + 4"),
        Field("g_Spyro.m_Position.z", spyro + 0x08, source="spyro.h: m_Position + 8"),
        Field("g_Spyro.m_State", spyro + 0x78, source="guest_globals.h: m_State at +0x78"),
        Field("g_Spyro.m_health", spyro + 0x160,
              source="spyro.h: unk_0x15c then m_invulverabilityTimer then m_health"),
        Field("g_Spyro.m_bodyAnimation", spyro + 0x2C, width=8, source="spyro.h: m_bodyAnimation"),
        # -- camera ------------------------------------------------------------------
        Field("g_Camera.m_State", CAMERA + 0x58,
              source="docs/issues/0138 measured 0x80076E28 as the sub-update selector CameraUpdate "
                     "branches on; 0x80076E28 - kCamera = 0x58"),
        Field("g_Camera.unk_0xC0", CAMERA + 0xC0, source="func_8002C664 writes 0x80000012 here"),
        # -- presentation state ------------------------------------------------------
        Field("g_TitlescreenState.m_Mode", titlescreen + 0x00, source="guest_globals.h"),
        Field("g_TitlescreenState.m_State", titlescreen + 0x04, source="guest_globals.h"),
        Field("g_TitlescreenState.m_SubState", titlescreen + 0x10, source="guest_globals.h"),
        Field("g_CutsceneIdx", 0x8007566C, source="re_globals.py 0x800331AC, read at 0x800331B0"),
        Field("g_CutsceneLayout", 0x80075680, source="re_globals.py 0x800331AC, read at 0x800331C8"),
        Field("g_Fade", 0x80075918, source="re_globals.py 0x800331AC, written at 0x80033300"),
        Field("g_DemoMode", 0x80075714, source="disasm 0x8003314C: written 1 on the TSD_DemoLevel arm"),
        Field("g_DemoFadeTimer", 0x80075884,
              source="disasm 0x80033154: written 0 on the TSD_DemoLevel arm"),
        Field("g_LevelMobys", guest_globals.kLevelMobys, source="guest_globals.h kLevelMobys"),
        # -- clocks, reported but not expected to agree -------------------------------
        Field("g_GameTick", guest_globals.kGameTick,
              source="guest_globals.h; a CLOCK, so a skipped run is expected to read lower"),
        Field("g_LevelTicks", guest_globals.kLevelTicks,
              source="guest_globals.h; a CLOCK, so a skipped run is expected to read lower"),
        # -- pad, so a held button is visible in the comparison ----------------------
        Field("g_Pad.m_Down", guest_globals.kPad + 0x00, source="guest_globals.h kPad"),
        Field("g_Pad.m_Released", guest_globals.kPad + 0x04, source="guest_globals.h kPad"),
        Field("g_Pad.m_Held", guest_globals.kPad + 0x08, source="guest_globals.h kPad"),
    ]


# The fields whose value a completed transition is supposed to leave identical. A difference in one
# of these is a claim the skip did not keep; a difference in a clock is the skip working.
HANDOFF_FIELDS = frozenset({
    "g_Gamestate", "g_LoadStage", "g_LevelId", "g_NextLevelId", "g_PortalLevelId",
    "g_HasLevelTransition", "g_LevelTransHudActive", "g_Spyro.m_State", "g_Spyro.m_health",
    "g_LevelMobys", "g_CutsceneIdx",
})


class Refusal(RuntimeError):
    """An input this tool will not guess at."""


class Dump:
    """A read-only 2 MiB main-RAM image. An address outside it is a distinct outcome from a zero."""

    def __init__(self, path: Path):
        if not path.is_file():
            raise Refusal(f"{path} does not exist; a comparison against a missing capture compares "
                          f"nothing and reports every field as equal")
        self.blob = path.read_bytes()
        if len(self.blob) != RAM_SIZE:
            raise Refusal(f"{path} is {len(self.blob)} bytes; a guest main-RAM dump is exactly "
                          f"{RAM_SIZE} (the REPL `dumpram` writes 0x200000)")
        self.path = path

    def holds(self, address: int, width: int) -> bool:
        return RAM_BASE <= address and address + width <= RAM_BASE + RAM_SIZE

    def read(self, field: Field) -> int:
        if not self.holds(field.address, field.width // 8):
            raise Refusal(f"{field.name} at 0x{field.address:08X} is outside {self.path}")
        offset = field.address - RAM_BASE
        if field.width == 8:
            return self.blob[offset]
        if field.width == 16:
            return struct.unpack_from("<H", self.blob, offset)[0]
        return struct.unpack_from("<I", self.blob, offset)[0]


def parse_field(spec: str) -> Field:
    """`ADDRESS:WIDTH:NAME` — every malformed form refused by name."""
    parts = spec.split(":")
    if len(parts) != 3 or not all(part.strip() for part in parts):
        raise Refusal(f"--field {spec!r} is ADDRESS:WIDTH:NAME, e.g. 800757D8:32:g_Gamestate")
    try:
        address = int(parts[0], 16)
        width = int(parts[1], 0)
    except ValueError:
        raise Refusal(f"--field {spec!r} has a non-numeric address or width") from None
    if width not in (8, 16, 32):
        raise Refusal(f"--field {spec!r} asks for {width} bits; a guest field is 8, 16 or 32")
    if not RAM_BASE <= address < RAM_BASE + RAM_SIZE:
        raise Refusal(f"--field {spec!r} is not in guest main RAM "
                      f"(0x{RAM_BASE:08X}..0x{RAM_BASE + RAM_SIZE - 1:08X})")
    return Field(parts[2].strip(), address, width, source="--field")


def compare(left: Dump, right: Dump, fields: list[Field]) -> tuple[int, int, int, list[str]]:
    """Returns (fields compared, equal, differing, one line per difference)."""
    same = 0
    lines: list[str] = []
    for field in fields:
        a = left.read(field)
        b = right.read(field)
        if a == b:
            same += 1
            continue
        kind = "clock/timing" if field.name not in HANDOFF_FIELDS else "HAND-OFF STATE"
        lines.append(f"  DIFFERS [{kind}] {field.name} @0x{field.address:08X} "
                     f"({field.width} bit): {a} vs {b}")
    return len(fields), same, len(fields) - same, lines


def _selftest() -> int:
    failures = 0
    import shutil
    import tempfile

    # Under the repository's own scratch/, not /tmp: this machine's /tmp is a small tmpfs and the
    # project rule is that nothing lands there. Two 2 MiB captures, removed by the context manager.
    scratch = Path(__file__).resolve().parent.parent / "scratch" / "ram_compare"
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as tmp:
        root = Path(tmp)
        left = bytearray(RAM_SIZE)
        right = bytearray(RAM_SIZE)
        for field in _fields():
            offset = field.address - RAM_BASE
            # A run that has reached gameplay: level 11, loaded, playing, three health.
            left[offset:offset + 4] = struct.pack("<I", {
                "g_Gamestate": 0, "g_LoadStage": 0xFFFFFFFF, "g_LevelId": 11,
                "g_Spyro.m_health": 3,
            }.get(field.name, 0x11111111 if field.width == 32 else 0x1111))
            right[offset:offset + 4] = left[offset:offset + 4]
        # One clock differs, as it must between a skipped and an unskipped run.
        struct.pack_into("<I", right, guest_globals.kGameTick - RAM_BASE, 4321)
        # ... and one HAND-OFF field differs, which is the finding this tool exists to surface.
        struct.pack_into("<I", right, guest_globals.kGamestate - RAM_BASE, 1)
        (root / "a.bin").write_bytes(bytes(left))
        (root / "b.bin").write_bytes(bytes(right))

        total, same, differing, lines = compare(Dump(root / "a.bin"), Dump(root / "b.bin"), _fields())
        assert total == len(_fields()), total
        assert differing == 2, f"expected the clock and the gamestate to differ: {lines}"
        assert same == total - 2, (same, total)
        assert any("g_GameTick" in line and "clock/timing" in line for line in lines), lines
        assert any("g_Gamestate" in line and "HAND-OFF STATE" in line for line in lines), lines
        print(f"  two captures differing in one clock and one hand-off field -> "
              f"{same}/{total} equal, {differing} differing")
        for line in lines:
            print(line)

        # The negative that matters: a capture that is not a guest RAM image is REFUSED, not read as
        # a field full of zeros -- a short file would make every address look equal.
        (root / "short.bin").write_bytes(bytes(left[:4096]))
        for bad, why in (((root / "missing.bin"), "does not exist"), ((root / "short.bin"), "bytes")):
            try:
                Dump(bad)
            except Refusal as refusal:
                assert why in str(refusal), refusal
                print(f"  refuses {bad.name}: {refusal}")
            else:
                print(f"SELFTEST FAILED: {bad.name} was accepted", file=sys.stderr)
                failures += 1

        # Two IDENTICAL captures must report every field equal, or the tool is comparing noise.
        (root / "c.bin").write_bytes(bytes(left))
        total, same, differing, lines = compare(Dump(root / "a.bin"), Dump(root / "c.bin"), _fields())
        assert differing == 0 and same == total, (same, total, lines)
        print(f"  a capture against itself -> {same}/{total} equal, 0 differing")

    for bad in ("800757D8", "800757D8:32", "zz:D:32:n", "800757D8:24:n", "00100000:32:n"):
        try:
            parse_field(bad)
        except Refusal as refusal:
            print(f"  refuses --field {bad!r}: {refusal}")
        else:
            print(f"SELFTEST FAILED: --field {bad!r} was accepted", file=sys.stderr)
            failures += 1
    extra = parse_field("800757D8:32:g_Gamestate")
    assert (extra.address, extra.width, extra.name) == (0x800757D8, 32, "g_Gamestate"), extra
    print(f"  accepts 800757D8:32:g_Gamestate -> {extra.name}")

    if failures:
        shutil.rmtree(scratch, ignore_errors=True)
        print(f"ram_compare selftest FAILED: {failures} case(s)", file=sys.stderr)
        return 1
    shutil.rmtree(scratch, ignore_errors=True)
    print("ram_compare selftest PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("left", nargs="?", type=Path, help="the run WITHOUT the skip")
    parser.add_argument("right", nargs="?", type=Path, help="the run WITH the skip")
    parser.add_argument("--field", action="append", default=[], metavar="ADDR:WIDTH:NAME",
                        help="compare this field too; repeatable")
    parser.add_argument("--all", action="store_true",
                        help="list every field with both values, not only the differences")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        return _selftest()
    if not args.left or not args.right:
        parser.error("two captures are required (or --selftest)")

    fields = _fields()
    try:
        for spec in args.field:
            fields.append(parse_field(spec))
        left, right = Dump(args.left), Dump(args.right)
        total, same, differing, lines = compare(left, right, fields)
    except Refusal as refusal:
        print(f"ram_compare REFUSED: {refusal}", file=sys.stderr)
        return 2

    print(f"compared {total} field(s): {same} equal, {differing} differing "
          f"({args.left.name} vs {args.right.name})")
    if args.all:
        for field in fields:
            a, b = left.read(field), right.read(field)
            mark = "==" if a == b else "!="
            print(f"  {mark} {field.name:<28} @0x{field.address:08X}  "
                  f"{a if field.width != 8 else hex(a)} vs {b if field.width != 8 else hex(b)}")
    for line in lines:
        print(line)
    hand_off = [line for line in lines if "HAND-OFF STATE" in line]
    if hand_off:
        print(f"  {len(hand_off)} HAND-OFF field(s) differ: the skip did not land on the same state")
        return 1
    print("  every hand-off field agrees; the remaining differences are clocks and pad words")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
