#!/usr/bin/env python3
"""drive.py — reach a named Spyro 1 game state by OBSERVING it, not by counting frames.

WHY THIS EXISTS. Every scripted route in this project so far was a fixed list of `run N` / `tap`
lines. That works exactly once: the boot/attract sequence is timing dependent, so the same script
reaches the save picker on one run, the Insomniac card on the next, and the title screen on a third
— and each of those looks like a plausible capture, so a run that never reached gameplay reads like
a run that did. Two sessions were spent re-deriving the same route from screenshots.

This drives the port's own REPL control channel as a loop: advance a few frames, READ the guest's
state words, decide the next input from them, and refuse by name when the expected state is not
reached inside the budget. Nothing is written into guest memory; every transition is taken with the
same pad edges a player would use.

The observed words come from external/spyro-1 (byte-identical target, so its symbols are our
addresses):
  g_Gamestate           0x800757D8   Gamestate enum (0 = GS_Playing, 13 = GS_TitleScreen)
  g_TitlescreenState    0x80078D78   m_Mode, m_State, m_Tick, m_SubTick, m_SubState, m_OptionSelected
  g_LoadStage           0x80075864   streaming/load phase
  g_LevelTransTicks     0x800756AC   level-transition HUD tick
  g_LevelTransHudActive 0x800756B0

Usage:
  drive.py gameplay --shot scratch/screenshots/gameplay.ppm
  drive.py gameplay --hold left --hold-frames 60 --shot scratch/screenshots/left.ppm
  drive.py gameplay --tap circle --after 120 --shot scratch/screenshots/flame.ppm
"""

from __future__ import annotations

import argparse
import math
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import guest_globals
import spyro1_steering
from spyro1_steering import Refusal as SteeringRefusal
from spyro1_steering import moby_class_targets, portal_targets

ROOT = Path(__file__).resolve().parent.parent

# The enhancement configuration every agent run of this port is gated under. It lives in the launch
# environment rather than in one tool's argparse default because oracle_compare.py builds its product
# environment from here too: left unset, the product falls back to its own discovery and picks up
# whichever psxport_settings.ini happens to sit in the working directory -- an untracked, per-machine
# file. Measured 2026-09-19: an oracle run that recorded product_env {} still had fps60 and widescreen
# on, from the operator's personal file, and a fresh clone or CI would silently have run without them.
SHIPPING_SETTINGS = ROOT / "tools" / "shipping_settings.ini"

# Guest addresses. The shared ones come from the shipping owner, game/core/guest_globals.h, through
# tools/guest_globals.py, so this driver and the product cannot read different memory. The two
# level-transition words below are read here and nowhere else, so they stay with their only reader.
G_GAMESTATE = guest_globals.kGamestate
G_TITLESCREEN = guest_globals.kTitlescreenState
G_LOAD_STAGE = guest_globals.kLoadStage
G_LEVEL_ID = guest_globals.kLevelId
G_LEVEL_TRANS_TICKS = 0x800756AC
G_LEVEL_TRANS_HUD = 0x800756B0

# The gamestate / overlay vocabulary is title_states' (one home, because tools/title_prompts.py needs it
# too and a second copy is how two drivers come to disagree about which screen they are on). Re-exported
# here because this module is where every other tool has always imported them from.
from title_states import (  # noqa: F401  (re-exported on purpose — see above)
    GS_CUTSCENE,
    GS_CREDITS,
    GS_ENTRANCE_ANIMATION,
    GS_LEVEL_TRANSITION,
    GS_PLAYING,
    GS_TITLE_SCREEN,
    TSS_ACTIVE,
    TSM_DEMO,
    TSM_INIT,
    TSM_LOADING,
    TSM_MENU,
    TitleState,
)

import title_prompts  # the menu sequence, shared with tools/live_play.py's driver

