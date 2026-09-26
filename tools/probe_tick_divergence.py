#!/usr/bin/env python3
"""Localise the demo route's gameplay divergence: the FIRST guest word that differs, by measurement.

`tools/oracle_compare.py --policy demo` reports `player.position` first differing at g_GameTick 556
and never recovering. The comparator's declared ranges answer "may the simulation differ here", not
"which word wrote the difference", and one of its ranges is 680 bytes wide. This probe answers the
second question: it drives both cores with the ROUTE's own predicates and advance, steps them in
lockstep, and diffs a WATCH SET of guest blocks at EVERY iteration, recording for each watch word the
first iteration at which it differs. The answer is then the smallest such tick, read off a table
rather than argued.

It re-implements nothing: the sessions, the driver, the route's advance and the route's predicates are
the shipping ones, and both cores are stepped by the same `route.advance(1)` the comparator uses, so
its samples ARE the comparator's samples. Every printed address is a guest address named by
external/spyro-1 (asm/data/game.bss.s, game.sbss.s, include/*.h) or by game/core/guest_globals.h.

The negative first: the verdict states how many iterations it scanned, how many words it compared, and
how many words were equal on every one of them -- so a clean scan is a measurement and not a broken
instrument. Words that already differ on the FIRST sample are reported as PRE-EXISTING, separately
from the first NEW divergence, because a word that was different before the segment began cannot be
the segment's divergence. Nothing is dropped quietly: the full per-iteration count is printed.

    uv run --frozen python tools/probe_tick_divergence.py --ticks 580
    uv run --frozen python tools/probe_tick_divergence.py --selftest
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools" / "oracle"))

import compare
import compare_cores
import drive
import guest_globals
import oracle_spyro1_demo as route

OUT_DIR = ROOT / "scratch" / "oracle" / "probe"

# The watch set: (label, address, size). Every address is read from the decomp, not guessed.
#
#  * g_Pad covers the STICKS at +0x14, which the route's declared `pad.down/released/held` do NOT
#    cover. During demo playback every word of g_Pad is written by PadDemoUpdate from the recording
#    (external/spyro-1/src/gamepad.c:165-199), so a difference in the recorded stream shows up here
#    and nowhere else in the declared ranges.
#  * g_Spyro is the whole 0x2A8 the sbss reserves, not just m_Position, so the physics
#    (m_Physics at +0xC8) and the collision results are diffed too.
#  * The globals blocks hold the demo clock, the demo pointer walk and the level identity.
G_PAD = guest_globals.kPad
G_SPYRO = guest_globals.kSpyro
G_CAMERA = guest_globals.kCamera
G_DEMO_DATA_PTR = 0x8007585C  # game.sbss.s:378, "g_DemoDataPtr"
G_PADBUFFER = 0x800786A0       # game.bss.s:227, "g_PadBuffer"
G_COLLISION_NORMAL = 0x80077368  # game.bss.s:87, "g_CollisionNormal", 0x10 to g_Pad
SPYRO_SIZE = 0x2A8             # game.bss.s:239, "Total size from 80078A58 to 80078D00"
CAMERA_SIZE = 0x110            # game.bss.s:49,  "Total size from 80076DD0 to 80076EE0"
PAD_SIZE = 0xA8                # game.bss.s:91,   "Total size from 80077378 to 80077420"

# Words the ROUTE already declares as harness residual, not simulation. They are printed, but
# separately, and never counted as a product divergence.
#  * g_LevelTicks: counted per delivered field by the VSync callback, declared informational in
#    oracle_spyro1.excluded ("g_LevelTicks (informational only)", issue 0110).
#  * g_UnprocessedFrames: the same residual seen at the park, which is a field boundary with the
#    next VBlank handler pending on both cores (oracle_spyro1.lookahead); it does not reach the
#    simulation during a demo because PadDemoUpdate ASSIGNS it 2 (gamepad.c:214).
ROUTE_RESIDUAL = {
    guest_globals.kLevelTicks: "g_LevelTicks (issue 0110)",
    guest_globals.kUnprocessedFrames: "g_UnprocessedFrames at the field-boundary park",
}

# Spyro field names by offset, from include/spyro.h. Only the fields a position divergence can be
# read out of; the probe prints an offset unannotated rather than guessing.
SPYRO_FIELDS = {
    0x00: "m_Position", 0x0C: "m_bodyRotation", 0x18: "m_headRotation", 0x24: "m_tailRotation",
    0x3C: "m_DamageFlags", 0x40: "m_FloorDistance", 0x44: "m_RotationMatrix",
    0x68: "m_State", 0x6C: "m_walkingState", 0x70: "m_idleTimer", 0x78: "m_touchingMoby",
    0x7C: "m_previousPosition", 0x88: "m_floorIdleTime", 0x8C: "m_airTime",
    0x90: "m_surfaceBelowSpyro", 0x94: "m_floorPositonOnSlope", 0xA0: "m_slopeAngle",
    0xA4: "m_againstWall", 0xA8: "m_wallAgainstSpyro", 0xB4: "m_onEdge",
    0xB8: "m_Physics", 0xC8: "m_Physics.m_TargetSpeedAngle", 0xD8: "m_Physics.m_SlopeGravityZ",
    0xDC: "m_Physics.unk_0xdc", 0xE8: "m_Physics.unk_0xe8", 0xF4: "m_Physics.m_Acceleration",
    0x100: "m_Physics.m_Velocity", 0x10C: "m_Physics.m_TrueVelocity",
    0x118: "m_Physics.m_SpeedAngle", 0x128: "m_Physics.m_TrueSpeed",
    0x12C: "m_Physics.m_CollisionMovement", 0x148: "m_Physics.m_TurnMomentum",
    0x14C: "m_Physics.m_gravity", 0x150: "m_onSlope", 0x154: "m_isGliding",
    0x160: "m_invulverabilityTimer", 0x164: "m_health", 0x16C: "m_touchingSurface",
    0x274: "m_CollisionTriangleIndex", 0x278: "m_collisionTriangleUnpacked",
}

PAD_FIELDS = {
    0x00: "m_Down", 0x04: "m_Released", 0x08: "m_Held", 0x0C: "m_Type",
    0x10: "m_LeftStickMoved", 0x14: "m_Sticks (RightX,RightY,LeftX,LeftY)",
    0x18: "m_NoButtonsDown", 0x1C: "m_NoMovementButtonPressed", 0x40: "m_BufferedInputs[0]",
}

WATCH = (
    ("g_Pad", G_PAD, PAD_SIZE),
    ("g_PadBuffer", G_PADBUFFER, 0x28),
    ("g_Spyro", G_SPYRO, SPYRO_SIZE),
    ("g_Camera", G_CAMERA, CAMERA_SIZE),
    ("g_CollisionNormal", G_COLLISION_NORMAL, 0x10),
    ("globals", 0x80075600, 0x300),
    ("demo+level", 0x80075828, 0xB0),
    ("g_LevelId", guest_globals.kLevelId, 8),
    ("g_TitlescreenState", guest_globals.kTitlescreenState, 0x20),
)

# The trajectory view: the words a movement difference is read out of, printed side by side for both
# cores at every tick of a window. `--trace 550:560`.
TRAJECTORY = (
    ("pos.x", G_SPYRO + 0x00),
    ("pos.y", G_SPYRO + 0x04),
    ("pos.z", G_SPYRO + 0x08),
    ("vel.x", G_SPYRO + 0x100),
    ("vel.y", G_SPYRO + 0x104),
    ("vel.z", G_SPYRO + 0x108),
    ("tvel.x", G_SPYRO + 0x10C),
    ("tvel.y", G_SPYRO + 0x110),
    ("tvel.z", G_SPYRO + 0x114),
    ("tspeed", G_SPYRO + 0x128),
    ("acc.x", G_SPYRO + 0xF4),
    ("acc.y", G_SPYRO + 0xF8),
    ("acc.z", G_SPYRO + 0xFC),
    ("grav", G_SPYRO + 0x14C),
    ("slopeDeg", G_SPYRO + 0xA0),
    ("againstWall", G_SPYRO + 0xA4),
    ("onSlope", G_SPYRO + 0x150),
    ("airTime", G_SPYRO + 0x8C),
    ("floorIdle", G_SPYRO + 0x88),
    ("floorDist", G_SPYRO + 0x40),
    ("surfaceBelow", G_SPYRO + 0x90),
    ("triIndex", 0x80075808),
    ("normal.x", G_COLLISION_NORMAL + 0),
    ("normal.y", G_COLLISION_NORMAL + 4),
    ("normal.z", G_COLLISION_NORMAL + 8),
)

WATCH_BYTES = sum(size for _, _, size in WATCH)
WATCH_WORDS = WATCH_BYTES // 4


def read_watch(core: compare_cores.CoreSession) -> dict[int, int]:
    """Every watch word, keyed by guest address. One dict per core, compared key by key."""
    out: dict[int, int] = {}
    for _, address, size in WATCH:
        raw = core.read(address, size)
        for offset in range(0, size, 4):
            out[address + offset] = int.from_bytes(raw[offset:offset + 4], "little")
    return out


def differing(native: dict[int, int], console: dict[int, int]) -> list[tuple[int, int, int]]:
    """(address, native, console) for every differing watch word, in address order. Nothing is
    filtered here: the caller classifies, so a residual can never hide a simulation difference."""
    return [(address, native[address], console[address]) for address in sorted(native)
            if native[address] != console[address]]


def name_of(address: int) -> str:
    for label, base, size in WATCH:
        if base <= address < base + size:
            offset = address - base
            if label == "g_Spyro":
                return f"g_Spyro+0x{offset:03X} {SPYRO_FIELDS.get(offset, '?')}"
            if label == "g_Pad":
                return f"g_Pad+0x{offset:02X} {PAD_FIELDS.get(offset, '?')}"
            return f"{label}+0x{offset:03X}"
    return "?"


def _signed(value: int) -> int:
    return value - (1 << 32) if value & 0x80000000 else value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ticks", type=int, default=400,
                        help="main-loop iterations to step in lockstep after arrival (default 400)")
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--binary", type=Path, default=Path("scratch/assets/spyro1/SCUS_942.28"))
    parser.add_argument("--bios", type=Path, default=ROOT.parent / "SCPH1001.BIN")
    parser.add_argument("--selftest", action="store_true",
                        help="prove the differ on a fixture instead of driving cores")
    parser.add_argument("--trace", default="",
                        help="print the movement words side by side for FIRST:LAST tick, e.g. 550:560")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    disc = drive.disc_path()
    if not disc:
        print("REFUSED: no disc; set PSXPORT_SPYRO_DISC in the environment or .env", file=sys.stderr)
        return 2
    environment = drive.environment(disc)
    environment.update(compare.product_env(argparse.Namespace(product_env=[])))
    product = compare.Product(ROOT / args.executable, ROOT / args.binary, environment, ROOT, Path(disc))
    product = compare.fresh_card(product, OUT_DIR)

    native = compare_cores.NativeReplSession(str(product.binary), str(product.executable),
                                             product.environment, str(product.cwd),
                                             OUT_DIR / "native.log")
    console = compare_cores.ConsoleSession(ROOT / "external" / "psxport", product.disc, args.bios, "na",
                                           OUT_DIR / "console.log")
    try:
        print(f"[probe] watch set: {len(WATCH)} block(s), {WATCH_BYTES} bytes, {WATCH_WORDS} words")
        driver = compare.Driver(route)
        settle = None
        for core in (console, native):
            used, settle = route.reach_demo_playing(driver, core, route.FIELD_BUDGET, settle)
            print(f"[probe] {core.name}: demo_playing after {used} advance steps; "
                  f"session field count {core.frames}")

        # For each watch word: the first iteration at which it differs, its values there, and how
        # many of the scanned iterations it differed on. The answer is the smallest such tick.
        first_seen: dict[int, tuple[int, int, int]] = {}
        counts: dict[int, int] = {}
        scanned = 0
        previous_tick = None
        first_tick = None
        trace = _parse_trace(args.trace)
        for index in range(args.ticks):
            for core in (native, console):
                core.hold(frozenset())
                route.advance(core, 1)
            native_words = read_watch(native)
            console_words = read_watch(console)
            tick = native_words[guest_globals.kGameTick]
            if previous_tick is not None and tick != previous_tick + 1:
                print(f"[probe] tick sequence jumped {previous_tick} -> {tick} at iteration "
                      f"{index}: the two cores did not each run one update, so these samples are not "
                      f"the comparator's")
            previous_tick = tick
            if first_tick is None:
                first_tick = tick
            scanned += 1
            if trace and trace[0] <= tick <= trace[1]:
                _trajectory_row(tick, native_words, console_words)
            for address, native_value, console_value in differing(native_words, console_words):
                first_seen.setdefault(address, (tick, native_value, console_value))
                counts[address] = counts.get(address, 0) + 1

        if first_tick is None:
            print("[probe] REFUSED: no iteration was scanned, so nothing was measured")
            return 2
        always_equal = WATCH_WORDS - len(first_seen)
        print(f"[probe] scanned {scanned} iteration(s), g_GameTick {first_tick}..{previous_tick}; "
              f"compared {scanned * WATCH_WORDS} watch words; {always_equal} of {WATCH_WORDS} words "
              f"were equal on every one of the {scanned} iterations; {len(first_seen)} word(s) "
              f"differed on at least one")
        pre_existing = [address for address in first_seen if first_seen[address][0] == first_tick]
        print(f"[probe] {len(pre_existing)} word(s) already differed on the FIRST sample "
              f"(g_GameTick {first_tick}); {len(first_seen) - len(pre_existing)} word(s) first "
              f"differed later:")
        for address in sorted(pre_existing):
            _report(address, *first_seen[address], counts[address], scanned, "PRE-EXIST ")
        for tick, native_value, console_value in sorted(
                (row for address, row in first_seen.items() if address not in pre_existing),
                key=lambda row: row[0]):
            address = next(a for a, row in first_seen.items() if row == (tick, native_value, console_value))
            _report(address, tick, native_value, console_value, counts[address], scanned, "FIRST AT  ")
        return 0
    finally:
        for core in (native, console):
            core.close()


def _parse_trace(text: str) -> tuple[int, int] | None:
    if not text:
        return None
    first, _, last = text.partition(":")
    if not last:
        raise SystemExit(f"REFUSED: --trace wants FIRST:LAST, got {text!r}")
    return int(first), int(last)


def _trajectory_row(tick: int, native: dict[int, int], console: dict[int, int]) -> None:
    """One tick, both cores, the words a movement difference is read out of, with the per-tick delta
    so a step's SIZE is visible and not only its end state."""
    print(f"[trace] tick {tick}")
    for label, address in TRAJECTORY:
        n, c = native[address], console[address]
        mark = "  " if n == c else "**"
        print(f"[trace]  {mark} {label:12} native {_signed(n):>9} console {_signed(c):>9} "
              f"delta {_signed(n) - _signed(c):+d}")


