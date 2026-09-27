"""Does pressing Start during the level transition abort the renderer?

WHY THIS FILE EXISTS
--------------------
Two operator reports point at one thing. First: "start should skip transition scenes like this one
(In The World Of) but it doesn't work". Second, a backtrace:

    SpyroRenderer::renderScene -> SpyroRenderer::abortUnimplemented -> abort (SIGABRT)
    [render:error] NATIVE RENDER NOT IMPLEMENTED - stage selector = N (...)

Neither the gameplay route nor the portal route aborts; both reach `level_transition` and exit 0. The one
thing neither does is **press Start**, and Start is what dispatches `spyro1::TransitionSkip`, which hands
control back to the guest by dispatching the very functions the screen's own owner calls when it ends
naturally (`func_8004AC24` then `LoadLevel` at `0x80015370`). So the working hypothesis is specific: the
skip leaves the guest in a state whose scene has no registered producer, and the renderer's deliberate
fail-loud turns that into an abort.

That is a worse defect than "the skip does not work", and it is the kind that reads as two unrelated bugs.

WHAT IT DOES, AND WHAT IT REFUSES TO DO
---------------------------------------
Drives a real portal crossing through `tools/drive.py`'s own machinery, watches `g_Gamestate` every frame,
and taps Start on the first frame the level-transition stage is up. It then keeps running and reports what
the renderer did.

It NEVER writes a guest byte, never disables the fail-loud, and never weakens it. A renderer that refuses to
present a scene it cannot draw is behaving correctly; the defect is that the port can put the guest into
that state. So the fix belongs in the skip, not in the refusal.

THE CONTROL
-----------
Without the Start press, over the same route and the same number of frames, this must reach the same
transition and NOT abort. An experiment that only has the failing arm cannot tell a skip defect from a
route defect, and the route has already been shown to be clean twice. So the no-press arm is part of the
result, not a formality.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

import drive  # noqa: E402
import title_prompts  # noqa: E402

G_GAMESTATE = drive.G_GAMESTATE
G_LEVEL_TRANS_HUD = drive.G_LEVEL_TRANS_HUD
# Spyro's stage numbers, from the guest: 1 is GS_LevelTransition (the tally / intro card the operator's
# screenshot shows), 10 is GS_ExitLevel (the return-home glide), 13 is GS_TitleScreen (the flyby card).
STAGE_LEVEL_TRANSITION = 1
ABORT_RE = re.compile(r"NATIVE RENDER NOT IMPLEMENTED|stage selector|fatal boundary", re.IGNORECASE)


def census(port: drive.Port) -> dict[int, int]:
    return dict(port.gamestate_census)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--press", action="store_true",
                    help="tap Start on the first frame the level-transition stage is up (the FAILING arm)")
    ap.add_argument("--watch-frames", type=int, default=400,
                    help="frames to keep running after the tap (default: 400)")
    ap.add_argument("--log", default="scratch/logs/skip_start_probe.log")
    args = ap.parse_args()

    env = drive.environment(drive.disc_path(), TOOLS / "shipping_settings.ini")
    log = TOOLS.parent / args.log
    port = drive.Port(TOOLS.parent / "build" / "bin" / "spyro_port",
                      TOOLS.parent / "scratch" / "assets" / "spyro1" / "SCUS_942.28",
                      log, env)
    pressed_at = None
    try:
        print(f"[skip] arm: {'PRESS Start during the transition' if args.press else 'CONTROL, no press'}")
        navigator = drive.Navigator(port)
        navigator.reach_gameplay()
        print(f"[skip] reached gameplay; now walking to a portal to cross it")

        # Cross a REAL portal, with the same Seeker the `--seek-portal` route uses, so this arm and the
        # route that already passed twice are the same walk. The seek stops when `g_LevelId` changes,
        # which is the instant the transition begins.
        entering = port.word(drive.G_LEVEL_ID)
        drive.Seeker(port, "portal", drive.portal_targets(port.words), arrived=0,
                     stop=lambda: port.word(drive.G_LEVEL_ID) != entering,
                     stop_is=f"left level {entering}").walk()
        crossed = False
        for _ in range(120):
            if port.word(G_GAMESTATE) == STAGE_LEVEL_TRANSITION:
                crossed = True
                break
            port.run(2)
        if not crossed:
            print(f"[skip] the walk did not reach the level-transition stage within its budget; the route "
                  f"is not testing the thing under test. gamestate census so far: {census(port)}")
            return 2
        print(f"[skip] level-transition stage is up (g_Gamestate={STAGE_LEVEL_TRANSITION}); "
              f"tally HUD active={port.word(G_LEVEL_TRANS_HUD)}")

        if args.press:
            port.tap("start", frames=4)
            pressed_at = port.frame
            print(f"[skip] tapped Start at frame {pressed_at}")

        port.run(args.watch_frames)

        # What the guest looks like AFTER the press, which is what the renderer has to draw.
        final_state = port.gamestate()
        print(f"[skip] after {args.watch_frames} frames: g_Gamestate={final_state} "
              f"tally HUD={port.word(G_LEVEL_TRANS_HUD)}")
        print(f"[skip] gamestate census over the whole run: {census(port)}")

        text = log.read_text(errors="replace") if log.is_file() else ""
        aborted = bool(ABORT_RE.search(text))
        print(f"[skip] renderer abort present in the run log: {aborted}")
        if aborted:
            for line in text.splitlines():
                if ABORT_RE.search(line):
                    print(f"[skip]   {line.strip()[:150]}")
        status = 1 if aborted else 0
        print(f"[skip] VERDICT: {'ABORTED' if aborted else 'no abort'}"
              f"{' after a Start press' if args.press else ' (control)'}")
        return status
    finally:
        try:
            port._proc.kill()
            port._proc.wait(timeout=10)
        except Exception:  # noqa: BLE001 - the port is killed on every exit path
            pass


if __name__ == "__main__":
    raise SystemExit(main())