# Named so a census line reads as game states rather than as integers. These are the states a Spyro 1
# route can enter; the census reports the ones it did not reach as well as the ones it did.
GAMESTATE_NAMES = {
    GS_PLAYING: "playing",
    GS_LEVEL_TRANSITION: "level_transition",
    8: "dragon",
    GS_ENTRANCE_ANIMATION: "entrance_animation",
    GS_TITLE_SCREEN: "title_screen",
    GS_CUTSCENE: "cutscene",
    GS_CREDITS: "credits",
}
# One refusal type for every named refusal a driver can make, so a steering refusal is reported the
# same way as a navigation one instead of escaping as a traceback.
Refusal = SteeringRefusal


class Port:
    """One live port process driven through its REPL control channel."""

    _WORDS = re.compile(r"\[repl\] ([0-9A-F]{8}):((?: [0-9A-F]{8})+)")
    _READY = re.compile(r"\[repl\] frame=(\d+) ready")

    def __init__(self, executable: Path, binary: Path, log: Path, env: dict[str, str]):
        log.parent.mkdir(parents=True, exist_ok=True)
        self._log = log.open("w")
        self._proc = subprocess.Popen(
            [str(executable), str(binary)],
            cwd=ROOT,
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        self.frame = 0
        # Every gamestate this run was ever observed in, and how many samples saw it. A driven run
        # that ends without its symptom proves nothing unless the state under test was reached, and
        # until now no driver could say: issue 0103's documented repro exits 0 either because the
        # dragon producer works or because the run never met a dragon, and the log cannot tell them
        # apart.
        self.gamestate_census: dict[int, int] = {}
        # The census restarted at arrival. A whole-run census cannot tell "the dragon cutscene
        # rendered during gameplay" from "the attract sequence happened to pass through it on the
        # way in", and those are different answers to issue 0103.
        self._census_at_arrival: dict[int, int] = {}
        self._await_prompt()

    # -- public API ---------------------------------------------------------

    # A state the game passes through in a few frames is invisible to a sampler that only looks once
    # per call, and calls here are as long as --hold-frames. Advancing in bounded chunks costs a REPL
    # round trip each and gives every run the same resolution; the guest advances the same number of
    # frames either way, and held buttons are separate commands so chunking cannot drop an input.
    SAMPLE_FRAMES = 10

    def run(self, frames: int) -> int:
        remaining = max(1, frames)
        result = 0
        while remaining > 0:
            step = min(self.SAMPLE_FRAMES, remaining)
            self._send(f"run {step}")
            result = self._await_prompt()
            remaining -= step
            state = self.gamestate()
            self.gamestate_census[state] = self.gamestate_census.get(state, 0) + 1
        return result

    def mark_arrival(self) -> None:
        self._census_at_arrival = dict(self.gamestate_census)

    def census_line(self) -> str:
        """One line naming every gamestate reached, with its denominator.

        Printed whether or not anything went wrong, and it names the states NOT seen among the ones
        a Spyro route can reach, because "the dragon cutscene never happened" and "the dragon
        cutscene rendered fine" are the two readings of a clean run that have to be told apart.
        """
        total = sum(self.gamestate_census.values())
        if not total:
            return "gamestates: nothing sampled (no frames were advanced)"
        seen = ", ".join(f"{GAMESTATE_NAMES.get(state, str(state))}={count}"
                         for state, count in sorted(self.gamestate_census.items()))
        missing = [name for state, name in sorted(GAMESTATE_NAMES.items())
                   if state not in self.gamestate_census]
        line = (f"gamestates over {total} samples ({self.SAMPLE_FRAMES} frames apart): {seen}"
                + (f"; never reached: {', '.join(missing)}" if missing else ""))
        after = {state: count - self._census_at_arrival.get(state, 0)
                 for state, count in self.gamestate_census.items()
                 if count - self._census_at_arrival.get(state, 0) > 0}
        if not self._census_at_arrival:
            return line + "; gameplay never started, so none of this is a gameplay observation"
        since = ", ".join(f"{GAMESTATE_NAMES.get(state, str(state))}={count}"
                          for state, count in sorted(after.items()))
        return line + f" | since GS_Playing: {since or 'nothing sampled'}"

    def tap(self, button: str, frames: int = 4) -> None:
        self._send(f"tap {button} {frames}")

    def press(self, button: str) -> None:
        self._send(f"press {button}")

    def release(self, button: str) -> None:
        self._send(f"release {button}")

    def words(self, address: int, count: int = 1) -> list[int]:
        self._send(f"rw {address:X} {count}")
        return self._await_words(address, count)

    def word(self, address: int) -> int:
        return self.words(address, 1)[0]

    def gamestate(self) -> int:
        return self.word(G_GAMESTATE)

    def title(self) -> TitleState:
        w = self.words(G_TITLESCREEN, 6)
        return TitleState(w[0], w[1], w[2], w[3], w[4], w[5])

    def gates(self) -> None:
        """Ask the port to list this level's gates, their target levels and path nodes."""
        self._send("gates")

    def gate_teleport(self, gate: int, node: int) -> None:
        """Place Spyro on a gate's own path node, through the port's existing gate diagnostic.

        Walking to an Artisans portal stalls about 11k view units out: the portals sit above the
        hub and the steering loop has no climb. This is the diagnostic route the port already
        provides for reaching one, and it is a driving aid, never something a product path may do.
        """
        self._send(f"gate-teleport {gate} {node}")

    def shot(self, path: str) -> None:
        Path(ROOT / path).parent.mkdir(parents=True, exist_ok=True)
        self._send(f"shot {path}")

    def end(self) -> int:
        self._send("end")
        assert self._proc.stdin is not None
        self._proc.stdin.close()
        self._drain()
        return self._proc.wait()

    # -- plumbing -----------------------------------------------------------

    def _send(self, line: str) -> None:
        assert self._proc.stdin is not None
        self._proc.stdin.write(line + "\n")
        self._proc.stdin.flush()

    def _lines(self):
        assert self._proc.stdout is not None
        for line in self._proc.stdout:
            self._log.write(line)
            yield line

    def _await_prompt(self) -> int:
        for line in self._lines():
            match = self._READY.search(line)
            if match:
                self.frame = int(match.group(1))
                return self.frame
        raise Refusal("the port exited before reaching its next frame boundary")

    def _await_words(self, address: int, count: int) -> list[int]:
        for line in self._lines():
            match = self._WORDS.search(line)
            if match and int(match.group(1), 16) == address:
                values = [int(v, 16) for v in match.group(2).split()]
                if len(values) == count:
                    return values
        raise Refusal(f"the port exited before answering rw 0x{address:08X}")

    def _drain(self) -> None:
        for _ in self._lines():
            pass
        self._log.close()



class Seeker:
    """Walk the product to one of a set of world positions, steering from the guest's own camera.

    Holding one direction cannot reach anything: the pad is camera-relative, and Artisans' portals
    sit tens of thousands of units away on a bearing the start position does not face. A fixed
    input list cannot reach them either, for the same reason every fixed route in this file was
    replaced.

    The route policy and the geometry are `spyro1_steering.Walk`, shared with the oracle's
    level-entry checkpoint so both walk one route. This class is only the part that is specific to
    driving the product's REPL: press, run a fixed number of FIELDS, release.
    """

    STEP = 24  # fields per steering decision; a whole walk cycle, short enough to correct a wall

    def __init__(self, port: "Port", what: str, targets, budget: int = 6000,
                 arrived: int | None = None, stop=None, stop_is: str = ""):
        self._port = port
        self._walk = spyro1_steering.Walk(what, list(targets), arrived=arrived)
        self._budget = budget
        # A destination that must make something HAPPEN — a portal entry, not a portal visit — gives
        # its own predicate, and the walk keeps closing the gap until that predicate holds. Ending
        # at a proximity radius would report "reached" for a walk that never entered anything.
        self._stop = stop
        self._stop_is = stop_is or "the destination"

    def walk(self) -> int:
        """Returns the view-space distance actually reached, or refuses by name."""
        # Printed per target, not just counted: a centre in the wrong units or read from the wrong
        # field looks exactly like an unreachable destination once the walk starts.
        for target in self._walk.remaining:
            print(f"seek: {target.what} at {target.position}", file=sys.stderr)
        spent = 0
        while spent < self._budget:
            if self._stop is not None and self._stop():
                print(f"seek: {self._stop_is} after {spent} field(s), "
                      f"{self._walk.closest} from the nearest {self._walk.what}", file=sys.stderr)
                return self._walk.closest or 0
            decision = self._walk.next(spyro1_steering.camera(self._port.words))
            if decision is None:
                print(f"seek: reached {self._walk.what} at {self._walk.closest}", file=sys.stderr)
                return self._walk.closest
            print(f"seek: {decision.describe()}", file=sys.stderr)
            for button in decision.buttons:
                self._port.press(button)
            if decision.hop:
                self._port.tap("cross", 8)
            self._port.run(self.STEP)
            for button in decision.buttons:
                self._port.release(button)
            spent += self.STEP
        if self._stop is not None:
            raise Refusal(
                f"walked to within {self._walk.closest} of a {self._walk.what} over {spent} "
                f"field(s), but {self._stop_is} never happened: being next to one is not entering it"
            )
        raise self._walk.exhausted(spent)


class Navigator:
    """Boot -> title -> save picker -> a loaded, playable level, decided from guest state."""

    STEP = 20  # frames between observations; small enough to catch a one-shot menu state

    def __init__(self, port: Port, budget: int = 12000, skip_transitions: bool = False):
        self._port = port
        self._budget = budget
        self._skip_transitions = skip_transitions

    def reach_gameplay(self) -> None:
        self._reach_title_menu()
        self._start_new_game()
        self._wait_for_playing()

    def _screen(self) -> "title_prompts.Screen":
        return title_prompts.Screen(gamestate=self._port.gamestate(), title=self._port.title(),
                                    level_trans_hud=self._port.word(G_LEVEL_TRANS_HUD))

    def _answer(self, prompt: "title_prompts.Prompt") -> None:
        if prompt.refuse:
            raise Refusal(prompt.refuse)
        for button in prompt.buttons:
            self._port.tap(button)

    # WHICH button each screen wants is title_prompts' decision, not this driver's: tools/live_play.py
    # plays the same route over the live debug server, and a second copy of the menu sequence would be
    # free to answer a different prompt than this one. What stays here is the stepping and the refusal
    # wording, which is about this driver's budget.
    #
    # The prompts themselves: TSM_Init sub-state 3 is the interactive "PRESS START" platform and every
    # other TSM_Init sub-state is the fly-in, a fade, or the demo hand-off, where Start is meaningless.
    # TSM_Menu is the memory-card front end, and only its non-destructive prompts are answered (15 picks
    # the card, 4 accepts playing without a save, 10 confirms creating this game's save); the FORMAT
    # prompts 5/6/7 are deliberately absent, because a driver that answered them would erase the
    # operator's card to reach a screenshot.
    def _reach_title_menu(self) -> None:
        spent = 0
        while spent < self._budget:
            prompt = title_prompts.title_menu_prompt(self._screen())
            if prompt.reached:
                return
            self._answer(prompt)
            spent += self.STEP
            self._port.run(self.STEP)
        raise Refusal(
            f"never reached the save picker (TSM_Loading) within {self._budget} frames; "
            f"last gamestate={self._port.gamestate()} title={self._port.title()}"
        )

    # TSM_Loading state 4 offers NEW GAME (option 0) beside LOAD GAME (option 1); state 1 then picks
    # the slot the new game is written into, and state 5 is the fade towards play. Confirming state 4
    # on its default option is what silently bounced earlier runs back to the title screen: LOAD GAME
    # on three EMPTY slots has nothing to load. State 2 (confirm overwrite) is deliberately never
    # answered here — it only appears over an existing save, which is the operator's.
    def _start_new_game(self) -> None:
        spent = 0
        while spent < self._budget:
            prompt = title_prompts.new_game_prompt(self._screen())
            if prompt.reached:
                return
            self._answer(prompt)
            spent += self.STEP
            self._port.run(self.STEP)
        raise Refusal(
            f"the save picker never committed a slot within {self._budget} frames; "
            f"last title={self._port.title()}"
        )

    def _wait_for_playing(self) -> None:
        spent = 0
        while spent < self._budget:
            prompt = title_prompts.load_route_prompt(self._screen(), self._skip_transitions)
            if prompt.reached:
                return
            self._answer(prompt)
            spent += self.STEP
            self._port.run(self.STEP)
        raise Refusal(
            f"never reached GS_Playing within {self._budget} frames; "
            f"gamestate={self._port.gamestate()} load_stage={self._port.word(G_LOAD_STAGE)}"
        )


def environment(disc: str | None, settings: Path | None = None) -> dict[str, str]:
    """The headless REPL launch environment for the built port. The framework's launch policy
    (external/psxport/tools/port/launch_environment.py) owns the headless/silent/unpaced knobs so
    no agent driver can seize the desktop or drift from the others.

    `settings` names the tracked .ini the run is gated with, defaulting to the shipping one. The
    picture oracle passes the reference configuration instead: it photographs the product against a
    4:3 console, and a widescreen frame is a different SIZE, which the comparison would refuse."""
    sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools"))
    from port.launch_environment import agent_environment

    env = agent_environment(dict(os.environ), settings or SHIPPING_SETTINGS)
    env.update(
        PSXPORT_REPL="1",
        PSXPORT_WATCHDOG="0",
        PSXPORT_ASSET_DIR="external/psxport",
    )
    if disc:
        env["PSXPORT_SPYRO_DISC"] = disc
    return env


def disc_path() -> str | None:
    value = os.environ.get("PSXPORT_SPYRO_DISC")
    if value:
        return value
    env_file = ROOT / ".env"
    if env_file.exists():
        for line in env_file.read_text().splitlines():
            key, separator, rest = line.partition("=")
            if separator and key.strip() == "PSXPORT_SPYRO_DISC":
                return rest.strip()
    return None


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("target", choices=["gameplay"], help="the state to drive to")
    parser.add_argument("--executable", default="build/bin/spyro_port")
    parser.add_argument("--binary", default="scratch/assets/spyro1/SCUS_942.28")
    parser.add_argument("--log", default="scratch/logs/drive.log")
    parser.add_argument("--debug", default="", help="PSXPORT_DEBUG channels for this run")
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="extra PSXPORT_* knob for this run, e.g. --env PSXPORT_RENDER_PATH=gte",
    )
    parser.add_argument(
        "--settings",
        default=str(SHIPPING_SETTINGS),
        help="PSXPORT_SETTINGS for this run; the default turns on the two enhancements under test "
        "(widescreen and interpolated 60fps), because the previous default named a file that did "
        "not exist and silently gated the product with both of them off",
    )
    parser.add_argument(
        "--settle",
        type=int,
        default=120,
        help="frames to run after arrival before any input; the level fades in and hands the player "
        "control some frames after GS_Playing is entered, so an input issued at frame 0 is eaten",
    )
    parser.add_argument(
        "--skip-transitions",
        action="store_true",
        help="press Start on the level-transition tally and the level flyby while driving in, exercising the port's "
        "cancellation of those screens",
    )
    parser.add_argument("--hold", action="append", default=[], help="button held after arrival")
    parser.add_argument("--hold-frames", type=int, default=60)
    parser.add_argument("--tap", action="append", default=[], help="button tapped after arrival")
    parser.add_argument("--after", type=int, default=0, help="frames to run after the taps")
    parser.add_argument(
        "--repeat", type=int, default=1, help="how many times to repeat the tap/after cycle"
    )
    parser.add_argument(
        "--seek-class",
        type=int,
        default=-1,
        help="walk to the nearest live Moby of this class before the holds/taps, steering from the "
        "guest camera each step; Spyro 1's gems are classes 83..87",
    )
    parser.add_argument(
        "--seek-arrived",
        type=int,
        default=None,
        metavar="UNITS",
        help="stop the seek at this view-space distance instead of walking all the way in. Standing "
        "next to a Moby puts it in front of the camera with nothing between, which cannot exhibit an "
        "occlusion defect; stopping short is how a scene where terrain covers it is reached",
    )
    parser.add_argument(
        "--gate-teleport",
        default="",
        metavar="GATE:NODE",
        help="place Spyro on that gate's path node before anything else, using the port's own "
        "gate diagnostic; the walk cannot climb to a portal",
    )
    parser.add_argument(
        "--seek-portal",
        action="store_true",
        help="walk to the nearest level portal instead, which is how a homeworld route reaches a "
        "level; mutually exclusive with --seek-class",
    )
    parser.add_argument("--shot", default="", help="capture here once the route and inputs are done")
    args = parser.parse_args()

    env = environment(disc_path())
    if args.debug:
        env["PSXPORT_DEBUG"] = args.debug
    if args.settings:
        # A settings path the product cannot open is worse than none: the run proceeds on defaults
        # and looks exactly like a run that honoured the file, which is how six weeks of Spyro
        # evidence came to be collected with widescreen and fps60 off. Refuse by name instead.
        settings = Path(args.settings)
        if not settings.is_file():
            parser.error(
                f"--settings {args.settings} does not exist; the product would silently run on "
                "defaults, with the enhancements under test switched off"
            )
        env["PSXPORT_SETTINGS"] = str(settings.resolve())
    for entry in args.env:
        name, separator, value = entry.partition("=")
        if not separator:
            parser.error(f"--env expects NAME=VALUE, got {entry!r}")
        env[name] = value

    port = Port(ROOT / args.executable, ROOT / args.binary, ROOT / args.log, env)
    try:
        Navigator(port, skip_transitions=args.skip_transitions).reach_gameplay()
        print(f"reached GS_Playing at frame {port.frame}", file=sys.stderr)
        port.mark_arrival()
        if args.settle:
            port.run(args.settle)
        if args.gate_teleport:
            gate, separator, node = args.gate_teleport.partition(":")
            if not separator:
                parser.error(f"--gate-teleport expects GATE:NODE, got {args.gate_teleport!r}")
            port.gates()
            port.gate_teleport(int(gate), int(node))
            port.run(args.settle or 1)
        if args.seek_class >= 0 and args.seek_portal:
            parser.error("--seek-class and --seek-portal name two different destinations")
        if args.seek_class >= 0:
            Seeker(
                port,
                f"class {args.seek_class}",
                moby_class_targets(port.words, args.seek_class),
                arrived=args.seek_arrived,
            ).walk()
        elif args.seek_portal:
            entering = port.word(G_LEVEL_ID)
            Seeker(port, "portal", portal_targets(port.words), arrived=0,
                   stop=lambda: port.word(G_LEVEL_ID) != entering,
                   stop_is=f"left level {entering}").walk()
        for button in args.hold:
            port.press(button)
        if args.hold:
            port.run(args.hold_frames)
            for button in args.hold:
                port.release(button)
        for _ in range(max(1, args.repeat)):
            for button in args.tap:
                port.tap(button, 8)
            if args.after:
                port.run(args.after)
        if args.shot:
            port.shot(args.shot)
            port.run(1)
    except Refusal as refusal:
        print(f"drive.py REFUSED: {refusal}", file=sys.stderr)
        print(f"  {port.census_line()}", file=sys.stderr)
        print(f"  run log: {args.log}", file=sys.stderr)
        port.end()
        return 2
    code = port.end()
    print(port.census_line(), file=sys.stderr)
    print(f"run log: {args.log} (exit {code})", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main())