def _report(address: int, tick: int, native_value: int, console_value: int, count: int, scanned: int,
            tag: str) -> None:
    residual = f"  [{ROUTE_RESIDUAL[address]}]" if address in ROUTE_RESIDUAL else ""
    print(f"[probe] {tag} tick {tick:>4} 0x{address:08X} {name_of(address):46} native "
          f"{native_value:08X} ({_signed(native_value)}) console {console_value:08X} "
          f"({_signed(console_value)}) delta {_signed(native_value) - _signed(console_value):+d} "
          f"differed {count}/{scanned}{residual}")


def selftest() -> int:
    """Both answers: the differ must report a difference and must report none when there is none."""
    words = {address: address for address in range(0x100, 0x140, 4)}
    words[guest_globals.kLevelTicks] = 4
    assert differing(words, dict(words)) == [], "an identical pair must produce no difference"
    other = dict(words)
    other[0x110] = 0xDEADBEEF
    found = differing(words, other)
    assert found == [(0x110, 0x110, 0xDEADBEEF)], found
    noise = dict(words)
    noise[guest_globals.kLevelTicks] = 7
    rows = differing(words, noise)
    assert [row[0] for row in rows] == [guest_globals.kLevelTicks], rows
    assert all(row[0] in ROUTE_RESIDUAL for row in rows), "a residual must be classifiable"
    print(f"[probe] differ selftest PASS: 1 of 1 planted difference reported, 0 of 1 when the words "
          f"are equal, 1 of 1 declared residual reported AND classifiable ({WATCH_WORDS} watch "
          f"words, {WATCH_BYTES} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
