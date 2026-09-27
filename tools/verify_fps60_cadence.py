#!/usr/bin/env python3
"""Does the 60fps feature actually PRESENT at 60 Hz, and is the motion between frames real?

S020's evidence until now was an EMITTER SELF-REPORT: a live run with `fps60` on whose per-interval
ownership gate reported nonzero interpolated face emission (`faces=156..165 ... => PASS`). That shows
midpoints are computed and drawn. It does not show the product PRESENTS them, and it cannot tell a
correct midpoint from a duplicated endpoint, because both emit the same nonzero face count.

This measures the two things the "60fps" claim actually rests on, over the live control channel, and it
measures them with a CONTROL LEG because neither number means anything alone:

1. **CADENCE.** Presented frames (`frame` on the endpoint) against guest game ticks (`g_GameTick` read
   from the running guest) over a window, while the game is being played with real held input. A product
   that computes midpoints and then presents at 30 Hz reports the same ratio as one that presents them.
2. **MOTION IS REAL, NOT A DUPLICATE.** Consecutive presented frames captured through `shot` are
   compared byte for byte. A run whose feature is on but whose "interpolated" frames duplicate their
   endpoint is presenting 60 distinct frame NUMBERS and 30 distinct pictures, and byte comparison is
   what says so.

The control leg is `tools/fps60_control_settings.ini`: the same file with `fps60=0` and 4:3, so a width
change cannot move the presented-frame count. If both legs report the same cadence, the feature is not
presenting faster and this tool says so rather than reporting the shipped leg on its own.

## What this deliberately does NOT claim

- Not that the interpolated geometry is CORRECT, only that it is DISTINCT from its endpoints. Correctness
  is a per-object question against the captured endpoint pair, and S020's remaining gap.
- Not oracle parity. That is `tools/oracle_compare.py`, and it is a separate gate: guest state must be
  unperturbed, which a cadence measurement cannot show.
- Not a frame-time or pacing claim. Presented-frame count per guest tick is a ratio of two counters, not
  a wall-clock measurement.

## Why it drives the real product

Boot, logos and menus do not establish anything about frame cadence, and neither does a static trace.
This launches the shipping product headless, walks the documented route to `GS_Playing`, holds real pad
input through the endpoint, and reads the counters and the presented pixels from the running process. A
route it cannot reach is a REFUSAL, not a pass.
"""

from __future__ import annotations

import argparse
import filecmp
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools"))

import drive  # noqa: E402
import guest_globals  # noqa: E402
import live_play  # noqa: E402
import title_prompts  # noqa: E402
from dbgclient import LiveClient  # noqa: E402

SHIPPING = ROOT / "tools" / "shipping_settings.ini"
CONTROL = ROOT / "tools" / "fps60_control_settings.ini"
SHOTS = ROOT / "scratch" / "fps60_cadence"


