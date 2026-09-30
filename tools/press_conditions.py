#!/usr/bin/env python3
"""press_conditions.py — a pad edge a driven run issues when the guest is observed in a named state.

Two phases, two classes, ONE policy, and the split is the interesting part.

`PressConditions` (PRE-ARRIVAL, `drive.py --press-while`) exists because `drive.py`'s `--hold`,
`--tap` and `--after` are all applied after arrival, and arrival is `GS_Playing`. So no driven run
could touch anything that ends before gameplay: the boot logos, the Insomniac card, the title fly-in,
and every transition screen between them. That was recorded as a harness property rather than assumed
(`docs/findings/start-skip-map.md`, "the harness gap that hid this"), and it is why the
entrance-animation screen could not even be driven, let alone judged.

`PostArrivalPressConditions` (POST-ARRIVAL, `drive.py --press-after`) exists because the same gap has
a mirror image: every screen between two LEVELS — the level-transition tally, the entrance sweep, the
return-home glide — is reached only after gameplay has started, so the pre-arrival mechanism is
structurally unable to touch any of them. They were therefore unit-tested and never once observed,
which is indistinguishable from not working. A press has to be decidable from observed state on BOTH
sides of arrival, and the difference between the two classes is not the mechanism (it is the same
`Port.tap`) but WHICH states each is allowed to name.

The fix is not a frame count. This file exists because the same reason the file replaced every fixed
`run N` script applies here: the boot/attract sequence is timing dependent, so a press at field 400
lands on the second logo on one run and on the title screen on the next. So a press here is
CONDITION-DRIVEN, decided from the guest's own observed gamestate, the same way the navigator decides
which menu prompt to answer. `--press-while 0:start` means "tap Start on the first occasion the guest
is observed in gamestate 0", which is a boot logo on every run; `--press-after 1:start` means the
same on the level-transition tally.

ONE EDGE PER CONDITION, EVER -- and this was measured, not chosen. The first version of this rule was
"one edge per contiguous run of samples in the named state", so a re-visit re-armed it. A live run
with `--press-while 0:start` then fired TWICE: once at field 20 on the boot logo, and again at field
6180, which is the moment the guest became GS_Playing. That second edge is Start landing in gameplay,
and the guest opened GS_PauseMenu -- the run's own census reported `since GS_Playing: playing=13,
2=13`. It is the hazard `docs/findings/start-skip-map.md` already named ("once the sequence hands off
to gameplay, another pulse opens the pause screen"), reproduced by a driver rather than by a player.
Within the pre-arrival window the ONLY gamestate that recurs is the one the run is driving toward, so
re-arming has no consumer and one hazard; the rule is one edge per condition, and a second visit to
the same screen is not re-armed.

A run samples the gamestate every `Port.SAMPLE_FRAMES` fields, so a screen up for 400 fields is
sampled 40 times; firing per sample would be 40 edges on one screen and the port's `FieldScheduler`
would see a held button where the player pressed once.

PRE-ARRIVAL ONLY, structurally. `PressConditions` is owned by `drive.Navigator` and applied from
`Navigator._advance()`, which only the three pre-arrival phases call, and `reach_gameplay()` returns
before any of them can run again. There is no flag to mis-set and nothing to clear at arrival, so a
pre-arrival script cannot press a button during gameplay — where Start is the pause button and the
next gamestate's producer may not exist at all.

POST-ARRIVAL IS ALLOWED, and it is allowed to run for the whole run, because a transition screen
between two levels exists only after arrival. What it may NOT name is the hazard the run above
measured, and that is refused by name at parse time rather than left to a run to discover:
`GS_Playing` (a press there is gameplay input, and Start opens the pause menu), `GS_PauseMenu` and
`GS_InventoryMenu` (a press there is a menu command — confirm, cancel — not a skip). The states a
post-arrival press exists for are the presentation screens themselves: 1 level transition, 8 dragon,
9 entrance animation, 10 exit level, 11 fairy, 12 balloonist.

    uv run --frozen python tools/press_conditions.py --selftest
"""

from __future__ import annotations

import sys
from dataclasses import dataclass, field
from pathlib import Path

