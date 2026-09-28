#!/usr/bin/env python3
"""The WAD-RELOAD evidence at a level entry, on the product, from the resident images themselves.

    uv run --frozen python tools/oracle_level_entry.py
    uv run --frozen python tools/oracle_level_entry.py --selftest

WHAT IT ANSWERS, and what it cannot. Issue 0114 exists because a level entry is the only place the
product must discard and reload guest code at a REUSED load address and invalidate every translation
that came from it. The oracle already compares the STATE across that boundary
(`tools/oracle_spyro1_demo.py`, `demo_playing`: 15 of 15 decisive ranges equal). What no run had
recorded is the other half: WHICH image was resident on each side, at which generation, and WHICH
guest address ranges were handed to the invalidation owner on the way across.

This reads that from the product's own live state through the REPL (`tools/drive.py`'s `Port`), and
it reads the framework's two answers the only honest way available without touching psxport:

  * resident image identity: the product's `Core::imageCatalog()`, reached through the guest's own
    overlay pointer. `g_UpdateMoby` (`0x80075734`) holds the CURRENT level overlay's update entry,
    written by `SetOverlayPointers` (`external/spyro-1/src/overlay_pointers.c`; the image's own jump
    table is at `0x800113A4` and every one of its 43 level cases is a `sw ..., 0x5734($at)`), so the
    value read there names the resident overlay and therefore its load generation's ADDRESSES.
  * invalidation: the framework's one owner is `psx::cpu::notifyExecutableWrite`, called from
    `Core::writeGuestMemory` (every guest store) and from the module-load paths. This probe does not
    re-derive which ranges it was given; it reports the ranges the product's own CD/archive owner
    published for the load, from the `cdq` channel, and says so in those words.

WHAT IT DELIBERATELY DOES NOT CLAIM. A generation NUMBER is a per-`Core` counter
(`ImageCatalog::activate` increments it), so it is comparable only within one process, and the REPL
exposes no command that reads the catalog. What is comparable across the boundary, and is reported,
is: which overlay the pointer names before and after, whether that address is the SAME address the
previous level used (the reuse claim), and whether the declared guest state agrees across the
crossing. The number that would close the loop -- the generation pair and the invalidated block list
-- needs one framework read command, named in the report.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

import drive  # noqa: E402
import guest_globals  # noqa: E402
from drive import GS_PLAYING  # noqa: E402

# The guest's own overlay pointer, named by the shipping address owner.
G_UPDATE_MOBY = 0x80075734

# The module arena every Spyro overlay loads at (titles/spyro1/core/spyro1_field_scheduler.cpp
# documents `arena 0x8007AA38` and the title overlay's update entry at `arena+0x174`).
ARENA_BASE = 0x8007AA38

# Filled by the crossing, and read after the run: the resident overlay entry on each side.
before_overlay = 0
after_overlay = 0

# The published line, in the shape `cd_queue.cpp::transfer` writes it. `coverage=[a,b)` is the part
# of the REQUESTED range this transfer actually supplied, and it is read rather than assumed: a line
# whose coverage does not start at 0 means part of that range was already resident, so attributing
# the whole range to this one would credit a write that did not happen.
CDQ = re.compile(r"\[cdq\] (loader|stream): base=(\d+) dest=0x([0-9A-F]+) len=(\d+) "
                 r"offset=0x([0-9A-F]+) token=0x([0-9A-F]+) -> coverage=\[(\d+),(\d+)\)")


def parse_loads(text: str) -> tuple[list[tuple[int, int, int]], int]:
    """(destination, covered_length, base_lba) per published transfer, in log order. A line this
    cannot parse is NOT skipped silently: the caller is given the count that parsed and the count
    that was seen, because a shorter list must not read as "there were no loads".

    `dest` is the guest destination and `len` the request length (both as the owner publishes them);
    `coverage` says which part of the request this transfer supplied, and only that part is
    attributed."""
    loads: list[tuple[int, int, int]] = []
    seen = 0
    for line in text.splitlines():
        match = CDQ.search(line)
        if not match:
            continue
        seen += 1
        begin, end = int(match.group(7)), int(match.group(8))
        if end <= begin:
            continue
        loads.append((int(match.group(3), 16) + begin, end - begin, int(match.group(2))))
    return loads, seen


def classify(loads: list[tuple[int, int, int]], address: int) -> list[tuple[int, int, int]]:
    """The loads whose destination RANGE covers `address` -- the ones whose bytes the resident code at
    that address came from. The rule is the range test, not a load address: a level's overlay is
    published as several transfers at consecutive destinations, and the one that happens to start
    lowest is not the one that wrote the code at +0x174. Returns (destination, length, end) so a
    reader can see the coverage rather than take it on trust.

    `address` is the resident overlay entry read from `g_UpdateMoby`; nothing here infers an image
    from a level number."""
    out = []
    for destination, length, _ in loads:
        end = destination + length
        if destination <= address < end:
            out.append((destination, length, end))
    return out


def selftest() -> int:
    """Both answers, and the one that matters: a load list that is SHORTER than what was logged must
    report itself, because "the arena held no load" is the conclusion a silent short read invents."""
    text = ("[cdq] loader: base=100 dest=0x8007AA38 len=8192 offset=0x0 token=0x1 -> "
            "coverage=[0,8192) complete=1 accepted=1\n"
            "[cdq] stream: base=200 dest=0x80100000 len=2048 offset=0x0 token=0x2 -> "
            "coverage=[0,2048) complete=1 accepted=1\n"
            "unrelated line that is not a transfer\n"
            # A PARTIAL cover: the request spans 0x8007AA38..0x8008AA38 but only its first 0x2000
            # bytes were supplied here, the rest already being resident. A reader that attributed
            # the whole request range would claim to have written 0x8007CA38..0x8008AA38, which
            # nothing in this line says happened.
            "[cdq] stream: base=300 dest=0x8007D000 len=131072 offset=0x4000 token=0x3 -> "
            "coverage=[4096,12288) complete=0 accepted=1\n")
    loads, seen = parse_loads(text)
    assert seen == 3 and len(loads) == 3, (seen, loads)
    assert loads[0] == (0x8007AA38, 8192, 100), loads
    # Only the COVERED span is attributed: this request asks for 0x8007D000..0x8008D000 but supplied
    # 0x8007E000..0x80080000, so those are the only bytes it may be credited with.
    assert loads[2] == (0x8007E000, 8192, 300), loads
    # A byte inside the covered span is attributed; its first and last bytes are the edges a
    # start-at-0 or length-only reader would get wrong.
    assert classify(loads, 0x8007E000) == [(0x8007E000, 8192, 0x80080000)], classify(loads, 0x8007E000)
    assert classify(loads, 0x8007FFFF) == [(0x8007E000, 8192, 0x80080000)]
    # The gap this partial transfer left -- between the first load's end (0x8007CA38) and this one's
    # cover (0x8007E000) -- must be UNATTRIBUTED, not bridged to the nearest transfer on either side.
    assert classify(loads, 0x8007D000) == [], classify(loads, 0x8007D000)
    assert classify(loads, 0x80080000) == [], "a byte past the covered span must have no supplier"
    # An address no load covers at all returns NOTHING, not the nearest load.
    assert classify(loads, 0x80000000) == [], "an uncovered address must return no load at all"

    # Two transfers whose ranges OVERLAP, and an address both cover: both must be reported, in log
    # order. An attribution that kept only the first would drop a real write of those bytes.
    overlap = parse_loads(
        "[cdq] loader: base=1 dest=0x8007AA38 len=4096 offset=0x0 token=0x1 -> coverage=[0,4096) "
        "complete=1 accepted=1\n"
        "[cdq] loader: base=2 dest=0x8007AA38 len=8192 offset=0x0 token=0x2 -> coverage=[0,8192) "
        "complete=1 accepted=1\n")[0]
    both = classify(overlap, 0x8007AB38)
    assert both == [(0x8007AA38, 4096, 0x8007BA38), (0x8007AA38, 8192, 0x8007CA38)], both
    assert classify(overlap, 0x8007BA38) == [(0x8007AA38, 8192, 0x8007CA38)]
    assert classify(overlap, 0x8007CA38) == []

    empty, empty_seen = parse_loads("nothing here\n")
    assert empty == [] and empty_seen == 0, (empty, empty_seen)
    print(f"  parsed {len(loads)} of {seen} transfer lines; only the COVERED span is attributed, so "
          f"a partial cover does not claim the rest; an uncovered address returns 0 loads; two "
          f"overlapping transfers are both reported, in log order")
    print("oracle_level_entry selftest PASS")
    return 0


def main() -> int:
    global before_overlay, after_overlay
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--binary", type=Path, default=Path("scratch/assets/spyro1/SCUS_942.28"))
    parser.add_argument("--log", type=Path, default=Path("scratch/oracle/level_entry.log"))
    parser.add_argument("--debug", default="cdq",
                        help="PSXPORT_DEBUG channels; the default is the one that publishes the load "
                             "destinations ('cdq')")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()

    disc = drive.disc_path()
    if not disc:
        print("REFUSED: no disc; set PSXPORT_SPYRO_DISC in the environment or .env", file=sys.stderr)
        return 2
    environment = drive.environment(disc)
    environment["PSXPORT_DEBUG"] = args.debug
    port = drive.Port(args.executable, args.binary, args.log, environment)
    # The log is the run's own record, and the crossing has to be SPLIT inside it: a load published
    # before the guest changed level supplied the OLD image, and one published after supplied the
    # NEW one. Counting every transfer that covers an address across the whole run would report the
    # same six for both sides and cannot say which of them wrote the code that is executing.
    def log_mark() -> int:
        return len(args.log.read_text(errors="replace").splitlines()) if args.log.is_file() else 0

    before_mark = 0
    after_mark = 0
    try:
        drive.Navigator(port).reach_gameplay()
        port.mark_arrival()
        before_mark = log_mark()
        before_level = port.word(guest_globals.kLevelId)
        before_overlay = port.word(G_UPDATE_MOBY)
        before_words = port.words(before_overlay, 8)
        print(f"[level] before the crossing: g_LevelId={before_level} "
              f"g_UpdateMoby=0x{before_overlay:08X}")
        print(f"[level]   overlay prologue 0x{before_overlay:08X}: "
              + " ".join(f"{value:08X}" for value in before_words))
        # A real walk, with the pad, until the guest itself reports a different resident level. No
        # teleport, no forced load, no written guest byte: the stop condition IS the observation.
        entering = before_level
        # The mark is taken INSIDE the stop predicate, on the first observation at which the guest
        # reports a different resident level -- not after the walk returns. The level's overlay
        # transfers are published between the level id changing and the walk ending, so a mark taken
        # at the end would put the new image's own load on the wrong side of the split and report
        # "no transfer supplied it" for an image the run demonstrably loaded.
        marks: list[int] = []

        def left_level() -> bool:
            if port.word(guest_globals.kLevelId) == entering:
                return False
            if not marks:
                marks.append(log_mark())
            return True

        # The walk is `drive.Seeker` over the shared `spyro1_steering.Walk` policy -- the same one
        # `tools/drive.py --seek-portal` runs -- with the stop condition being the guest's OWN report
        # that a different level is resident. Real pad edges, no teleport, no written guest byte.
        drive.Seeker(port, "portal", drive.portal_targets(port.words), arrived=0,
                     stop=left_level, stop_is=f"left level {entering}").walk()
        # The crossing is observed, not assumed: the transition screen is a real phase, so the run
        # continues until the guest is back in GS_Playing with the new level resident, and the
        # before/after samples are both taken in GS_Playing.
        for _ in range(600):
            port.run(10)
            if (port.gamestate() == GS_PLAYING
                    and port.word(guest_globals.kLevelId) != entering):
                break
        after_level = port.word(guest_globals.kLevelId)
        after_overlay = port.word(G_UPDATE_MOBY)
        after_mark = marks[0] if marks else log_mark()
        after_words = port.words(after_overlay, 8)
        print(f"[level] after the crossing: g_LevelId={after_level} "
              f"g_UpdateMoby=0x{after_overlay:08X}")
        print(f"[level]   overlay prologue 0x{after_overlay:08X}: "
              + " ".join(f"{value:08X}" for value in after_words))
        print(f"[level] overlay prologue bytes identical across the crossing: "
              f"{before_words == after_words}")
        print(f"[level] resident overlay address REUSED across the crossing: "
              f"{before_overlay == after_overlay} "
              f"(0x{before_overlay:08X} -> 0x{after_overlay:08X}); level id changed: "
              f"{before_level != after_level}")
    finally:
        code = port.end()
    text = args.log.read_text(errors="replace") if args.log.is_file() else ""
    lines = text.splitlines()
    loads, seen = parse_loads(text)
    before_text = "\n".join(lines[:before_mark])
    after_text = "\n".join(lines[after_mark:])
    before_loads, before_seen = parse_loads(before_text)
    after_loads, after_seen = parse_loads(after_text)
    print(f"[level] published transfers parsed: {len(loads)} of {seen} 'cdq' lines in {args.log}")
    print(f"[level] log split at the crossing: {before_seen} of {seen} transfer lines before the "
          f"guest changed level, {after_seen} after (log lines 0..{before_mark} and "
          f"{after_mark}..{len(lines)})")
    print("[level] WHY these ranges are also the INVALIDATED ones, and what is not claimed:")
    print("[level]   the 'cdq' line is written by cd_queue.cpp::transfer, which calls")
    print("[level]   ArchiveTransfer::read; that owner writes the bytes with Core::mem_w8 and then")
    print("[level]   calls Core::imageCatalog().activate() over the same span, and the comment at")
    print("[level]   that call site says the canonical memory writer owns executable invalidation")
    print("[level]   (Core::writeGuestMemory -> psx::cpu::notifyExecutableWrite, MappedStore).")
    print("[level]   So each covered range below is BOTH the image-activation span and a range handed")
    print("[level]   to the invalidation owner, by the shipping path and not by re-derivation here.")
    print("[level]   NOT CLAIMED: the per-Core invalidation COUNT and the image generation NUMBERS.")
    print("[level]   Those live in psxport (LightrecExecutor's counters, ImageCatalog::activate) and")
    print("[level]   no REPL command reads them; naming a number here would be inventing one.")
    before_side_covering: list[tuple[int, int, int]] = classify(before_loads, before_overlay)
    for side, address, side_loads, side_seen in (
            ("before", before_overlay, before_loads, before_seen),
            ("after", after_overlay, after_loads, after_seen)):
        covering = classify(side_loads, address)
        print(f"[level] transfers {side} the crossing whose COVERED range contains the {side} "
              f"overlay entry 0x{address:08X}: {len(covering)} of {side_seen} ({len(side_loads)} "
              f"parsed)")
        for destination, length, end in covering:
            print(f"[level]   0x{destination:08X}..0x{end:08X} ({length} bytes)")
        if not covering:
            print(f"[level]   NONE. No transfer published {side} the crossing supplied the bytes at "
                  f"0x{address:08X}, so this probe cannot name the image that supplied them. That "
                  f"is a limit of the evidence and is not reported as an identity.")
        elif side == "after":
            # The address-reuse claim, stated from the two sides' own numbers rather than asserted.
            reused = [(d, length) for d, length, _ in covering
                      if any(bd <= before_overlay < bd + bl for bd, bl, _ in before_side_covering)]
            print(f"[level]   of those, {len(reused)} also cover the BEFORE crossing's entry "
                  f"0x{before_overlay:08X}: the same guest addresses were written by a load on "
                  f"each side of the boundary, which is the reuse issue 0114 exists to cover")
            for destination, length in reused:
                print(f"[level]     0x{destination:08X}..0x{destination + length:08X} ({length} bytes)")
    print(f"[level] product run log {args.log} (exit {code})")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