def sustained_cadence(session: live_play.Session, *, windows: int, updates_per_window: int) -> dict:
    """Presented frames per guest UPDATE, over several equal windows, with input held across all of them.

    Two things this had to get right, both found by the instrument refusing its own first result.

    **The window is defined by guest UPDATES, not by wall clock.** A wall-clock window measures a mixture of
    how fast the guest is updating and how fast the product presents, and on this title the guest's
    update rate moves by more than an order of magnitude between game states — the first version of this
    tool reported per-window rates of 1.00, 1.62 and 5.96 and correctly refused to call any of them a
    cadence. The quantity the "60fps" claim is about is presented frames per guest update, so each window
    is a fixed number of UPDATES, and the two counters are read at the same two instants.

    **Input is held across every window, not per window.** Releasing and re-pressing between windows
    changes the game state under the measurement, which is the mechanism behind the spread above. One
    press, W windows, one release.

    A window in which the player's position did not change is a REFUSAL, not a sample: the guest is not
    being played there and its ratio is not a cadence.
    """
    client = session.client
    client.press("left")
    try:
        samples: list[dict] = []
        for index in range(windows):
            ticks_before = client.word(guest_globals.kGameTick)
            # The TOTAL present counter, not `client.frame()`. That was the first version's bug and it
            # produced a confident, wrong answer: `frame` counts REAL presents, and an interpolated
            # in-between reaches the screen without advancing it, so the shipped leg read 1.077 per guest
            # update while the same run emitted 3,915 in-betweens. Both numbers were true. The feature
            # adds frames this counter never moved for, so the ratio has to count them.
            before = client.frames()
            position_before = client.words(guest_globals.kSpyro, 3)
            deadline = time.monotonic() + 120.0
            ticks = 0
            while ticks < updates_per_window:
                ticks = client.word(guest_globals.kGameTick) - ticks_before
                if time.monotonic() > deadline:
                    raise SystemExit(
                        f"REFUSED: window {index} advanced only {ticks} of {updates_per_window} guest "
                        f"updates in 120 s, so there is no interval to measure"
                    )
                time.sleep(0.002)
            after = client.frames()
            position_after = client.words(guest_globals.kSpyro, 3)
            if position_before == position_after:
                raise SystemExit(
                    f"REFUSED: window {index} held real input and the player's position did not change "
                    f"({position_before} -> {position_after}), so the game was not being played there and "
                    f"this window's ratio is not a cadence"
                )
            real = after["frame"] - before["frame"]
            inbetweens = after["interp"] - before["interp"]
            total = after["total"] - before["total"]
            samples.append({
                "window": index,
                "real_presents": real,
                "interpolated_presents": inbetweens,
                "presented_frames": total,
                "guest_updates": ticks,
                "presented_per_guest_tick": round(total / ticks, 4),
                "real_per_guest_tick": round(real / ticks, 4),
                "interpolated_per_guest_tick": round(inbetweens / ticks, 4),
                "player_before": position_before,
                "player_after": position_after,
            })
    finally:
        client.release("left")
    rates = [s["presented_per_guest_tick"] for s in samples]
    return {
        "windows": samples,
        "updates_per_window": updates_per_window,
        "presented_per_guest_tick_min": min(rates),
        "presented_per_guest_tick_max": max(rates),
        "presented_per_guest_tick": round(sum(rates) / len(rates), 4),
    }


def consecutive_shots(client: LiveClient, count: int, tag: str) -> list[Path]:
    """Capture `count` consecutive PRESENTATIONS and return their paths in order.

    The wait is on the TOTAL present counter, not the real one. Waiting on the real counter advances a
    whole logic frame and therefore SKIPS the in-between, so the captures would be real-after-real and
    the one frame the feature exists to add would never be sampled — which is the first version's second
    bug, after the numerator. On a 60 Hz leg roughly half of what this captures is an interpolated
    presentation, and whether that picture DIFFERS from its neighbour is the motion question.

    Each capture reads whatever is on screen once the presenter has moved on, so a duplicated in-between
    shows up as two identical files.
    """
    SHOTS.mkdir(parents=True, exist_ok=True)
    paths: list[Path] = []
    kinds: list[int] = []
    for index in range(count):
        before = client.frames()
        target = before["total"] + 1
        deadline = time.monotonic() + 60.0
        while client.frames()["total"] < target:
            if time.monotonic() > deadline:
                raise SystemExit(
                    f"REFUSED: capture {index} waited 60 s for presentation {target} and the product "
                    f"reached only {client.frames()['total']}, so the presentations are not consecutive"
                )
            time.sleep(0.001)
        after = client.frames()
        kinds.append(after["interp"] - before["interp"])
        path = SHOTS / f"{tag}_{index:03d}.png"
        reply = client.send(f"shot {path}")
        if "->" not in reply:
            raise SystemExit(f"REFUSED: the endpoint did not capture a presented frame: {reply!r}")
        paths.append(path)
    return paths, kinds


