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
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Guest addresses, from external/spyro-1's symbols over our byte-identical SCUS_942.28.
G_GAMESTATE = 0x800757D8
G_TITLESCREEN = 0x80078D78
G_LOAD_STAGE = 0x80075864
G_LEVEL_TRANS_TICKS = 0x800756AC
G_LEVEL_TRANS_HUD = 0x800756B0

GS_PLAYING = 0
GS_LEVEL_TRANSITION = 1
GS_ENTRANCE_ANIMATION = 9
GS_TITLE_SCREEN = 13
GS_CUTSCENE = 14
GS_CREDITS = 15

TSM_INIT, TSM_MENU, TSM_LOADING, TSM_DEMO = 0, 1, 2, 3


class Refusal(RuntimeError):
    """The requested state was not reached, and saying so beats returning a plausible frame."""


@dataclass(frozen=True)
class TitleState:
    mode: int
    state: int
    tick: int
    sub_tick: int
    sub_state: int
    option: int


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
        self._await_prompt()

    # -- public API ---------------------------------------------------------

    def run(self, frames: int) -> int:
        self._send(f"run {max(1, frames)}")
        return self._await_prompt()

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

    # Stage 13 / TSM_Init sub-state 3 is the interactive "PRESS START" platform. Anything else in
    # TSM_Init is the fly-in, a fade, or the demo hand-off; Start is only meaningful at sub-state 3.
    #
    # TSM_Menu is the memory-card front end. Only its non-destructive prompts are answered here:
    # 15 selects which card to use, 4 accepts playing without a save, and 10 confirms creating this
    # game's save file. The FORMAT prompts (5/6/7) are deliberately absent — a driver that answered
    # them would erase the operator's card to reach a screenshot.
    def _reach_title_menu(self) -> None:
        answerable = {4, 10, 15}
        spent = 0
        while spent < self._budget:
            state = self._port.gamestate()
            title = self._port.title()
            if state == GS_TITLE_SCREEN and title.mode == TSM_LOADING:
                return
            if state == GS_TITLE_SCREEN and title.mode == TSM_INIT and title.sub_state == 3:
                self._port.tap("start")
            elif state == GS_TITLE_SCREEN and title.mode == TSM_MENU:
                if title.sub_state in answerable:
                    self._port.tap("cross")
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
            title = self._port.title()
            if self._port.gamestate() != GS_TITLE_SCREEN:
                return
            if title.mode == TSM_LOADING and title.state == 4:
                self._port.tap("left" if title.option != 0 else "cross")
            elif title.mode == TSM_LOADING and title.state == 1:
                self._port.tap("cross")
            spent += self.STEP
            self._port.run(self.STEP)
        raise Refusal(
            f"the save picker never committed a slot within {self._budget} frames; "
            f"last title={self._port.title()}"
        )

    def _wait_for_playing(self) -> None:
        spent = 0
        while spent < self._budget:
            state = self._port.gamestate()
            if state == GS_PLAYING:
                return
            # Exercises the port's own Start cancellation of the level-transition tally. The press is
            # only meaningful while that screen's HUD flag is still set, which is also the condition
            # the port itself checks, so a run with this off and one with it on differ by nothing but
            # the press.
            if (
                self._skip_transitions
                and state == GS_LEVEL_TRANSITION
                and self._port.word(G_LEVEL_TRANS_HUD) != 0
            ):
                self._port.tap("start")
            if state not in (
                GS_TITLE_SCREEN,
                GS_LEVEL_TRANSITION,
                GS_ENTRANCE_ANIMATION,
                GS_CUTSCENE,
            ):
                raise Refusal(f"left the load route into unexpected gamestate {state}")
            spent += self.STEP
            self._port.run(self.STEP)
        raise Refusal(
            f"never reached GS_Playing within {self._budget} frames; "
            f"gamestate={self._port.gamestate()} load_stage={self._port.word(G_LOAD_STAGE)}"
        )


def environment(disc: str | None) -> dict[str, str]:
    env = dict(os.environ)
    env.update(
        PSXPORT_REPL="1",
        PSXPORT_NOAUDIO="1",
        PSXPORT_NOPACE="1",
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
    parser.add_argument("--settings", default="scratch/spyro-runtime/settings.ini")
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
        help="press Start on the level-transition tally while driving in, exercising the port's "
        "cancellation of that screen",
    )
    parser.add_argument("--hold", action="append", default=[], help="button held after arrival")
    parser.add_argument("--hold-frames", type=int, default=60)
    parser.add_argument("--tap", action="append", default=[], help="button tapped after arrival")
    parser.add_argument("--after", type=int, default=0, help="frames to run after the taps")
    parser.add_argument(
        "--repeat", type=int, default=1, help="how many times to repeat the tap/after cycle"
    )
    parser.add_argument("--shot", default="", help="capture here once the route and inputs are done")
    args = parser.parse_args()

    env = environment(disc_path())
    if args.debug:
        env["PSXPORT_DEBUG"] = args.debug
    if args.settings:
        env["PSXPORT_SETTINGS"] = args.settings
    for entry in args.env:
        name, separator, value = entry.partition("=")
        if not separator:
            parser.error(f"--env expects NAME=VALUE, got {entry!r}")
        env[name] = value

    port = Port(ROOT / args.executable, ROOT / args.binary, ROOT / args.log, env)
    try:
        Navigator(port, skip_transitions=args.skip_transitions).reach_gameplay()
        print(f"reached GS_Playing at frame {port.frame}", file=sys.stderr)
        if args.settle:
            port.run(args.settle)
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
        print(f"  run log: {args.log}", file=sys.stderr)
        port.end()
        return 2
    code = port.end()
    print(f"run log: {args.log} (exit {code})", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main())