# The gamestates a press condition may name, named rather than numeric so a spec reads as a screen.
# The vocabulary is title_states', the same one drive.py and title_prompts.py use; a second copy is
# how two drivers come to disagree about which screen they are on.
from title_states import (
    GS_BALLOONIST,
    GS_CUTSCENE,
    GS_DRAGON,
    GS_ENTRANCE_ANIMATION,
    GS_EXIT_LEVEL,
    GS_FAIRY,
    GS_INVENTORY_MENU,
    GS_LEVEL_TRANSITION,
    GS_PAUSE_MENU,
    GS_PLAYING,
    GS_TITLE_SCREEN,
)

GAMESTATE_NAMES: dict[int, str] = {
    GS_PLAYING: "GS_Playing",
    GS_LEVEL_TRANSITION: "GS_LevelTransition",
    GS_PAUSE_MENU: "GS_PauseMenu",
    GS_INVENTORY_MENU: "GS_InventoryMenu",
    GS_DRAGON: "GS_Dragon",
    GS_ENTRANCE_ANIMATION: "GS_EntranceAnimation",
    GS_EXIT_LEVEL: "GS_ExitLevel",
    GS_FAIRY: "GS_Fairy",
    GS_BALLOONIST: "GS_Balloonist",
    GS_TITLE_SCREEN: "GS_TitleScreen",
    GS_CUTSCENE: "GS_Cutscene",
}

# The frames a fired edge is held for. The navigator answers menu prompts with `tap button 8`, and a
# shorter edge can be missed by a title owner that samples input every other field.
EDGE_FRAMES = 8


class Refusal(ValueError):
    """A press spec this driver will not guess at."""


@dataclass
class PressCondition:
    """One button, fired on the first sample that finds the guest in `gamestate`."""

    gamestate: int
    button: str
    frames: int = EDGE_FRAMES
    # Set by the first sample that finds the guest in `gamestate`, and never cleared. One edge per
    # condition: see the module docstring for the live run that decided it.
    fired: bool = False
    samples_in_state: int = 0
    times_fired: int = field(default=0)

    def name(self) -> str:
        return f"{self.button} while in {GAMESTATE_NAMES.get(self.gamestate, self.gamestate)}"

    def observe(self, gamestate: int) -> bool:
        """Record one sample; True when this sample is the one that owes an edge."""
        if gamestate != self.gamestate:
            return False
        self.samples_in_state += 1
        if self.fired:
            return False
        self.fired = True
        return True


class PressConditions:
    """A set of condition-triggered pad edges, applied from the pre-arrival navigator loop."""

    def __init__(self, conditions: list[PressCondition] | None = None):
        self.conditions = list(conditions or ())

    def apply(self, port, gamestate: int) -> list[PressCondition]:
        """Fire every edge its condition now owes. `port` needs only `tap(button, frames)`.

        The observed gamestate is passed in rather than read here: the navigator has just sampled it
        to decide its own next step, and a second read would be a second REPL round trip per sample
        for a value already in hand.
        """
        due = [c for c in self.conditions if c.observe(gamestate)]
        for condition in due:
            port.tap(condition.button, condition.frames)
            condition.times_fired += 1
        return due

    def report(self) -> str:
        """What was scanned and what matched, for every condition -- including the ones that never fired.

        A press condition that silently does nothing reads exactly like a screen the game never
        reached, which is the confusion this whole area has been losing time to. So the line names
        the samples each condition saw and the edges it issued, and says so when a condition was
        scanned and matched nothing.
        """
        if not self.conditions:
            return "pre-arrival presses: none requested"
        return "pre-arrival presses: " + _report_lines(self.conditions)


