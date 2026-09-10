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

# Steering inputs, from external/spyro-1 the same way the state words above are.
G_CAMERA = 0x80076DD0        # 5 packed matrix words, then the position at +0x28
G_LEVEL_MOBYS = 0x80075828   # pointer to the level's Moby array
MOBY_BYTES = 0x58
MOBY_CLASS_WORD = 52         # the class is the high half of this word
MOBY_STATE = 72
MAX_LEVEL_MOBYS = 1024
G_PORTALS = 0x80078640       # six Portal pointers
G_PORTAL_COUNT = 0x800758BC
PORTAL_CENTER = 0x20         # Portal::m_Center, after the skybox pointer, counts and world sector


def signed(value: int) -> int:
    return value - 0x100000000 if value >= 0x80000000 else value


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



def moby_class_targets(port: "Port", wanted_class: int) -> list[tuple[int, int, int]]:
    """World positions of every live Moby of a class. Read once: level Mobys do not walk away."""
    first = port.word(G_LEVEL_MOBYS)
    if not (0x80000000 <= first < 0x80200000):
        raise Refusal(f"g_LevelMobys holds 0x{first:08X}, which is not a RAM address")
    words: list[int] = []
    found: list[tuple[int, int, int]] = []
    index = 0
    while index < MAX_LEVEL_MOBYS:
        need = (index + 1) * MOBY_BYTES // 4 + 1
        while len(words) < need:
            words.extend(port.words(first + len(words) * 4, 64))
        base = index * MOBY_BYTES // 4
        state = words[base + MOBY_STATE // 4]
        if state & 0x80:
            if (state & 0xFF) == 0xFF:
                break
            index += 1
            continue
        if (words[base + MOBY_CLASS_WORD // 4] >> 16) & 0xFFFF == wanted_class:
            found.append(tuple(signed(words[base + 3 + i]) for i in range(3)))
        index += 1
    return found


def portal_targets(port: "Port") -> list[tuple[int, int, int]]:
    """Centres of the level's portals, which are a loaded table rather than Mobys.

    A homeworld's portals are the only way into a level, and reaching a level is what the
    return-home glide needs. g_Portals holds up to six pointers and g_PortalCount says how many are
    live; each Portal carries its own centre after the skybox pointer, point count, unknown vector
    and world sector.
    """
    count = port.word(G_PORTAL_COUNT)
    if not 0 <= count <= 6:
        raise Refusal(f"g_PortalCount holds {count}, which is outside the table's six slots")
    found: list[tuple[int, int, int]] = []
    for index in range(count):
        portal = port.word(G_PORTALS + index * 4)
        if not (0x80000000 <= portal < 0x80200000):
            raise Refusal(
                f"g_Portals[{index}] holds 0x{portal:08X}, which is not a RAM address, so the "
                "portal table was read before the level finished loading"
            )
        center = tuple(signed(v) for v in port.words(portal + PORTAL_CENTER, 3))
        # Printed per portal, not just counted: a centre in the wrong units or read from the wrong
        # field looks exactly like an unreachable destination once the walk starts.
        print(
            f"seek: portal {index} at 0x{portal:08X} level {signed(port.word(portal + 0x1C))} "
            f"center {center}",
            file=sys.stderr,
        )
        found.append(center)
    return found


class Seeker:
    """Walk to the nearest of a set of world positions, steering from the guest's own camera.

    Holding one direction cannot reach anything: the pad is camera-relative, and Artisans' gems sit
    50k units away on a bearing the start position does not face. A fixed input list cannot reach
    them either, for the same reason every fixed route in this file was replaced. So this reads the
    targets once, then each step re-reads the camera, converts them into view space exactly as the
    renderer does, and presses the eight-way pad direction whose sector contains the nearest one's
    bearing.

    The targets themselves come from a named source (`moby_class_targets`, `portal_targets`) so a
    new destination is a reader, not a second copy of the steering loop.
    """

    STEP = 24  # frames per steering decision; a whole walk cycle, short enough to correct a wall
    ARRIVED = 500  # view-space units (~1/4 of world scale); close enough to be on screen
    STALL_STEPS = 15  # steps without closing the gap before this target is abandoned
    PROGRESS = 200  # view-space units that count as having closed the gap
    # Steps of no progress before the walk starts hopping. Portals sit on raised platforms, so
    # steering alone circles the base forever; a jump is ordinary traversal, not a state write.
    JUMP_AFTER = 3

    # 0 deg is straight ahead and +90 is screen-right, so these are the pad's own compass sectors.
    SECTORS = (
        (-157.5, ("down", "left")),
        (-112.5, ("left",)),
        (-67.5, ("up", "left")),
        (-22.5, ("up",)),
        (22.5, ("up", "right")),
        (67.5, ("right",)),
        (112.5, ("down", "right")),
        (157.5, ("down",)),
    )

    def __init__(self, port: "Port", what: str, targets, budget: int = 6000):
        self._port = port
        self._what = what
        self._targets = list(targets)
        self._budget = budget

    def walk(self) -> int:
        """Returns the view-space distance actually reached, or refuses by name."""
        targets = self._targets
        if not targets:
            raise Refusal(
                f"nothing to seek: this level carries no {self._what}, so the target is wrong or "
                "this is not the level that carries it"
            )
        print(f"seek: {len(targets)} live {self._what} target(s)", file=sys.stderr)
        remaining = list(targets)
        best = None
        stalled = 0
        spent = 0
        while spent < self._budget:
            if not remaining:
                raise Refusal(
                    f"every {self._what} target is unreachable from the start: each one stalled at "
                    "a fixed distance, so the route needs a jump or a different level entry, not "
                    "more walking"
                )
            index, distance, bearing = self._nearest(remaining)
            if distance <= self.ARRIVED:
                print(f"seek: reached {self._what} at {distance}", file=sys.stderr)
                return distance
            if best is None or distance < best - self.PROGRESS:
                best, stalled = distance, 0
            else:
                stalled += 1
                if stalled >= self.STALL_STEPS:
                    # Blocked by geometry or standing under a ledge. Retail levels put dozens of
                    # gems around one hub, so abandoning this one and taking the next nearest is a
                    # real route, where pressing harder into the same wall is not.
                    print(
                        f"seek: {self._what} target {index} stuck at {distance}; trying the next "
                        "nearest",
                        file=sys.stderr,
                    )
                    remaining.pop(index)
                    best, stalled = None, 0
                    continue
            hop = stalled >= self.JUMP_AFTER
            print(
                f"seek: {distance} away, bearing {bearing:+.0f}{' (hopping)' if hop else ''}",
                file=sys.stderr,
            )
            buttons = self._sector(bearing)
            for button in buttons:
                self._port.press(button)
            if hop:
                self._port.tap("cross", 8)
            self._port.run(self.STEP)
            for button in buttons:
                self._port.release(button)
            spent += self.STEP
        raise Refusal(
            f"the nearest {self._what} was still about {best} away after {self._budget} steering "
            "frames"
        )

    # -- observation --------------------------------------------------------

    def _matrix(self, words: list[int]) -> list[list[int]]:
        def half(value: int, high: bool) -> int:
            part = (value >> 16) & 0xFFFF if high else value & 0xFFFF
            return part - 0x10000 if part >= 0x8000 else part

        return [
            [half(words[0], False), half(words[0], True), half(words[1], False)],
            [half(words[1], True), half(words[2], False), half(words[2], True)],
            [half(words[3], False), half(words[3], True), half(words[4], False)],
        ]

    def _nearest(self, targets) -> tuple[int, int, float]:
        observed = self._port.words(G_CAMERA, 14)
        matrix = self._matrix(observed)
        camera = [signed(observed[10 + i]) for i in range(3)]
        best = None
        for index, position in enumerate(targets):
            view = self._view(matrix, camera, position)
            distance = int(math.hypot(view[0], view[2]))
            if best is None or distance < best[1]:
                best = (index, distance, math.degrees(math.atan2(view[0], view[2])))
        return best

    @staticmethod
    def _view(matrix, camera, position) -> list[int]:
        # The renderer's own packing: X is target-minus-camera, Y and Z are camera-minus-target, all
        # arithmetic-shifted down two, and the matrix rows consume them in Y/Z/X order.
        relative = (
            (position[0] - camera[0]) >> 2,
            (camera[1] - position[1]) >> 2,
            (camera[2] - position[2]) >> 2,
        )
        source = (relative[1], relative[2], relative[0])
        return [sum(matrix[row][i] * source[i] for i in range(3)) >> 12 for row in range(3)]

    def _sector(self, bearing: float) -> tuple[str, ...]:
        for limit, buttons in self.SECTORS:
            if bearing <= limit:
                return buttons
        return ("down", "left")  # past +157.5 the compass wraps back to the first sector


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
    parser.add_argument(
        "--seek-class",
        type=int,
        default=-1,
        help="walk to the nearest live Moby of this class before the holds/taps, steering from the "
        "guest camera each step; Spyro 1's gems are classes 83..87",
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
        if args.seek_class >= 0 and args.seek_portal:
            parser.error("--seek-class and --seek-portal name two different destinations")
        if args.seek_class >= 0:
            Seeker(port, f"class {args.seek_class}", moby_class_targets(port, args.seek_class)).walk()
        elif args.seek_portal:
            Seeker(port, "portal", portal_targets(port)).walk()
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
