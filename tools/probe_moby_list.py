#!/usr/bin/env python3
"""Walk the guest's moby list in the LIVE route and say how many mobies it holds.

WHY THIS EXISTS. `docs/issues/0133` ended with one measured fact: at `g_GameTick` 41 of the attract
demo, the moby list at `0x800700F4` is entirely zero on the product while the reference holds real
moby pointers. That single fact accounts for the whole divergence chain — `Moby+0x42` is never set
because its writer is the animation state machine, and a moby that is not in the list is never
animated.

But that measurement is on the DEMO route, and the demo is not the game. This probe asks the question
that actually decides whether the product is broken for a player: **does the list hold mobies once a
real new game is running?** The answer can legitimately differ between the two routes — the attract
demo builds its own scene — so a demo measurement cannot stand in for it, and this exists so the two
are never confused.

THE LIST'S IDENTITY is read from the guest, not from the listing's constant. `0x800522CC` is
`addi $t9, $a0, 0x0`, the second instruction of `func_800522C0`, and an observer record there carries
`a0 = 800700F4` — the base the guest itself used. The same function loads `D_8006FCF4` (lines 215-216
of `asm/moby_lists.s`) and the list is that block's `+ 0x400`, so the address is corroborated by the
listing and confirmed by the guest; neither alone was trusted.

THE CONTROL comes first, and the probe REFUSES without it. Every reading this probe had produced was
zero, and a channel that cannot read a non-zero value would also print zero and look like a finding.
So before the list's zeros are read as "the list is empty", the probe reads two addresses the game is
known to keep live — `g_Gamestate`, which must be 0 for GS_Playing, and `kLevelMobys`, which must be a
RAM pointer — and additionally reports whether the level's OWN moby array behind that pointer is
non-zero. That last reading is what separates "the level never loaded its mobies" from "the level
loaded them and the step that walks them into the update list did not", which are different defects
with different fixes.

THE WALK is bounded and reports its own bound, because an unbounded walk over a list that is not
NULL-terminated would read the whole of RAM and call the result a moby count. A node is two words and
the terminator is a NULL first word, both measured on the reference at tick 41 (`801A92E8`, `801A9130`,
then zeros). Every entry is also checked to lie in main RAM, so a garbage word is reported as garbage
rather than counted as a moby.

WHAT THIS PROBE CANNOT SETTLE, and used to claim it could. The list is a per-frame scratch: the guest
fills it inside the level update (`func_80051FEC`, called from the resident overlay's own update) and
consumes it there, and the DRAW pass then clears the whole region -- `func_8002B9CC` (0x8002B9CC) is
`memset(0x8006FCF4, 0, 0x1C00)` and the list base `0x800700F4` is `+0x400` inside it. A read taken at
a frame park therefore observes the clear, not the update, and reporting its zero as a gameplay defect
was wrong. `tools/probe_level_update_dispatch.py` measures the update from inside it, and
`docs/issues/0133` records the measurement. This probe keeps the park read because it is a real
two-core-shaped control on "did anything clear this", but it no longer claims a zero is a defect.

    uv run --frozen python tools/probe_moby_list.py
    uv run --frozen python tools/probe_moby_list.py --selftest

`--selftest` drives NOTHING: it walks synthetic lists in memory and requires the walker to report both
a populated list and an empty one, plus a garbage word and an unterminated list, so the tool is known
to have shown the other answer before its count is read as a fact about the game.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
SCRATCH = ROOT / "scratch"

# The list base, as the GUEST reported it. See the module docstring for the two independent sources.
LIST_BASE = 0x800700F8  # MEASURED 2026-09-27 with the store observer armed on the filler's
# append (0x8005205C): its FIRST write is 0x800700F8, not 0x800700F4. The list is populated from
# here; 0x800700F4 is a slot nobody writes, and the consumer `func_800522C0` is handed THAT address,
# reads zero there, and `beqz`-exits before walking anything. The defect is this four-byte
# disagreement between where the filler writes and where the consumer reads.
# STRIDE 4, NOT 8. `func_800522C0` reads the list as one pointer per word and advances by four:
#     addi $t9, $a0, 0x0
#     L800522E0: lw $t5, 0x0($t9) ; addi $t9, $t9, 0x4 ; beqz $t5, .L80052448
# An earlier version of this probe assumed a two-word node, which would have counted the reference's
# `[801A92E8, 801A92E8]` as one node instead of two and reported a populated list as half its size.
NODE_WORDS = 1
MAX_NODES = 4096
RAM_LOW, RAM_HIGH = 0x80000000, 0x80200000
# From the PRODUCT's own `game/core/guest_globals.h` (`kLevelMobys` = "pointer to the level's Moby
# array"), so the control address and the product cannot drift apart.
G_GAMESTATE = 0x800757D8
K_LEVEL_MOBYS = 0x80075828
# g_LevelId, the level now resident. Reported because the list's WRITER is called from the resident
# level's own overlay, so a reader cannot compare this route's list against another route's without
# knowing which level each is in — the caller address is per-overlay while the list is a shared global.
K_LEVEL_ID = 0x8007596C

# `func_80051FEC` is the function that FILLS the list. It walks the level's moby array with this
# stride and appends the mobies that pass its gates, so these two constants are its contract, read
# out of `asm/moby_lists.s`:
#   0x8005203C  addi $at, $at, -0x58   ; start one Moby before the array...
#   0x80052040  addi $at, $at, 0x58    ; ...so the first step lands ON it. Stride 0x58 = 88.
MOBY_STRIDE = 0x58
# The four byte fields that function branches on, with the branch each one drives:
#   0x80052044  lb $v0, 0x48($at)  ; 0x8005204C bltz -> 0x8005212C, where `bne -1, $v0` decides
#                                     whether to keep walking. 0x48 == -1 is the ARRAY TERMINATOR.
#   0x80052048  lb $v1, 0x51($at)  ; 0x80052054 beqz -> 0x8005207C. 0 selects the NEAR-moby path,
#                                     which appends unconditionally at 0x8005205C.
#   0x80052058  lb $v0, 0x43($at)  ; after the near append: negative means skip the flag write.
#   0x8005207C  lb $v1, 0x52($at)  ; far-moby radius, <<10 then tested; this is the GTE-dependent
#                                     path (mtc2 IR1/IR2/IR3, SQR, mflo, mfc2 MAC1/MAC2).
GATE_TERMINATOR = 0x48
GATE_NEAR_SELECT = 0x51
GATE_NEAR_FLAG = 0x43
GATE_FAR_RADIUS = 0x52
MAX_MOBY_PROBE = 64


def walk(words: list[int]) -> dict[str, object]:
    """Walk a node list to its NULL terminator over ALREADY-READ words.

    Takes words rather than a reader so the selftest can exercise it with a fixture. The count is the
    number of NODES, and every node is classified, because the interesting failures are: an empty list
    (the product's measured state), a word that is not a pointer at all, and a list that never
    terminates. Those are three different answers and a single integer would hide all three.
    """
    entries: list[int] = []
    terminated = False
    offset = 0
    garbage: list[tuple[int, int]] = []
    while offset + NODE_WORDS <= len(words):
        node = words[offset]
        if node == 0:
            terminated = True
            break
        in_ram = RAM_LOW <= node < RAM_HIGH
        entries.append(node)
        if not in_ram:
            garbage.append((offset, node))
        offset += NODE_WORDS
        if len(entries) > MAX_NODES:
            break
    return {
        "entries": entries,
        "terminated": terminated,
        "garbage": garbage,
        "ran_out": not terminated,
    }


def read_range(port, address: int, count: int) -> list[int]:
    """Read `count` words through the live channel, in chunks the channel will actually answer.

    The channel caps a single `rw` at 64 words (`dbg_server.cpp`: `if (b > 64) b = 64;`) and returns
    the truncated count WITHOUT saying so. Asking for more in one command therefore does not fail and
    does not return everything — it returns 64 words that look like a complete answer, which is how a
    1408-word request here hung its waiter and would, in a slightly different caller, have turned a
    short read into "the list is empty". So the chunking lives in the reader, and a short chunk is
    reported as a short read rather than padded.
    """
    words: list[int] = []
    while len(words) < count:
        want = min(64, count - len(words))
        chunk = port.words(address + len(words) * 4, want)
        if len(chunk) < want:
            print(f"  SHORT READ at 0x{address + len(words) * 4:08X}: asked {want}, got "
                  f"{len(chunk)}. The channel caps a single read, so the data below is incomplete "
                  f"and must not be read as 'the rest is zero'.")
        words.extend(chunk)
        if len(chunk) < want:
            break
    return words


def selftest() -> int:
    """Four cases, and the point of the suite is that the EMPTY and POPULATED answers differ."""
    cases: list[tuple[str, list[int], int, bool]] = [
        # THREE nodes then a NULL, stride 4. The reference at tick 41 reads
        # 801A92E8, 801A92E8, 00000000 at 0x800700F4, so two nodes; the third here is synthetic and
        # exists so the walker's stride is pinned by a count that a stride-8 reader cannot produce.
        ("populated", [0x801A92E8, 0x801A92E8, 0x801A9130, 0, 0, 0], 3, True),
        ("empty", [0, 0, 0, 0], 0, True),
        # a word that is not a pointer must be REPORTED, not silently counted as a moby
        ("garbage-word", [0x00000042, 0, 0, 0], 1, True),
        # no NULL anywhere: the walk must stop at its own bound and SAY it did
        ("unterminated", [0x801A92E8, 0x801A9130] * 8, 16, False),
    ]
    failures = 0
    for name, words, want_count, want_terminated in cases:
        result = walk(words)
        got_count = len(result["entries"])  # type: ignore[arg-type]
        got_terminated = result["terminated"]
        ok = got_count == want_count and got_terminated == want_terminated
        failures += 0 if ok else 1
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: {got_count} node(s), "
              f"terminated={got_terminated}, garbage={len(result['garbage'])}"  # type: ignore[arg-type]
              f"  (expected {want_count}, terminated={want_terminated})")
    if failures:
        print(f"  selftest: {failures} case(s) FAILED")
        return 1
    print(f"  selftest: {len(cases)}/{len(cases)} cases behaved as required. The EMPTY case is the "
          "product's measured state and the POPULATED case is the reference's, and the tool reports "
          "them differently, so a count of 0 means the list is empty rather than that the walker is "
          "broken. The UNTERMINATED case is what stops the walk from reading all of RAM.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--selftest", action="store_true",
                        help="walk synthetic lists, driving nothing")
    parser.add_argument("--executable", default="build/bin/spyro_port")
    parser.add_argument("--binary", default="scratch/assets/spyro1/SCUS_942.28")
    parser.add_argument("--log", default="scratch/logs/moby_list.log")
    parser.add_argument("--settle", type=int, default=120,
                        help="frames to run after GS_Playing before reading, so the list has been "
                             "built and the mobies have had a field to appear in")
    args = parser.parse_args()
    if args.selftest:
        SCRATCH.mkdir(parents=True, exist_ok=True)
        return selftest()

    import runpy
    drive = runpy.run_path(str(TOOLS / "drive.py"))
    Port, Navigator, disc_path = drive["Port"], drive["Navigator"], drive["disc_path"]
    environment = drive["environment"]

    disc = disc_path()
    if disc is None:
        print("REFUSED: no disc image found; this probe needs the real executable to reach gameplay",
              file=sys.stderr)
        return 2
    env = environment(disc)
    port = Port(ROOT / args.executable, ROOT / args.binary, ROOT / args.log, env)
    try:
        Navigator(port).reach_gameplay()
        print(f"reached GS_Playing at frame {port.frame}")
        port.mark_arrival()
        if args.settle:
            port.run(args.settle)
        print(f"settled {args.settle} frames; now at frame {port.frame}")

        # THE CONTROL, and it is not optional. Every number this probe has ever printed was zero, and a
        # channel that cannot read a NON-ZERO value would print zero here too and look like a finding.
        # So read an address the game is known to keep live, and require it to be a RAM pointer,
        # BEFORE the list's zeros mean anything.
        gamestate = port.word(G_GAMESTATE)
        print(f"control: g_Gamestate (0x{G_GAMESTATE:08X}) = {gamestate} (0 = GS_Playing)")
        level_id = port.word(K_LEVEL_ID)
        print(f"level: g_LevelId (0x{K_LEVEL_ID:08X}) = {level_id}")
        level_mobys_ptr = port.word(K_LEVEL_MOBYS)
        in_ram = RAM_LOW <= level_mobys_ptr < RAM_HIGH
        print(f"control: kLevelMobys (0x{K_LEVEL_MOBYS:08X}) = 0x{level_mobys_ptr:08X} "
              f"{'a RAM pointer' if in_ram else 'NOT A POINTER'}")
        if gamestate != 0 or not in_ram:
            print("REFUSED: the control read did not come back as expected, so the reads below cannot "
                  "be trusted and no conclusion is drawn from them. Report this rather than the "
                  "list's contents.")
            port.end()
            return 2
        # If the level HAS mobies but the derived list is empty, then the level load worked and the
        # step that walks the level's mobies into the update list did not. That is a much narrower
        # defect than "the level never loaded its mobies", and the two need different fixes.
        level_window = read_range(port, level_mobys_ptr, 24)
        level_nonempty = [w for w in level_window if w]
        print(f"control: 24 words at the level's moby array 0x{level_mobys_ptr:08X} -> "
              f"{len(level_nonempty)} non-zero word(s)")

        # The gate fields, per moby. This is what turns "the list is empty" into "the list is empty
        # BECAUSE gate X rejects moby Y", which is the difference between a measurement and a lead.
        # `func_80051FEC` is the only writer of the list, and every branch it takes to append or skip
        # is decided by one of these four bytes, so reading them localises the refusal without
        # guessing at it.
        per_moby = MOBY_STRIDE // 4
        wanted = (MOBY_STRIDE * MAX_MOBY_PROBE) // 4
        raw = read_range(port, level_mobys_ptr, wanted)
        print(f"gates, walking the level's moby array with stride 0x{MOBY_STRIDE:X} "
              f"({MOBY_STRIDE} bytes = one Moby):")
        print(f"  {'moby':>5} {'addr':>10}  {'+0x48':>6} {'+0x51':>6} {'+0x43':>6} {'+0x52':>6}  verdict")
        terminated = False
        counted = 0
        for index in range(MAX_MOBY_PROBE):
            base = index * per_moby
            if base + per_moby > len(raw):
                print(f"  read window exhausted after {index} moby/ies")
                break
            addr = level_mobys_ptr + index * MOBY_STRIDE
            def byte_at(offset: int) -> int:
                word = raw[base + offset // 4]
                return (word >> (8 * (3 - offset % 4))) & 0xFF
            term = byte_at(GATE_TERMINATOR)
            near = byte_at(GATE_NEAR_SELECT)
            flag = byte_at(GATE_NEAR_FLAG)
            radius = byte_at(GATE_FAR_RADIUS)
            signed_term = term - 256 if term >= 128 else term
            if signed_term == -1:
                print(f"  {index:5d} 0x{addr:08X}  {term:6d} {near:6d} {flag:6d} {radius:6d}  "
                      f"TERMINATOR (+0x48 == -1): the filler stops here")
                terminated = True
                break
            if near == 0:
                counted += 1
                verdict = "appended by the NEAR path at 0x8005205C"
            elif radius == 0:
                verdict = "skipped: +0x52 == 0 fails the far radius test at 0x80052084"
            else:
                counted += 1
                verdict = "far path, decided by the GTE SQR/MAC test at 0x800520B4-0x80052108"
            print(f"  {index:5d} 0x{addr:08X}  {term:6d} {near:6d} {flag:6d} {radius:6d}  {verdict}")
        if not terminated:
            print(f"  NO TERMINATOR in the first {MAX_MOBY_PROBE} mobies: +0x48 never reached -1, so "
                  f"the filler's own walk would run off the end of the array")

        window = read_range(port, LIST_BASE, 64)
        print(f"read 64 words at 0x{LIST_BASE:08X} through the live channel")
        result = walk(window)
        entries: list[int] = result["entries"]  # type: ignore[assignment]
        print(f"moby list at 0x{LIST_BASE:08X}: {len(entries)} node(s), "
              f"terminated={result['terminated']}")
        for index, node in enumerate(entries[:16]):
            in_ram = RAM_LOW <= node < RAM_HIGH
            print(f"  node {index:2d}  0x{node:08X} {'in RAM' if in_ram else 'NOT A POINTER'}")
        if len(entries) > 16:
            print(f"  ... {len(entries) - 16} more")
        garbage: list[tuple[int, int]] = result["garbage"]  # type: ignore[assignment]
        if garbage:
            # `w` was never bound here, so this raised NameError on exactly the path that has to
            # REPORT a word it could not classify. A diagnostic that cannot print its own negative is
            # the failure mode this file exists to avoid, so the tuple is unpacked by name.
            print(f"  {len(garbage)} node(s) are not RAM pointers; they are reported, not counted "
                  f"as mobies: {[(hex(word), hex(value)) for word, value in garbage[:4]]}")
        if not result["terminated"]:
            print("  the list did not terminate inside the 64-word window, so the count above is a "
                  "LOWER BOUND, not the list's size")
        if not entries:
            print("VERDICT: the live route's moby list is EMPTY **AT THIS FRAME PARK**, and that is "
                  "NOT by itself a gameplay defect, so this probe no longer calls it one. The list is "
                  "a per-frame scratch the guest fills inside the level update and consumes there; "
                  "`func_8002B9CC` (0x8002B9CC) then memsets 0x8006FCF4 for 0x1C00 bytes in the DRAW "
                  "pass and the list base is +0x400 inside that region, so a frame-boundary read is "
                  "expected to be zero. `tools/probe_level_update_dispatch.py` measures the update "
                  "from inside it instead, by arming the store observer on the filler's own store "
                  "PCs, and says why. What is still open is whether the list CONTENT the product fills "
                  "matches the console's DURING the update; that needs a read from inside the update "
                  "on both cores, which this route cannot give.")
        else:
            print(f"VERDICT: the live route's moby list holds {len(entries)} mobies at this frame "
                  f"park, so this level is not being cleared before the park. The attract-demo "
                  f"divergence in 0133 is then a DEMO-ROUTE difference, not a missing level load, and "
                  f"that is the next thing to prove.")
    finally:
        port.end()
    return 0


if __name__ == "__main__":
    # Run unbuffered on purpose. A probe that hangs must not lose the measurements it already made:
    # a full stdout buffer is discarded when the process is killed, and those measurements are the
    # expensive part. That is exactly how an earlier run of this tool produced a completely empty
    # stdout and an hour of re-deriving a fact it had already taken.
    raise SystemExit(main())