def parse(spec: str) -> PressCondition:
    """`GAMESTATE:BUTTON[:FRAMES]` -> a condition. Every malformed form is refused by name.

    A spec that parses to a state nobody intended is worse than one that is rejected: it would sit
    in the run looking armed and never fire, and the run's own census would not say which of the
    two happened.
    """
    fields = spec.split(":")
    if len(fields) not in (2, 3) or not fields[0].strip() or not fields[1].strip():
        raise Refusal(
            f"press spec {spec!r} is not GAMESTATE:BUTTON[:FRAMES]; "
            f"a gamestate is a number ({', '.join(f'{s}={n}' for s, n in sorted(GAMESTATE_NAMES.items()))})"
        )
    try:
        gamestate = int(fields[0], 0)
    except ValueError:
        raise Refusal(f"press spec {spec!r} names a gamestate that is not a number: {fields[0]!r}") from None
    frames = EDGE_FRAMES
    if len(fields) == 3:
        try:
            frames = int(fields[2], 0)
        except ValueError:
            raise Refusal(f"press spec {spec!r} names a frame count that is not a number: {fields[2]!r}") from None
        if frames < 1:
            raise Refusal(f"press spec {spec!r} asks for {frames} frames; an edge needs at least one")
    return PressCondition(gamestate=gamestate, button=fields[1].strip(), frames=frames)


def parse_all(specs: list[str]) -> PressConditions:
    return PressConditions([parse(spec) for spec in specs])


class PostArrivalPressConditions:
    """Pad edges for the presentation screens that only exist AFTER gameplay has started.

    Same condition, same one-edge rule, same report; the difference is the refusal set below and
    where it is applied. This one is driven by `Port.run()`'s own sampler, so it is live for the
    whole run — which is the point: the level-transition tally, the entrance sweep and the
    return-home glide are entered by a portal or by the pause menu, long after `reach_gameplay()`
    returned, and no amount of arrival-gated tapping can put a button on the right one of them.
    """

    # Named, not numbered, because the refusal has to read as the reason it is a refusal.
    FORBIDDEN: dict[int, str] = {
        GS_PLAYING: "GS_Playing is where a pad button is gameplay input, and Start there opens the "
                    "pause menu -- the measured failure this whole file exists to not repeat",
        GS_PAUSE_MENU: "GS_PauseMenu reads Start and Cross as menu commands (confirm, resume), not "
                       "as a skip",
        GS_INVENTORY_MENU: "GS_InventoryMenu reads Start and Cross as menu commands (close, back)",
    }

    def __init__(self, conditions: list[PressCondition] | None = None):
        self.conditions = list(conditions or ())

    def apply(self, port, gamestate: int) -> list[PressCondition]:
        """Fire every edge its condition now owes, on the same seam as the pre-arrival set."""
        due = [c for c in self.conditions if c.observe(gamestate)]
        for condition in due:
            port.tap(condition.button, condition.frames)
            condition.times_fired += 1
        return due

    def report(self) -> str:
        if not self.conditions:
            return "post-arrival presses: none requested"
        return "post-arrival presses: " + _report_lines(self.conditions)


def parse_post(spec: str) -> PressCondition:
    """`GAMESTATE:BUTTON[:FRAMES]` for a post-arrival press, refusing a state a button cannot skip.

    A post-arrival script is live for the whole run, so a spec naming GS_Playing would fire at the
    first gameplay sample of the run and open the pause menu -- the same failure the pre-arrival set
    avoids structurally. Refusing it here means the mistake is a parser error naming the reason,
    not a log line three minutes into a run.
    """
    condition = parse(spec)
    reason = PostArrivalPressConditions.FORBIDDEN.get(condition.gamestate)
    if reason is not None:
        raise Refusal(
            f"--press-after {spec!r} names {GAMESTATE_NAMES.get(condition.gamestate, condition.gamestate)}, "
            f"which a post-arrival press may not touch: {reason}"
        )
    return condition


def parse_post_all(specs: list[str]) -> PostArrivalPressConditions:
    return PostArrivalPressConditions([parse_post(spec) for spec in specs])


def _report_lines(conditions: list[PressCondition]) -> str:
    parts = []
    for condition in conditions:
        part = f"{condition.name()} fired {condition.times_fired} time(s) in {condition.samples_in_state} sample(s)"
        if not condition.times_fired:
            part += " -- scanned, never matched"
        parts.append(part)
    return "; ".join(parts)