def distinct_pairs(paths: list[Path]) -> dict:
    """How many CONSECUTIVE presented frames are byte-identical, over the pairs that exist.

    `filecmp.cmp(shallow=False)` rather than a hash, so a truncated or unreadable capture is a mismatch
    rather than a silent pass, and the count is reported against the number of pairs actually taken
    instead of against `count`.
    """
    if len(paths) < 2:
        raise SystemExit(f"REFUSED: {len(paths)} capture(s) is not enough to compare a pair")
    identical = [
        index
        for index in range(len(paths) - 1)
        if filecmp.cmp(paths[index], paths[index + 1], shallow=False)
    ]
    pairs = len(paths) - 1
    return {
        "captures": len(paths),
        "pairs": pairs,
        "identical_consecutive_pairs": len(identical),
        "identical_at": identical,
        "all_captures_distinct": not identical,
    }


def leg(name: str, settings: Path, port: int, *, windows: int, updates_per_window: int,
        shots: int) -> dict:
    """One measured leg: launch, walk the route to GS_Playing, play it, and measure."""
    log = ROOT / "scratch" / "logs" / f"fps60-{name}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    product = live_play.launch(ROOT / "build" / "bin" / "spyro_port",
                               ROOT / "scratch" / "assets" / "spyro1" / "SCUS_942.28",
                               drive.disc_path(), port, log, settings=settings)
    try:
        client = live_play.connect(port, 180.0)
        session = live_play.Session(client, log)
        spent = session.drive_to_gameplay(12000)
        knobs = live_play.effective_configuration(client)
        fps60 = knobs["knobs"].get("PSXPORT_FPS60")
        if fps60 is None:
            raise SystemExit(
                "REFUSED: the product reports no PSXPORT_FPS60 cvar, so this run cannot say whether it "
                "was testing the feature on or off"
            )
        result = {
            "leg": name,
            "settings": str(settings.relative_to(ROOT)),
            "PSXPORT_FPS60": fps60,
            "PSXPORT_FPS60_layer": knobs["layers"].get("PSXPORT_FPS60"),
            "route_frames_spent": spent,
            "cadence": sustained_cadence(session, windows=windows,
                                         updates_per_window=updates_per_window),
        }
        captured, kinds = consecutive_shots(client, shots, name)
        result["captures"] = [str(p.relative_to(ROOT)) for p in captured]
        result["capture_was_in_between"] = kinds
        result["motion"] = distinct_pairs(captured)
        result["guest"] = live_play.guest_execution(client)
        return result
    finally:
        product.terminate()
        try:
            product.wait(timeout=30)
        except subprocess.TimeoutExpired:  # pragma: no cover - only on a wedged product
            product.kill()
            product.wait(timeout=30)


