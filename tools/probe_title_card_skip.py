#!/usr/bin/env python3
"""probe_title_card_skip.py — does a held Start reach the GUEST on the title card, and does the
guest's OWN title-card fast-forward fire?

WHY THIS EXISTS. The operator reported that Start does not skip the "IN THE WORLD OF DRAGONS..."
card. Everything the port owns was checked first and none of it explains that, because the port
never had to own it: titlescreen.c:100-106 is RETAIL's own title-card skip, which fast-forwards
g_CutsceneLayout->m_CurrentTick to 1170 once the tick passes 300 while

    (g_Pad.m_Held & (PAD_START | PAD_CROSS)) != 0

is held. So the question is not "is a skip missing" but "does the press reach g_Pad.m_Held on that
screen". This probe answers exactly that, and separates the two ways it can fail:

  * the guest never sees the press   -> m_Held stays 0 while the port's own pad says Start is down
  * the guest sees it and ignores it -> m_Held carries Start, and the screen still does not change

It reads guest words only, through the port's own REPL (`rw`), and takes every transition with the
same pad edges a player uses. It writes NOTHING into guest memory.

It also reports m_Type, because retail gates its own title timeout on
`g_Pad.m_Type >= 2` (CONTROLLER_TYPE_DPAD): a pad the guest believes is absent is a different
defect from a button that is not arriving, and the two produce the same symptom.

Addresses are the port's own guest_globals (the decompilation is byte-identical to the selected
image, so its symbols are the addresses):

  g_Pad              0x80077378   m_Down +0, m_Released +4, m_Held +8, m_Type +0x0C
  g_TitlescreenState 0x80078D78   m_Mode +0, m_State +4, m_Tick +8, m_SubTick +0xC,
                                   m_SubState +0x10
  g_Gamestate        0x800757D8   0 = GS_Playing, 13 = GS_TitleScreen

Run:  python3 tools/probe_title_card_skip.py [--hold-frames 240] [--selftest]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import guest_globals  # noqa: E402
from drive import ROOT, Port, disc_path, environment  # noqa: E402

G_PAD = 0x80077378
G_PAD_HELD = G_PAD + 0x08
G_PAD_TYPE = G_PAD + 0x0C

G_TITLESCREEN = guest_globals.kTitlescreenState
TS_MODE = G_TITLESCREEN + 0x00
TS_STATE = G_TITLESCREEN + 0x04
TS_TICK = G_TITLESCREEN + 0x08
TS_SUBTICK = G_TITLESCREEN + 0x0C
TS_SUBSTATE = G_TITLESCREEN + 0x10

TSM_INIT = 0
TSM_MENU = 1

# gamepad.h's own bit positions. PAD_START is bit 11, NOT bit 0 -- an earlier version of this probe
# assumed bit 0 and reported "the press never reached the guest" for a run in which it plainly had.
# The measured m_Held was 0xffff0800, and 0x0800 IS bit 11.
PAD_START = 1 << 11
PAD_CROSS = 1 << 6
CONTROLLER_TYPE_DPAD = 2

# The card is substate 2, and retail only reads the pad at substate 3 -- so the interesting window
# is substate 2 with the press held, which is exactly what the operator is looking at.
CARD_SUBSTATE = 2


def classify(held: int) -> str:
    """What the guest's own title-card condition would decide from this m_Held."""
    if not held & (PAD_START | PAD_CROSS):
        return "no buttons held -> retail's condition is false"
    # EXACTLY retail's predicate, nothing added: titlescreen.c:103 tests
    #   (g_Pad.m_Held & (PAD_START | PAD_CROSS)) != 0
    # and nothing else. An earlier version also required the high half to be clear, on the theory
    # that it held other buttons -- but a real run reads 0xffff0800 there, so that guard reported a
    # present press as absent. The high half is not part of retail's test, so it is not part of this.
    if held & (PAD_START | PAD_CROSS):
        return "START/CROSS held -> retail's fast-forward condition is TRUE"
    return f"held=0x{held:04X} but no START/CROSS bit -> condition false"


