#!/usr/bin/env python3
"""title_route.py — drive Spyro 2 or Spyro 3 from the title screen into gameplay, and prove it moved.

    uv run --frozen python tools/title_route.py --title spyro2
    uv run --frozen python tools/title_route.py --title spyro3 --shot-dir scratch/play
    uv run --frozen python tools/title_route.py --selftest

WHY THIS EXISTS. `drive.py` reaches Spyro 1's gameplay from Spyro 1's own words. Spyro 2 and 3 have their
own game-state word, their own title and their own player position (`title_profile.py`), and until now
nobody had driven either past its title screen: the route below is the first, and it reads the guest's
state to decide every press rather than counting frames, because a cutscene or a load that takes longer
than a count turns a frame-counted route into a capture of the wrong screen.

THE ROUTE, as observed on both titles. Wait for the title state; tap Start once; then, until the guest
reports the playing state, alternate Cross and Start every PRESS_PERIOD fields. Cross confirms the menu's
New Game, and either button skips the intro cutscenes (Start on Spyro 3, Cross on Spyro 2, which is why
both are pressed). Nothing is written into guest memory: every transition is a pad edge.

WHAT COUNTS AS ARRIVAL. The playing state is 0 on both titles, and 0 is also the boot state, so reaching
"state 0" proves nothing by itself. Arrival is refused unless the route first saw the title state AND the
loading state on the way, in that order.

WHAT COUNTS AS PLAYING. Reaching the state is not the claim; MOVING is. After a settle, the tool reads the
player's (x, y, z) triple and holds the title's walk button (a direction away from the first conversation),
then reads it again: it must move horizontally by at least MIN_MOVE. It then lets the player come to rest,
holds Cross and reads a third time: z must RISE, which separates the player from a camera that follows it and
from terrain that merely slopes under a run. A run that does not move, or whose z does not respond to a jump,
is refused by name. Shots of the settled and the moved frame are written when `--shot-dir` is given; open them.
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Protocol

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from title_profile import Refusal, TitleProfile, profile

EXIT_OK, EXIT_NOT_PLAYING, EXIT_REFUSED = 0, 1, 2

STEP = 10  # fields per observation; the same resolution `drive.Port.run` samples at
PRESS_PERIOD = 150  # fields between presses on the way in: long enough for a press to be consumed
TITLE_BUDGET = 3000  # fields allowed for boot to reach the title
ROUTE_BUDGET = 16000  # fields allowed from the title to gameplay: Spyro 3's two cutscenes take ~9000
SETTLE = 200  # fields after arrival before any input; the level fades in first
HOLD_WALK = 60
HOLD_JUMP = 16
REST = 90  # fields to stand still after the run, so a rise in z is the jump and not the slope
MIN_MOVE = 100  # world units; 40 fields of walking moved Spyro 3's y by 1201


class RoutePort(Protocol):
    """The subset of `drive.Port` the route uses, so a scripted port can stand in for the live one."""

    def run(self, frames: int) -> int: ...
    def gamestate(self) -> int: ...
    def tap(self, button: str, frames: int = 4) -> None: ...
    def press(self, button: str) -> None: ...
    def release(self, button: str) -> None: ...
    def words(self, address: int, count: int = 1) -> list[int]: ...
    def shot(self, path: str) -> None: ...


Position = tuple[int, int, int]


@dataclass
class Evidence:
    states_seen: list[int] = field(default_factory=list)
    arrival_field: int = 0
    settled: Position = (0, 0, 0)
    after_up: Position = (0, 0, 0)
    rested: Position = (0, 0, 0)
    after_jump: Position = (0, 0, 0)

    def moved(self) -> int:
        return abs(self.after_up[0] - self.settled[0]) + abs(self.after_up[1] - self.settled[1])


def _signed(word: int) -> int:
    return word - (1 << 32) if word & 0x80000000 else word


def read_position(port: RoutePort, entry: TitleProfile) -> Position:
    x, y, z = (_signed(port.words(address, 1)[0]) for address in entry.position_words())
    return x, y, z


def _note(evidence: Evidence, state: int) -> None:
    if not evidence.states_seen or evidence.states_seen[-1] != state:
        evidence.states_seen.append(state)


def _advance(port: RoutePort, evidence: Evidence) -> int:
    port.run(STEP)
    state = port.gamestate()
    _note(evidence, state)
    return state


def reach_playing(port: RoutePort, entry: TitleProfile, evidence: Evidence) -> int:
    """Drive the title into gameplay; returns the fields spent. Refuses by name when the route does not
    arrive in budget or arrives without having shown the title and loading states first."""
    fields = 0
    while _advance(port, evidence) != entry.state_title:
        fields += STEP
        if fields >= TITLE_BUDGET:
            raise Refusal(
                f"{entry.label}: no title screen (state {entry.state_title}) within {TITLE_BUDGET} fields; "
                f"states seen {evidence.states_seen}"
            )
    port.tap("start", 6)
    spent, presses = 0, 0
    while True:
        if _advance(port, evidence) == entry.state_playing and entry.state_loading in evidence.states_seen:
            return fields + spent
        spent += STEP
        if spent % PRESS_PERIOD == 0:
            port.tap(("cross", "start")[presses % 2], 6)
            presses += 1
        if spent >= ROUTE_BUDGET:
            raise Refusal(
                f"{entry.label}: gameplay (state {entry.state_playing}) not reached within {ROUTE_BUDGET} "
                f"fields of the title; states seen {evidence.states_seen}"
            )


def _hold(port: RoutePort, button: str, frames: int) -> None:
    port.press(button)
    port.run(frames)
    port.release(button)


def prove_movement(port: RoutePort, entry: TitleProfile, evidence: Evidence, shot_dir: Path | None) -> None:
    """Settle, then show the position responding to the pad. Raises Refusal when it does not."""
    port.run(SETTLE)
    evidence.settled = read_position(port, entry)
    if shot_dir is not None:
        port.shot(str(shot_dir / "settled.ppm"))
    _hold(port, entry.walk_button, HOLD_WALK)
    evidence.after_up = read_position(port, entry)
    if shot_dir is not None:
        port.shot(str(shot_dir / "moved.ppm"))
    if evidence.moved() < MIN_MOVE:
        raise Refusal(
            f"{entry.label}: {entry.walk_button} for {HOLD_WALK} fields moved the position by {evidence.moved()} (< {MIN_MOVE}): "
            f"{evidence.settled} -> {evidence.after_up}; this word is not the player or the player did not move"
        )
    port.run(REST)
    evidence.rested = read_position(port, entry)
    _hold(port, "cross", HOLD_JUMP)
    evidence.after_jump = read_position(port, entry)
    if evidence.after_jump[2] <= evidence.rested[2]:
        raise Refusal(
            f"{entry.label}: a jump left z at {evidence.after_jump[2]} from rest at {evidence.rested[2]}; "
            "the third word did not rise, so it is not the player's height"
        )


def play(port: RoutePort, entry: TitleProfile, shot_dir: Path | None = None) -> Evidence:
    evidence = Evidence()
    evidence.arrival_field = reach_playing(port, entry, evidence)
    prove_movement(port, entry, evidence, shot_dir)
    return evidence


def report(entry: TitleProfile, evidence: Evidence) -> str:
    return "\n".join(
        (
            f"{entry.label}: reached gameplay after {evidence.arrival_field} fields; states seen {evidence.states_seen}",
            f"  settled    {evidence.settled}",
            f"  after walk {evidence.after_up}  (moved {evidence.moved()} units horizontally)",
            f"  at rest    {evidence.rested}",
            f"  after jump {evidence.after_jump}  (z {evidence.rested[2]} -> {evidence.after_jump[2]})",
        )
    )


def run_live(name: str, executable: Path, log: Path, shot_dir: Path | None) -> int:
    import drive  # the live port; imported here so the selftest needs no binary and no disc

    entry = profile(name)
    disc = drive.disc_path(entry.disc_variable)
    if not executable.is_file():
        raise Refusal(f"the built port is missing: {executable} - build target spyro_port first")
    image = drive.ROOT / entry.image
    if not image.is_file():
        raise Refusal(f"{entry.label} is not provisioned: {image} is missing - run tools/provision_title.py")
    if not disc:
        raise Refusal(f"no disc for {entry.label}: set {entry.disc_variable} (environment or .env)")
    env = drive.environment(None)
    env[entry.disc_variable] = disc
    if shot_dir is not None:
        (drive.ROOT / shot_dir).mkdir(parents=True, exist_ok=True)
    port = drive.Port(executable, image, log, env, gamestate_address=entry.gamestate_word)
    try:
        evidence = play(port, entry, shot_dir)
    except drive.Refusal as refusal:
        raise Refusal(str(refusal)) from refusal
    finally:
        port.end()
    print(report(entry, evidence))
    return EXIT_OK


class _Scripted:
    """A port whose game state follows a script, for the selftest. Before Start is tapped it shows state 0
    (boot) and then the title (unless `has_title` is false); after it, `script` maps fields since Start to
    the state that begins there. `walks` says whether Up moves y, `jumps` whether Cross moves z."""

    BOOT_FIELDS = 100

    def __init__(
        self, script: Sequence[tuple[int, int]], *, has_title: bool = True, walks: bool = True, jumps: bool = True
    ) -> None:
        self.script, self.has_title, self.walks, self.jumps = script, has_title, walks, jumps
        self.field = 0
        self.started_at: int | None = None
        self.held: set[str] = set()
        self.pos = [1000, 2000, 3000]

    def run(self, frames: int) -> int:
        self.field += frames
        if "up" in self.held and self.walks:
            self.pos[1] += 30 * frames
        if "cross" in self.held and self.jumps:
            self.pos[2] += 25 * frames
        return self.field

    def gamestate(self) -> int:
        if self.started_at is None:
            return 11 if self.has_title and self.field >= self.BOOT_FIELDS else 0
        state = 11
        for begins, value in self.script:
            if self.field - self.started_at >= begins:
                state = value
        return state

    def tap(self, button: str, frames: int = 4) -> None:
        if button == "start" and self.started_at is None:
            self.started_at = self.field

    def press(self, button: str) -> None:
        self.held.add(button)

    def release(self, button: str) -> None:
        self.held.discard(button)

    def words(self, address: int, count: int = 1) -> list[int]:
        return [self.pos[(address - 0x80000100) // 4] & 0xFFFFFFFF]

    def shot(self, path: str) -> None:
        pass


def _selftest() -> int:
    entry = profile("spyro3")
    entry = TitleProfile(entry.label, entry.image, entry.disc_variable, 0x80000000, 11, 5, 0, 0x80000100, "up")
    route = ((0, 11), (500, 5), (800, 6), (1100, 5), (1400, 0))

    def attempt(label: str, scripted: _Scripted, expect: str | None) -> bool:
        try:
            result = play(scripted, entry, Path("shots"))
        except Refusal as refusal:
            if expect is not None and expect in str(refusal):
                print(f"  refuses {label}: {refusal}")
                return True
            print(f"SELFTEST FAILED: {label} refused for the wrong reason: {refusal}", file=sys.stderr)
            return False
        if expect is not None:
            print(f"SELFTEST FAILED: {label} was accepted", file=sys.stderr)
            return False
        assert result.moved() >= MIN_MOVE and result.after_jump[2] > result.rested[2]
        assert result.states_seen[:3] == [0, 11, 5], result.states_seen
        print(f"  accepts {label}: {report(entry, result).splitlines()[0]}")
        return True

    cases = (
        attempt("a title that loads and plays", _Scripted(route), None),
        attempt("a title that never leaves the title screen", _Scripted(((0, 11),)), "not reached"),
        attempt("a boot that never shows a title", _Scripted(route, has_title=False), "no title screen"),
        attempt("a title that skips loading", _Scripted(((0, 11), (300, 0))), "not reached"),
        attempt("a position that does not respond to Up", _Scripted(route, walks=False), "moved the position by 0"),
        attempt("a height that does not respond to a jump", _Scripted(route, jumps=False), "did not rise"),
    )
    if not all(cases):
        return 1
    print("title_route selftest PASS")
    return 0


def main(argv: Sequence[str] | None = None, *, live: Callable[..., int] = run_live) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--title", choices=("spyro2", "spyro3"))
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--log", type=Path, default=Path("scratch/play/title_route.log"))
    parser.add_argument("--shot-dir", type=Path, default=None, help="write settled.ppm and moved.ppm here")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args(argv)
    if args.selftest:
        return _selftest()
    if not args.title:
        parser.error("--title is required")
    try:
        return live(args.title, args.executable, args.log, args.shot_dir)
    except Refusal as refusal:
        print(f"REFUSED: {refusal}", file=sys.stderr)
        return EXIT_REFUSED


if __name__ == "__main__":
    raise SystemExit(main())
