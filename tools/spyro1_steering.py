#!/usr/bin/env python3
"""spyro1_steering.py — which way to press to walk at a place, and where this level's places are.

WHY THIS EXISTS. Two drivers walk Spyro 1. tools/drive.py explores interactively, on the product
alone. The oracle's level-entry checkpoint (tools/oracle_spyro1.py) has to walk the product AND the
independent console reference along the same route, one after the other, and then compare where
they ended up. A second copy of this transform would let those two cores take different routes and
call the result agreement — the exact failure the shared guest-address owner was created to end,
one level up.

So the geometry lives here once: read this level's destinations, convert one into the renderer's
own view space, and name the pad direction whose sector contains its bearing. The walking itself
stays with each driver, because they step their cores differently.

Every reader takes a `words(address, count) -> list[int]` callable, which is all either core
exposes in common; nothing here writes guest memory.

    uv run --frozen python tools/spyro1_steering.py --selftest
"""

from __future__ import annotations

import math
import sys
from dataclasses import dataclass
from typing import Callable

import guest_globals

Words = Callable[[int, int], list[int]]

# Record layouts. The addresses are the shipping owner's; these offsets are read only here.
MOBY_BYTES = 0x58
MOBY_CLASS_WORD = 52   # the class is the high half of this word
MOBY_STATE = 72
MOBY_POSITION = 12     # three signed words
MAX_LEVEL_MOBYS = 1024
PORTAL_CENTER = 0x20   # Portal::m_Center, after the skybox pointer, counts and world sector
PORTAL_LEVEL = 0x1C
PORTAL_SLOTS = 6
CAMERA_WORDS = 14      # 5 packed matrix words, filler, then the position at +0x28

RAM_BEGIN, RAM_END = 0x80000000, 0x80200000

# 0 deg is straight ahead and +90 is screen-right. Each entry is the upper bound of a 45-degree
# sector and the direction at that sector's CENTRE, so straight ahead presses up and nothing else.
# (This table used to be rotated one sector: bearing 0 pressed up+right, bearing 180 pressed
# down+left, and every walk paid for it in detours that looked like level geometry.)
SECTORS = (
    (-157.5, ("down",)),
    (-112.5, ("down", "left")),
    (-67.5, ("left",)),
    (-22.5, ("up", "left")),
    (22.5, ("up",)),
    (67.5, ("up", "right")),
    (112.5, ("right",)),
    (157.5, ("down", "right")),
)


class Refusal(RuntimeError):
    """The guest state did not look like what this reader is for, with the evidence seen."""


def signed(value: int) -> int:
    return value - 0x100000000 if value >= 0x80000000 else value


@dataclass(frozen=True)
class Target:
    """A place worth walking to, and what named thing it is."""

    what: str
    position: tuple[int, int, int]


@dataclass(frozen=True)
class View:
    """The camera as the renderer holds it: a 3x3 fixed-point matrix and a world position."""

    matrix: tuple[tuple[int, int, int], ...]
    position: tuple[int, int, int]

    def bearing(self, target: tuple[int, int, int]) -> tuple[int, float]:
        """Ground distance to `target` in view-space units, and its bearing in degrees."""
        view = self._view(target)
        return int(math.hypot(view[0], view[2])), math.degrees(math.atan2(view[0], view[2]))

    def nearest(self, targets) -> tuple[int, int, float]:
        """Index, distance and bearing of the closest of `targets`; refuses an empty set rather
        than reporting a distance of infinity that reads like an unreachable destination."""
        if not targets:
            raise Refusal("nothing to steer at: the target list is empty")
        best = None
        for index, target in enumerate(targets):
            distance, bearing = self.bearing(target.position)
            if best is None or distance < best[1]:
                best = (index, distance, bearing)
        return best

    def _view(self, position) -> list[int]:
        # The renderer's own packing: X is target-minus-camera, Y and Z are camera-minus-target, all
        # arithmetic-shifted down two, and the matrix rows consume them in Y/Z/X order.
        relative = (
            (position[0] - self.position[0]) >> 2,
            (self.position[1] - position[1]) >> 2,
            (self.position[2] - position[2]) >> 2,
        )
        source = (relative[1], relative[2], relative[0])
        return [sum(self.matrix[row][i] * source[i] for i in range(3)) >> 12 for row in range(3)]


def sector(bearing: float) -> tuple[str, ...]:
    """The pad direction whose compass sector contains `bearing`."""
    for limit, buttons in SECTORS:
        if bearing <= limit:
            return buttons
    return ("down",)  # past +157.5 the compass wraps back into the first sector


def camera(words: Words) -> View:
    """The camera's matrix and position, read the way the renderer packs them."""
    observed = words(guest_globals.kCamera, CAMERA_WORDS)

    def half(value: int, high: bool) -> int:
        part = (value >> 16) & 0xFFFF if high else value & 0xFFFF
        return part - 0x10000 if part >= 0x8000 else part

    matrix = (
        (half(observed[0], False), half(observed[0], True), half(observed[1], False)),
        (half(observed[1], True), half(observed[2], False), half(observed[2], True)),
        (half(observed[3], False), half(observed[3], True), half(observed[4], False)),
    )
    return View(matrix, tuple(signed(observed[10 + i]) for i in range(3)))