def selftest() -> int:
    """The negative first: this probe must be able to say the press did NOT arrive."""
    cases = [
        (0, "no buttons held -> retail's condition is false"),
        (PAD_START, "START/CROSS held -> retail's fast-forward condition is TRUE"),
        (PAD_CROSS, "START/CROSS held -> retail's fast-forward condition is TRUE"),
        (PAD_START | PAD_CROSS, "START/CROSS held -> retail's fast-forward condition is TRUE"),
        (1 << 8, "no buttons held -> retail's condition is false"),
        # The shape this probe actually measured on the title card. If the high half were mistaken
        # for the button mask this case would read FALSE and the probe would report a missing press
        # on a run that carried one -- which is exactly the mistake the first version made.
        (0xFFFF0800, "START/CROSS held -> retail's fast-forward condition is TRUE"),
        (0xFFFF0000, "no buttons held -> retail's condition is false"),
    ]
    failures = 0
    for held, want in cases:
        got = classify(held)
        if got != want:
            print(f"FAIL held=0x{held:04X}: got {got!r} want {want!r}")
            failures += 1
    # A predicate that cannot report the bad answer is not an instrument.
    positives = sum(1 for h, _ in cases if h & (PAD_START | PAD_CROSS))
    negatives = len(cases) - positives
    print(
        f"classify: {len(cases)} cases, {positives} positive, {negatives} negative, "
        f"{failures} failed"
    )
    if negatives == 0:
        print("FAIL the classifier has no negative case, so it cannot report a missing press")
        failures += 1
    if failures == 0:
        print(
            f"selftest: {len(cases) + 1}/{len(cases) + 1} OK -- the classifier names both the "
            f"arriving and the missing press, on the exact word shape a real run produces"
        )
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hold-frames", type=int, default=240)
    parser.add_argument("--settle", type=int, default=0)
    parser.add_argument(
        "--wait-title",
        type=int,
        default=1400,
        help="fields to advance looking for GS_TitleScreen; the title card is the subject, so the "
        "probe has to find it rather than assume a fixed offset from boot",
    )
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()

    # Same environment owner drive.py uses, so a probe run and a driven run cannot disagree about
    # which disc, which settings and which enhancements are in effect.
    env = environment(disc_path())
    port = Port(
        ROOT / "build/bin/spyro_port",
        ROOT / "scratch/assets/spyro1/SCUS_942.28",
        ROOT / "scratch/logs/probe_title_card_skip.log",
        env,
    )
    try:
        print("[probe] booted; looking for the title screen")
        if args.settle:
            print(f"[probe] settle {args.settle}")
            port.run(args.settle)

        # OBSERVE the title screen rather than assuming a field offset: the boot/attract sequence is
        # timing dependent, and a probe that waited a fixed number of fields would either miss the
        # card or read the card on some runs and a different screen on others. Both look like a
        # result, which is how two sessions were spent re-deriving the same route.
        gamestate = port.gamestate()
        mode = port.word(TS_MODE)
        substate = port.word(TS_SUBSTATE)
        seen_title = gamestate == 13
        advanced = 0
        while not seen_title and advanced < args.wait_title:
            port.run(10)
            advanced += 10
            gamestate = port.gamestate()
            if gamestate == 13:
                seen_title = True
                mode = port.word(TS_MODE)
                substate = port.word(TS_SUBSTATE)
                break
            # NOTE: gamestate 0 is GS_Playing, and it is ALSO what the word holds before the boot
            # owner has published a real state, so it is not a usable "the title screen is over"
            # signal here. An earlier version of this probe broke out on it and reported a refusal
            # ten fields in, which reads exactly like "the card was not up" and is why the wait is
            # bounded by budget alone.
        print(
            f"[probe] gamestate={gamestate} titleMode={mode} titleSubState={substate} "
            f"after {advanced} fields"
        )
        if not seen_title:
            print(
                f"[probe] REFUSING: gamestate is {gamestate}, not 13 (GS_TitleScreen), after "
                f"{advanced} of {args.wait_title} fields. The title card was not up, so this run "
                f"cannot say anything about it."
            )
            return 2
        if substate != CARD_SUBSTATE:
            print(
                f"[probe] NOTE: titleSubState is {substate}, not {CARD_SUBSTATE}. Retail reads the "
                f"pad at substate 3; a press here is retail's business, not this probe's."
            )

        held_before = port.word(G_PAD_HELD)
        pad_type = port.word(G_PAD_TYPE)
        print(
            f"[probe] before the press: g_Pad.m_Held={held_before:#06x} "
            f"g_Pad.m_Type={pad_type}"
        )
        if pad_type < CONTROLLER_TYPE_DPAD:
            print(
                f"[probe] FINDING: the guest believes m_Type={pad_type} "
                f"(CONTROLLER_TYPE_DPAD is {CONTROLLER_TYPE_DPAD}). Retail gates its title "
                f"timeout on m_Type >= 2, so the guest is treating the pad as absent."
            )

        print(f"[probe] HOLD start for {args.hold_frames} frames")
        port.press("start")
        held_samples: list[int] = []
        modes: list[int] = []
        remaining = args.hold_frames
        while remaining > 0:
            step = min(10, remaining)
            port.run(step)
            remaining -= step
            held_samples.append(port.word(G_PAD_HELD))
            modes.append(port.word(TS_MODE))

        saw_start = any(h & (PAD_START | PAD_CROSS) for h in held_samples)
        left_init = any(m == TSM_MENU for m in modes)
        substate_after = port.word(TS_SUBSTATE)
        gamestate_after = port.gamestate()
        port.release("start")

        print(f"[probe] g_Pad.m_Held samples: {[hex(h) for h in held_samples[:8]]}")
        print(f"[probe] titleMode samples:    {modes[:8]}")
        print(f"[probe] after: titleMode={port.word(TS_MODE)} titleSubState={substate_after}")
        print(f"[probe] after: gamestate={gamestate_after}")

        print()
        if not saw_start:
            print(
                "VERDICT: THE PRESS NEVER REACHED THE GUEST. m_Held carried no START/CROSS bit on "
                "any sampled frame while the port's own pad was held down, so retail's title-card "
                "condition was false every frame and no title-card skip could run. This is an input "
                "plumbing defect, not a missing skip."
            )
            return 1
        if not left_init:
            print(
                "VERDICT: THE GUEST SAW THE PRESS AND DID NOT ACT. m_Held carried START/CROSS while "
                "held, and the title mode never left TSM_Init, so retail's own fast-forward at "
                "titlescreen.c:100-106 did not fire. Either its gate is false for another reason "
                "(m_CurrentTick never reached 300, or m_SubState is not 2) or something upstream is "
                "rewriting the tick."
            )
            return 1
        print(
            "VERDICT: THE CARD WAS SKIPPED BY THE GUEST'S OWN ROUTE. m_Held carried START and the "
            "title mode left TSM_Init for TSM_Menu, which is retail's own substate 3 -> 4 -> "
            "TSM_Menu path. Nothing in the port had to be added."
        )
        return 0
    finally:
        port.end()


if __name__ == "__main__":
    raise SystemExit(main())