class _FakePort:
    """Records the edges it was asked for, so the selftest asserts on the pad, not on the state."""

    def __init__(self, gamestates: list[int] | None = None) -> None:
        self.edges: list[tuple[str, int]] = []
        self._gamestates = list(gamestates or ())
        self.frame = 0
        self._last = -1

    def tap(self, button: str, frames: int) -> None:
        self.edges.append((button, frames))

    # Only the two members drive.Navigator._advance() touches, so a fake that provides exactly those
    # is enough to test the seam. If _advance grows a third dependency, this stops being a fake and
    # says so, rather than passing against a Port it no longer resembles.
    def run(self, frames: int) -> None:
        self._last = self._gamestates.pop(0) if self._gamestates else -1
        self.frame += frames

    @property
    def last_sampled_gamestate(self) -> int:
        return self._last


def _selftest_navigator_seam() -> None:
    """The WIRING, not the policy: the pre-arrival navigator must APPLY the script, and must decide
    it against the state it just sampled rather than a fresh read of a state it has not reached.

    The fake carries exactly the members `Navigator._advance()` touches. If that method grows a third
    dependency this stops being a fake and fails loudly, rather than passing against a Port it no
    longer resembles.
    """
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import drive  # imported here, not at module scope: drive imports THIS module

    port = _FakePort([GS_TITLE_SCREEN, GS_TITLE_SCREEN, GS_PLAYING])
    presses = PressConditions([PressCondition(GS_TITLE_SCREEN, "start")])
    navigator = drive.Navigator(port, presses=presses)
    navigator._advance()
    assert port.edges == [("start", EDGE_FRAMES)], port.edges
    navigator._advance()
    assert port.edges == [("start", EDGE_FRAMES)], \
        f"a second sample of the same screen must not re-fire: {port.edges}"
    print(f"  Navigator._advance over 3 samples -> {port.edges} after {port.frame} field(s)")


def _selftest_port_seam() -> None:
    """The post-arrival WIRING: `Port.run()` must apply the post-arrival script from the state it
    just sampled, because that is the only sampler live while the run is walking a level.

    Driven through the real `drive.Port` against a fake process, not through a stand-in: the seam
    being tested is Port's own chunking loop, and a fake that reimplemented it would test the fake.
    """
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import drive

    port = drive.Port.__new__(drive.Port)
    port._post_presses = PostArrivalPressConditions([PressCondition(GS_LEVEL_TRANSITION, "start")])
    port.gamestate_census = {}
    port._last_sampled_gamestate = -1
    port.frame = 0
    port.edges: list[tuple[str, int]] = []
    port.tap = lambda button, frames=4: port.edges.append((button, frames))  # type: ignore[method-assign]
    port.gamestate = lambda: GS_LEVEL_TRANSITION  # type: ignore[method-assign]
    port._send = lambda line: port.chunks.append(line)  # type: ignore[method-assign]
    port._await_prompt = lambda: port.frame  # type: ignore[method-assign]
    port.chunks: list[str] = []

    port.run(30)  # three 10-field samples, every one of them in GS_LevelTransition
    assert port.chunks == ["run 10", "run 10", "run 10"], port.chunks
    assert port.edges == [("start", EDGE_FRAMES)], \
        f"one edge out of three samples of the same screen, and no re-arm: {port.edges}"
    print(f"  Port.run(30) in GS_LevelTransition -> chunks {port.chunks}, edges {port.edges}")

    # The pause menu's two words are 0xA8 apart, and reading them as one contiguous pair returns a
    # submenu flag that is not there. This is here because that bug was WRITTEN and shipped once: the
    # route then reads a garbage submenu and either backs out of an options menu nobody opened or
    # walks straight past it. The assertion is on the ADDRESSES asked for, not on the values.
    asked: list[tuple[int, int]] = []
    port.word = lambda address: (asked.append((address, 1)) or 0)  # type: ignore[method-assign]
    port.words = lambda address, count=1: (asked.append((address, count)) or [0] * count)  # type: ignore[method-assign]
    assert port.pause_menu() == (0, 0), port.pause_menu()
    assert asked == [(drive.G_PAUSE_OPTION, 1), (drive.G_PAUSE_SUBMENU, 1)], asked
    assert drive.G_PAUSE_SUBMENU - drive.G_PAUSE_OPTION == 0xA8, \
        f"the two words are not where the pause-menu update writes them: {hex(drive.G_PAUSE_OPTION)} " \
        f"{hex(drive.G_PAUSE_SUBMENU)}"
    print(f"  Port.pause_menu -> reads {[hex(a) for a, _ in asked]}")