def verdict(shipped: dict, control: dict) -> tuple[int, list[str]]:
    """Decide from BOTH legs. Reporting the shipped leg alone is how a product that computes midpoints
    and presents them at 30 Hz would be recorded as working."""
    lines: list[str] = []
    ship = shipped["cadence"]
    ctrl = control["cadence"]
    for leg_result in (shipped, control):
        rate = leg_result["cadence"]
        lines.append(
            f"[cadence] {leg_result['leg']:8s} PSXPORT_FPS60={leg_result['PSXPORT_FPS60']:5s} "
            f"[{leg_result['PSXPORT_FPS60_layer']}]: {rate['presented_per_guest_tick']} presented frames "
            f"per guest update, per window "
            f"{[w['presented_per_guest_tick'] for w in rate['windows']]} "
            f"over {len(rate['windows'])} held-input window(s)"
        )
    failures = 0
    if shipped["PSXPORT_FPS60"] != "true":
        lines.append("[cadence] REFUSED: the shipped leg did not have the feature on, so it measures nothing")
        failures += 1
    if control["PSXPORT_FPS60"] != "false":
        lines.append("[cadence] REFUSED: the control leg did not have the feature off, so there is no control")
        failures += 1
    for leg_result in (shipped, control):
        for window in leg_result["cadence"]["windows"]:
            if window["presented_per_guest_tick"] is None:
                lines.append(f"[cadence] REFUSED: {leg_result['leg']} window {window['window']} advanced "
                             f"no guest tick, so the ratio is undefined")
                failures += 1
    if failures:
        return 1, lines
    # The spread is the instrument's own honesty check. A real cadence feature presents the same number
    # in every window; a ratio that depends on the game state is not a cadence measurement.
    for leg_result in (shipped, control):
        rate = leg_result["cadence"]
        spread = rate["presented_per_guest_tick_max"] - rate["presented_per_guest_tick_min"]
        lines.append(f"[cadence] {leg_result['leg']:8s} spread across windows: {spread:.4f}")
        if spread > 0.5:
            lines.append(
                f"[cadence] {leg_result['leg']}'s ratio moves by {spread:.4f} between windows, so it is "
                f"tracking the game state rather than a presentation rate and the average is not a cadence"
            )
            failures += 1
    gained = shipped["cadence"]["presented_per_guest_tick"] - control["cadence"]["presented_per_guest_tick"]
    lines.append(f"[cadence] the feature GAINS {gained:.4f} presented frames per guest UPDATE")
    if gained < 0.9:
        lines.append(
            f"[cadence] the feature gains {gained:.4f}, short of the ~1.0 that one extra presentation per "
            f"guest tick would be. Midpoints may be computed and still not presented, and the emitter's "
            f"own gate cannot tell those apart"
        )
        failures += 1
    for leg_result in (shipped, control):
        motion = leg_result["motion"]
        lines.append(
            f"[motion] {leg_result['leg']:8s}: {motion['identical_consecutive_pairs']} of "
            f"{motion['pairs']} consecutive presented-frame pairs byte-identical"
        )
    in_betweens = sum(1 for k in shipped.get("capture_was_in_between", []) if k)
    lines.append(f"[motion] shipped : {in_betweens} of {len(shipped.get('capture_was_in_between', []))} "
                 f"captured presentations were in-betweens")
    if not shipped["motion"]["all_captures_distinct"]:
        lines.append(
            "[motion] the shipped leg presented byte-identical consecutive frames, so its frame NUMBERS "
            "advance while its PICTURES do not. That is a 60 Hz counter over a 30 Hz picture"
        )
        failures += 1
    return (1 if failures else 0), lines


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=5981, help="live endpoint port (default 5981)")
    parser.add_argument("--windows", type=int, default=4,
                        help="held-input windows the cadence is sampled over (default 4)")
    parser.add_argument("--updates-per-window", type=int, default=120,
                        help="guest updates per window (default 120)")
    parser.add_argument("--shots", type=int, default=8,
                        help="consecutive presented frames captured per leg (default 8)")
    parser.add_argument("--leg", choices=("shipped", "control", "both"), default="both")
    parser.add_argument("--out", type=Path, default=ROOT / "scratch" / "fps60_cadence" / "report.json")
    args = parser.parse_args(argv)

    if not drive.disc_path():
        print("REFUSED: no disc; set PSXPORT_SPYRO_DISC in the environment or .env", file=sys.stderr)
        return 2

    results: dict[str, dict] = {}
    try:
        if args.leg in ("shipped", "both"):
            results["shipped"] = leg("shipped", SHIPPING, args.port, windows=args.windows,
                                     updates_per_window=args.updates_per_window, shots=args.shots)
        if args.leg in ("control", "both"):
            results["control"] = leg("control", CONTROL, args.port + 1, windows=args.windows,
                                     updates_per_window=args.updates_per_window, shots=args.shots)
    finally:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(results, indent=2, default=str))

    if "shipped" in results:
        for line in verdict(results["shipped"], results.get("control", {"cadence": {"presented_per_guest_tick": None, "presented_per_guest_tick_min": None,
                                "presented_per_guest_tick_max": None, "windows": []},
                "PSXPORT_FPS60": "ABSENT", "PSXPORT_FPS60_layer": "-", "motion": {
                    "identical_consecutive_pairs": 0, "pairs": 0, "all_captures_distinct": False},
                "leg": "control"}))[1]:
            print(line)
    print(f"[report] {args.out}")
    if "shipped" in results and "control" in results:
        status, _ = verdict(results["shipped"], results["control"])
        return status
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