def portal_targets(words: Words) -> list[Target]:
    """Centres of this level's portals, which are a loaded table rather than Mobys.

    A homeworld's portals are the only way into a level. g_Portals holds up to six pointers and
    g_PortalCount says how many are live; each Portal carries its own centre after the skybox
    pointer, point count, unknown vector and world sector.
    """
    count = words(guest_globals.kPortalCount, 1)[0]
    if not 0 <= count <= PORTAL_SLOTS:
        raise Refusal(f"g_PortalCount holds {count}, which is outside the table's {PORTAL_SLOTS} slots")
    found: list[Target] = []
    for index in range(count):
        portal = words(guest_globals.kPortals + index * 4, 1)[0]
        if not RAM_BEGIN <= portal < RAM_END:
            raise Refusal(
                f"g_Portals[{index}] holds 0x{portal:08X}, which is not a RAM address, so the "
                "portal table was read before the level finished loading"
            )
        level = signed(words(portal + PORTAL_LEVEL, 1)[0])
        center = tuple(signed(v) for v in words(portal + PORTAL_CENTER, 3))
        found.append(Target(f"portal to level {level}", center))
    return found


def moby_class_targets(words: Words, wanted_class: int) -> list[Target]:
    """World positions of every live Moby of a class. Read once: level Mobys do not walk away."""
    first = words(guest_globals.kLevelMobys, 1)[0]
    if not RAM_BEGIN <= first < RAM_END:
        raise Refusal(f"g_LevelMobys holds 0x{first:08X}, which is not a RAM address")
    seen: list[int] = []
    found: list[Target] = []
    index = 0
    while index < MAX_LEVEL_MOBYS:
        need = (index + 1) * MOBY_BYTES // 4 + 1
        while len(seen) < need:
            seen.extend(words(first + len(seen) * 4, 64))
        base = index * MOBY_BYTES // 4
        state = seen[base + MOBY_STATE // 4]
        if state & 0x80:
            if (state & 0xFF) == 0xFF:
                break
            index += 1
            continue
        if (seen[base + MOBY_CLASS_WORD // 4] >> 16) & 0xFFFF == wanted_class:
            found.append(Target(f"class {wanted_class}",
                                tuple(signed(seen[base + MOBY_POSITION // 4 + i]) for i in range(3))))
        index += 1
    return found


@dataclass(frozen=True)
class Decision:
    """What to press for the next stretch of walking, and why."""

    buttons: tuple[str, ...]
    hop: bool
    distance: int
    bearing: float
    detour: int

    def describe(self) -> str:
        return (f"{self.distance} away, bearing {self.bearing:+.0f}"
                f"{f' detour {self.detour:+d}' if self.detour else ''}"
                f"{' (hopping)' if self.hop else ''} -> {'+'.join(self.buttons)}")


class Walk:
    """The route policy: close the gap, detour around what blocks it, hop onto what it stands on,
    and abandon a target that never gets closer.

    It owns no core and takes no steps. A driver reads the camera, asks what to press, presses it
    for as long as that driver's step is, and asks again. That is what lets the interactive driver
    and the oracle — which step their cores in different units, fields and game frames — walk the
    same route instead of two routes that happen to share a comment.
    """

    ARRIVED = 500       # view-space units (~1/4 of world scale); close enough to be on screen
    STALL_STEPS = 15    # decisions without closing the gap before this target is abandoned
    PROGRESS = 200      # view-space units that count as having closed the gap
    # Decisions of no progress before the walk starts hopping. Portals sit on raised platforms, so
    # steering alone circles the base forever; a jump is ordinary traversal, not a state write.
    JUMP_AFTER = 3
    # Bearing offsets tried in turn once the straight line stops closing the gap. Walking around an
    # obstacle is what a player does; pressing harder into the same wall is not. Index 0 is the
    # straight line, so an unobstructed walk never detours.
    DETOURS = (0, 60, -60, 120, -120)

    def __init__(self, what: str, targets: list[Target], arrived: int | None = None):
        if not targets:
            raise Refusal(
                f"nothing to walk at: this level carries no {what}, so the destination is wrong or "
                "this is not the level that carries it"
            )
        self.what = what
        # A driver that must make something HAPPEN at the destination — walking into a portal, not
        # standing near it — passes arrived=0 so the walk keeps closing the gap until its own
        # predicate fires, and the stall rule moves it to the next destination if nothing does.
        self.arrived = self.ARRIVED if arrived is None else arrived
        self.remaining = list(targets)
        self.abandoned: list[str] = []
        self.best: int | None = None
        self.closest: int | None = None
        self.stalled = 0
        self.decisions = 0

    def next(self, view: View) -> Decision | None:
        """The next decision, or None once the nearest target is within ARRIVED."""
        while True:
            if not self.remaining:
                raise Refusal(
                    f"every {self.what} is unreachable from here: {'; '.join(self.abandoned)}. The "
                    "route needs a different entry, not more walking"
                )
            index, distance, bearing = view.nearest(self.remaining)
            self.closest = distance
            if distance <= self.arrived:
                return None
            if self.best is None or distance < self.best - self.PROGRESS:
                self.best, self.stalled = distance, 0
                break
            self.stalled += 1
            if self.stalled < self.STALL_STEPS:
                break
            # Blocked by geometry or standing under a ledge. Retail levels put several destinations
            # around one hub, so taking the next nearest is a real route; pressing harder into the
            # same wall is not.
            self.abandoned.append(f"{self.remaining[index].what} stuck at {distance}")
            self.remaining.pop(index)
            self.best, self.stalled = None, 0
        detour = self.DETOURS[self.stalled % len(self.DETOURS)] if self.stalled else 0
        steered = (bearing + detour + 180.0) % 360.0 - 180.0
        self.decisions += 1
        return Decision(sector(steered), self.stalled >= self.JUMP_AFTER, distance, bearing, detour)

    def exhausted(self, steps: int) -> Refusal:
        """The refusal a driver raises when its own budget runs out, with the distance reached."""
        return Refusal(
            f"the nearest {self.what} was still about {self.closest} away after {steps} steering "
            f"step(s) and {self.decisions} decision(s)"
        )


def _selftest() -> int:
    """Both answers: a camera that must produce a known bearing, and readers that must refuse."""
    identity = View(((0x1000, 0, 0), (0, 0x1000, 0), (0, 0, 0x1000)), (0, 0, 0))
    # The rows consume (dy, dz, dx) in that order, so a camera looking down -Z takes view X from
    # the third column and view Z from the second: straight ahead is 0 degrees, screen-right +90.
    ahead = View(((0, 0, 0x1000), (0x1000, 0, 0), (0, 0x1000, 0)), (0, 0, 0))
    straight = ahead.bearing((0, 0, -4000))
    right = ahead.bearing((4000, 0, 0))
    assert abs(straight[1]) < 1e-6, straight
    assert abs(right[1] - 90.0) < 1e-6, right
    assert sector(straight[1]) == ("up",), sector(straight[1])
    assert sector(right[1]) == ("right",), sector(right[1])
    assert sector(-90.0) == ("left",), sector(-90.0)
    assert sector(180.0) == ("down",), sector(180.0)
    assert sector(45.0) == ("up", "right"), sector(45.0)
    assert identity.bearing((0, 0, 0))[0] == 0
    print(f"  straight ahead {straight}, screen-right {right}")

    for label, failing in {
        "an empty target set": lambda: identity.nearest([]),
        "a portal count out of range": lambda: portal_targets(lambda a, n: [99] * n),
        "a portal table read too early": lambda: portal_targets(
            lambda a, n: [1] * n if a == guest_globals.kPortalCount else [0] * n),
        "a Moby array that is not in RAM": lambda: moby_class_targets(lambda a, n: [0] * n, 83),
    }.items():
        try:
            failing()
        except Refusal as refusal:
            print(f"  refuses {label}: {refusal}")
        else:
            print(f"SELFTEST FAILED: {label} was accepted", file=sys.stderr)
            return 1
    # The walk policy: a target that never gets closer is abandoned, and the last one refuses.
    still = View(((0, 0, 0x1000), (0x1000, 0, 0), (0, 0x1000, 0)), (0, 0, 0))
    walk = Walk("portal", [Target("portal to level 11", (0, 0, -40000))])
    decisions = [walk.next(still) for _ in range(Walk.STALL_STEPS)]
    assert all(d is not None for d in decisions), decisions
    assert decisions[0].detour == 0 and not decisions[0].hop, decisions[0]
    assert decisions[Walk.JUMP_AFTER].hop, decisions[Walk.JUMP_AFTER]
    try:
        walk.next(still)
    except Refusal as refusal:
        print(f"  refuses a walk whose every target stalled: {refusal}")
    else:
        print("SELFTEST FAILED: a fully stalled walk kept walking", file=sys.stderr)
        return 1
    close = Walk("portal", [Target("portal to level 11", (0, 0, -1000))])
    assert close.next(still) is None, "a target inside ARRIVED must end the walk"
    into = Walk("portal", [Target("portal to level 11", (0, 0, -1000))], arrived=0)
    assert into.next(still) is not None, "arrived=0 must keep walking into the destination"
    print(f"  arrives at {close.closest}, abandons after {Walk.STALL_STEPS} stalled decisions")
    print("spyro1_steering selftest PASS")
    return 0


if __name__ == "__main__":
    if "--selftest" not in sys.argv[1:]:
        print(__doc__.strip().splitlines()[-1].strip(), file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(_selftest())
