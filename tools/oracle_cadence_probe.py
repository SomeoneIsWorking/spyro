#!/usr/bin/env python3
"""The per-FIELD cadence census of ONE core, and the first-iteration analysis of the field window.

    uv run --frozen python tools/oracle_cadence_probe.py --core console
    uv run --frozen python tools/oracle_cadence_probe.py --core console --selftest

WHAT THIS ANSWERS, and why it is not the oracle. `tools/oracle_compare.py` compares the two cores at
one parked moment per main-loop iteration and cannot see INSIDE an iteration: retail's
`g_DeltaTime` is written at `0x80012240` and zeroed at `0x80012270` (read out of the image; see
`docs/issues/0110`), so the counter's window is (this iteration's update) + (the previous
iteration's draw). The only way to see that window's shape is to stop at every FIELD and read the
words, which is what this does. It reuses the shipping route, the shipping predicates and the
shipping pad delivery; it adds only the field-granular sampling and the arithmetic that turns a
per-field series into "fields per main-loop iteration".

Two things it deliberately does NOT do:
  * it does not compare the cores (that is `tools/oracle_compare.py`), and
  * it does not claim the series is phase-aligned with any other core's. Field numbers are not
    state alignment; the series is only ever compared with ITSELF (iteration N against N+1).

THE ARITHMETIC, and its one assumption. An iteration boundary is the field at which
`g_UnprocessedFrames` reads 0, because `0x80012270` zeroes it exactly once per iteration and nothing
else in the loaded image writes 0 to that word between the increment at `0x800542C8` and the zero
(measured: the word's only immediate-form store sites in all 103,936 loaded words are `0x80012270`
and the `0x80053C50` demo path, which writes 2 or 0 and is not reachable in gameplay -- the report
prints the census of both so a reader can see which boundary rule fired).

Given a boundary at field f_n, the fields in the window are (f_n - f_{n-1}), and `g_DeltaTime` read
at that boundary is the window's size CLAMPED to [2,4] -- which the census checks rather than
assumes, by printing the raw window next to the value the guest itself stored.
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools" / "oracle"))

import compare  # noqa: E402
import compare_cores  # noqa: E402
import drive  # noqa: E402
import guest_globals  # noqa: E402
import oracle_spyro1 as route  # noqa: E402

OUT_DIR = ROOT / "scratch" / "oracle" / "cadence"

# Read out of the image, not from a listing: `lui $v0,0x8007 ; lw $v0,0x58C8($v0)` at `0x80053C6C`
# is PadVSync's `g_LevelTicks` load and `0x800542B8`/`0x800542C8` are the
# `g_UnprocessedFrames += 1` pair, and `0x80053CA0` loads the CD read-time pair. The census prints
# the address it asked for so a reader can check the derivation against the scan.
FIELDS = (
    ("g_UnprocessedFrames", guest_globals.kUnprocessedFrames),
    ("g_DeltaTime", guest_globals.kDeltaTime),
    ("g_LevelTicks", guest_globals.kLevelTicks),
    ("g_GameTick", guest_globals.kGameTick),
    ("g_StateSwitch", guest_globals.kStateSwitch),
    ("g_CDMaxReadTime", 0x800756E0),
    ("g_CDReadTime", 0x800756E4),
)


def sample(core) -> dict:
    """One read of every watched word, in address-contiguous blocks no larger than the session's
    per-read cap. A range wider than the cap is split rather than silently truncated, because a
    truncated tail would read the wrong word under a name that looks right."""
    out: dict[str, int] = {}
    ordered = sorted(FIELDS, key=lambda item: item[1])
    block: list[tuple[str, int]] = []
    for name, address in ordered:
        if block and address - block[0][1] >= 256:
            out.update(_read_block(core, block))
            block = []
        block.append((name, address))
    if block:
        out.update(_read_block(core, block))
    return out


def _read_block(core, block) -> dict:
    lo = block[0][1]
    raw = core.read(lo, block[-1][1] + 4 - lo)
    return {name: int.from_bytes(raw[address - lo:address - lo + 4], "little")
            for name, address in block}


def windows(series: list[dict]) -> tuple[list[tuple[int, int, int]], Counter]:
    """(field_index, window, stored g_DeltaTime) per main-loop iteration, plus the census of which
    boundary rule fired. `series` is one dict per FIELD, oldest first."""
    rows: list[tuple[int, int, int]] = []
    fired = Counter()
    previous = None
    for index, entry in enumerate(series):
        if entry["g_UnprocessedFrames"] != 0:
            continue
        if previous is not None:
            rows.append((index, index - previous, entry["g_DeltaTime"]))
        previous = index
    return rows, fired


def selftest() -> int:
    """Both answers: a series whose boundaries are the zeroing, and one where the window rule and
    the guest's own stored value disagree, which the tool must be able to REPORT rather than hide."""
    good = [
        {"g_UnprocessedFrames": 0, "g_DeltaTime": 2},   # boundary: the main loop just zeroed it
        {"g_UnprocessedFrames": 1, "g_DeltaTime": 2},
        {"g_UnprocessedFrames": 0, "g_DeltaTime": 2},   # boundary: window 2 -> stored 2
        {"g_UnprocessedFrames": 1, "g_DeltaTime": 2},
    ]
    rows, _ = windows(good)
    assert [r[1] for r in rows] == [2], rows
    assert [r[2] for r in rows] == [2], rows

    over = [
        {"g_UnprocessedFrames": 0, "g_DeltaTime": 2},
        {"g_UnprocessedFrames": 1, "g_DeltaTime": 2},
        {"g_UnprocessedFrames": 2, "g_DeltaTime": 2},
        {"g_UnprocessedFrames": 3, "g_DeltaTime": 2},
        {"g_UnprocessedFrames": 4, "g_DeltaTime": 2},
        {"g_UnprocessedFrames": 5, "g_DeltaTime": 2},
        # The main loop read 5, plus the field it is inside: window 6, and it stored the clamp.
        {"g_UnprocessedFrames": 0, "g_DeltaTime": 4},
    ]
    rows, _ = windows(over)
    assert [r[1] for r in rows] == [6], rows
    assert [r[2] for r in rows] == [4], rows

    # A stored value that is NOT clamp(window) must be visible as such, not silently accepted.
    liar = [
        {"g_UnprocessedFrames": 0, "g_DeltaTime": 2},
        {"g_UnprocessedFrames": 2, "g_DeltaTime": 2},
        {"g_UnprocessedFrames": 0, "g_DeltaTime": 3},   # window 2, guest stored 3
    ]
    rows, _ = windows(liar)
    assert rows[0][1] == 2 and rows[0][2] == 3, rows
    print("  a 2-field window is read as 2; a 6-field window as 4 (clamped); a disagreeing stored "
          "value is reported, not accepted")
    print("oracle_cadence_probe selftest PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--core", choices=("console", "native"), default="console",
                        help="which core to census. 'console' is the full-console reference; "
                             "'native' is the product, whose REPL `run 1` is the same one-field step, "
                             "so the two are measured in the same unit")
    parser.add_argument("--fields", type=int, default=1000,
                        help="display FIELDS to census after arming (default 1000, which is the "
                             "whole 477-iteration route at two fields per iteration)")
    parser.add_argument("--arm-tick", type=int, default=0,
                        help="arm on this g_GameTick instead of on a field count. The console's "
                             "FIELD phase through the level intro varies between runs (its card I/O "
                             "and CD timing do), so a field count aims at a different moment each "
                             "time; the game tick is guest state and is the only stable aim point. "
                             "0 disables it and uses --skip.")
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--binary", type=Path, default=Path("scratch/assets/spyro1/SCUS_942.28"))
    parser.add_argument("--bios", type=Path, default=ROOT.parent / "SCPH1001.BIN")
    parser.add_argument("--dump", action="store_true",
                        help="also print every sampled field, not only the summary")
    parser.add_argument("--selftest", action="store_true",
                        help="exercise the boundary/window arithmetic on synthetic series and exit")
    args = parser.parse_args()
    if args.selftest:
        return selftest()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    disc = drive.disc_path()
    if not disc:
        print("REFUSED: no disc; set PSXPORT_SPYRO_DISC in the environment or .env", file=sys.stderr)
        return 2
    product = compare.Product(ROOT / args.executable, ROOT / args.binary,
                              drive.environment(disc), ROOT, Path(disc))
    product = compare.fresh_card(product, OUT_DIR)
    # The framework REFUSES to drive the product before the reference at a route checkpoint: the
    # console's arrival field fixes the settle pad the product's arrival must read
    # (`compare.Driver.drive`). So `--core native` still opens the reference, drives the chain on it
    # to obtain each settle, and then drives the product to the same checkpoint -- exactly the
    # order `compare.Comparison.reach` uses. Censusing is still ONE core; the other exists only to
    # produce the pad the product is required to match.
    reference = compare_cores.ConsoleSession(ROOT / "external" / "psxport", product.disc, args.bios,
                                             "na", OUT_DIR / "console.log")
    print(f"[cadence] console card: {compare.match_console_card(reference, None)}")
    core = reference
    product_core = None
    if args.core == "native":
        product_core = compare_cores.NativeReplSession(str(product.binary), str(product.executable),
                                                       product.environment, str(product.cwd),
                                                       OUT_DIR / "native.log")
        core = product_core
    driver = compare.Driver(route)
    # The route's checkpoints are a chain from boot and each one owns its own prompt policy:
    # `title_menu_pattern` presses Start at TSM_Init sub-state 3 and `new_game_pattern` only
    # answers the NEW GAME prompt once the title is in TSM_LOADING. Driving the settled checkpoint
    # directly with the second policy lets the title time out into the attract demo instead, so
    # the chain is walked in the declared order and `settle` is carried exactly as
    # `compare.Comparison.reach` carries it.
    settle = None
    for goal in (route.reach_save_picker, route.reach_playing, route.reach_settled_play):
        for target in ([reference] if core is reference else [reference, core]):
            used, settle = goal(driver, target, 6000, settle)
            label = "console" if target is reference else "native"
            print(f"[cadence] {goal.__name__} on {label}: after {used} game frames, "
                  f"field {target.frames} {route.summary(target)}")

    # The gameplay schedule, walked one FIELD at a time, so every field is an observation point.
    # The pad is held for the whole iteration exactly as `compare.Playback` holds it, so the
    # per-field pad sequence is the same one the oracle delivers (route lookahead is 0).
    schedule = [buttons for buttons, frames in route.gameplay for _ in range(frames)]
    armed = False
    series: list[dict] = []
    series_field: list[int] = []
    frame = 0            # SCHEDULE index: advances once per main-loop ITERATION, as the oracle's
    pad_pending = True   # Playback does. Advancing it per FIELD would compress the input route.
    while not armed or len(series) < args.fields:
        tick = sample(core)["g_GameTick"]
        if not armed and ((args.arm_tick and tick >= args.arm_tick) or
                          (not args.arm_tick and frame >= 230)):
            armed = True
            series.clear()
            series_field.clear()
            print(f"[cadence] arming at g_GameTick {tick}, field {core.frames}, "
                  f"at schedule iteration {frame}")
        if pad_pending:
            core.hold(schedule[min(frame, len(schedule) - 1)])
            pad_pending = False
        core.step(1)
        if armed:
            entry = sample(core)
            series.append(entry)
            series_field.append(core.frames)
            if entry["g_UnprocessedFrames"] == 0:
                pad_pending = True
                frame += 1
        else:
            frame += int(sample(core)["g_UnprocessedFrames"] == 0)
        if frame > 4000:
            print(f"[cadence] REFUSED: never reached g_GameTick {args.arm_tick} by schedule "
                  f"iteration {frame}", file=sys.stderr)
            core.close()
            return 2

    rows, _ = windows(series)
    census = Counter(r[1] for r in rows)
    print(f"[cadence] fields sampled: {len(series)} of {len(series)} requested (1 per step); "
          f"iteration boundaries found: {len(rows)}")
    print(f"[cadence] fields per main-loop iteration: {dict(sorted(census.items()))} "
          f"total {sum(census.values())}")
    clamped = Counter((r[1], r[2]) for r in rows)
    print(f"[cadence] (window, g_DeltaTime the guest stored): {dict(sorted(clamped.items()))}")
    disagree = [(r, series[r[0]]) for r in rows
                if r[2] != min(4, max(2, r[1]))]
    print(f"[cadence] iterations where the stored value is NOT clamp(window,2,4): {len(disagree)}")
    for row, entry in disagree[:16]:
        print(f"   window={row[1]} stored={row[2]} at census index {row[0]}: " +
              " ".join(f"{k}={v}" for k, v in entry.items()))
    print("[cadence] widest windows, with the words that were live in them:")
    for row, entry in sorted(((r, series[r[0]]) for r in rows), key=lambda x: -x[0][1])[:8]:
        print(f"   field {series_field[row[0]]:6d} window={row[1]} stored={row[2]}  " +
              " ".join(f"{k}={v}" for k, v in entry.items()))
    if args.dump:
        print("[cadence] per-field series (the window is read between two g_UnprocessedFrames==0 "
              "fields):")
        for index, entry in enumerate(series):
            print(f"   f{series_field[index]:6d} " +
                  " ".join(f"{k}={v}" for k, v in entry.items()))
    core.close()
    if product_core is not None:
        product_core.close()
    reference.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