def _selftest() -> int:
    failures = 0

    # A condition on a state the run DOES reach fires on the first sample that finds it, exactly
    # once, and holds the edge for EDGE_FRAMES fields.
    port = _FakePort()
    presses = PressConditions([PressCondition(GS_TITLE_SCREEN, "start")])
    for _ in range(40):  # 40 samples = 400 fields of one screen up
        presses.apply(port, GS_TITLE_SCREEN)
    assert port.edges == [("start", EDGE_FRAMES)], port.edges
    assert presses.conditions[0].times_fired == 1, presses.conditions[0]
    assert presses.conditions[0].samples_in_state == 40, presses.conditions[0]
    # The report carries the DENOMINATOR, not just the count of presses, and the whole line is
    # pinned: a report that said "fired 1 time(s)" and stopped would answer the question nobody
    # asked, because one press is equally consistent with a screen up for one sample and for four
    # hundred fields.
    assert presses.report() == ("pre-arrival presses: start while in GS_TitleScreen fired 1 time(s) "
                                "in 40 sample(s)"), presses.report()
    print(f"  one screen, 40 samples -> {len(port.edges)} edge(s): {port.edges}")

    # The negative that matters most: a state this run never enters scans and matches NOTHING, and
    # says so. Without the count, "no press happened" and "the screen never appeared" are the same
    # line of evidence.
    port = _FakePort()
    presses = PressConditions([PressCondition(GS_ENTRANCE_ANIMATION, "start")])
    for state in (GS_PLAYING, GS_TITLE_SCREEN, GS_LEVEL_TRANSITION, GS_PLAYING):
        presses.apply(port, state)
    assert port.edges == [], port.edges
    assert presses.conditions[0].samples_in_state == 0, presses.conditions[0]
    assert presses.report() == ("pre-arrival presses: start while in GS_EntranceAnimation fired 0 "
                                "time(s) in 0 sample(s) -- scanned, never matched"), presses.report()
    print(f"  a state never reached -> {presses.report()}")

    # A second visit to the same screen is NOT re-armed. The rule was "once per contiguous run"
    # until a live run fired a second edge at the GS_Playing hand-off and the guest entered
    # GS_PauseMenu; three visits to one screen therefore yield exactly one edge here, and the five
    # samples the condition saw are still counted, because "pressed once out of five samples" and
    # "pressed once out of one" are different runs.
    port = _FakePort()
    presses = PressConditions([PressCondition(GS_LEVEL_TRANSITION, "start")])
    for state in (GS_LEVEL_TRANSITION, GS_LEVEL_TRANSITION, GS_PLAYING,
                  GS_LEVEL_TRANSITION, GS_LEVEL_TRANSITION, GS_PLAYING,
                  GS_LEVEL_TRANSITION):
        presses.apply(port, state)
    assert port.edges == [("start", EDGE_FRAMES)], port.edges
    assert presses.conditions[0].samples_in_state == 5, presses.conditions[0]
    print(f"  three visits to one screen -> {len(port.edges)} edge(s) out of "
          f"{presses.conditions[0].samples_in_state} samples: no re-arm")

    # TWO conditions on two screens each fire once, on their own state, in the order the run met
    # them. Gamestate 0 is the case the rule exists for: it is boot AND the arrival state, so the
    # condition that names it must fire at the boot logo and never again at the hand-off.
    port = _FakePort()
    presses = parse_all(["13:start", "0:cross"])
    for state in (GS_PLAYING, GS_TITLE_SCREEN, GS_PLAYING, GS_TITLE_SCREEN):
        presses.apply(port, state)
    assert port.edges == [("cross", EDGE_FRAMES), ("start", EDGE_FRAMES)], port.edges
    print(f"  two conditions over four samples -> {port.edges}")

    # The spec parser refuses every malformed form BY NAME rather than accepting one and letting it
    # sit in the run looking armed.
    for bad in ("13", "13:", ":start", "thirteen:start", "13:start:zero", "13:start:0", ""):
        try:
            parse(bad)
        except Refusal as refusal:
            print(f"  refuses {bad!r}: {refusal}")
        else:
            print(f"SELFTEST FAILED: {bad!r} was accepted", file=sys.stderr)
            failures += 1
    # ... and accepts the one well-formed shape, including an explicit frame count.
    good = parse("9:start:12")
    assert (good.gamestate, good.button, good.frames) == (9, "start", 12), good
    assert good.name() == "start while in GS_EntranceAnimation", good.name()
    assert good.observe(9) is True and good.observe(9) is False and good.observe(0) is False
    assert good.observe(9) is False, "a re-visit must not re-arm"
    print(f"  accepts 9:start:12 -> {good.name()}")

    # No conditions requested is a real, reportable state, not an empty string.
    assert "none requested" in PressConditions().report(), PressConditions().report()
    print("  no conditions -> none requested")

    # --- post-arrival -------------------------------------------------------
    # The hazard a post-arrival script would otherwise carry is refused BY NAME, at parse time: a
    # spec naming GS_Playing is live for the whole run, so it would fire at the first gameplay
    # sample and open the pause menu. The pre-arrival set avoids that structurally; this one has to
    # refuse it, and the refusal says which state and why.
    for bad, why in ((f"{GS_PLAYING}:start", "GS_Playing"), (f"{GS_PAUSE_MENU}:cross", "GS_PauseMenu"),
                     (f"{GS_INVENTORY_MENU}:start", "GS_InventoryMenu")):
        try:
            parse_post(bad)
        except Refusal as refusal:
            assert why in str(refusal), f"the refusal must name the state: {refusal}"
            print(f"  refuses --press-after {bad!r}: {refusal}")
        else:
            print(f"SELFTEST FAILED: --press-after {bad!r} was accepted", file=sys.stderr)
            failures += 1

    # The screens a post-arrival press EXISTS for are accepted, and fire once each from the sampler.
    port = _FakePort()
    post = parse_post_all(["1:start", "10:cross:30"])
    for state in (GS_PLAYING, GS_LEVEL_TRANSITION, GS_LEVEL_TRANSITION, GS_PLAYING,
                  GS_ENTRANCE_ANIMATION, GS_ENTRANCE_ANIMATION, GS_EXIT_LEVEL):
        post.apply(port, state)
    assert port.edges == [("start", EDGE_FRAMES), ("cross", 30)], port.edges
    assert post.conditions[1].samples_in_state == 1, post.conditions[1]
    assert post.report() == ("post-arrival presses: start while in GS_LevelTransition fired 1 "
                             "time(s) in 2 sample(s); cross while in GS_ExitLevel fired 1 time(s) "
                             "in 1 sample(s)"), post.report()
    print(f"  two post-arrival conditions -> {port.edges}")
    # ... and the one it never saw, which is the reading a clean run most often gets wrong.
    assert "start while in GS_EntranceAnimation" not in post.report()
    print(f"  an unreached post-arrival screen is still named: {post.report()}")

    # Both phases refuse the same malformed specs: the post-arrival parser is the pre-arrival parser
    # plus the refusal set, and a spec that one accepts and the other rejects would be a trap.
    for bad in ("1", "1:", ":start", "one:start", "1:start:0", ""):
        try:
            parse_post(bad)
        except Refusal as refusal:
            print(f"  --press-after refuses {bad!r}: {refusal}")
        else:
            print(f"SELFTEST FAILED: --press-after {bad!r} was accepted", file=sys.stderr)
            failures += 1
    assert "none requested" in PostArrivalPressConditions().report()
    print("  no post-arrival conditions -> none requested")

    # The seams: the pre-arrival navigator and the post-arrival sampler both apply what they are given.
    _selftest_navigator_seam()
    _selftest_port_seam()

    if failures:
        print(f"press_conditions selftest FAILED: {failures} case(s)", file=sys.stderr)
        return 1
    print("press_conditions selftest PASS")
    return 0


if __name__ == "__main__":
    if "--selftest" not in sys.argv[1:]:
        print(__doc__.strip().splitlines()[-1].strip(), file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(_selftest())
