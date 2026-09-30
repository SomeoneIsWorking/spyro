#!/usr/bin/env python3
"""repl_walk.py — walk Spyro to a place by pressing the pad over the port's REPL.

WHY THIS IS ITS OWN MODULE. The walking stepper used to live in drive.py, which drove ONE route
shape: get to GS_Playing, seek an optional destination, then apply a fixed list of holds and taps.
The scene routes (tools/route_scenes.py) walk the same way and have to refuse the same way, so
keeping the stepper inside drive.py would make route_scenes import drive.py, which imports
route_scenes. The stepper needs no core of its own -- it drives anything with press/tap/run/words
-- so it is its own owner and both callers use it.

`spyro1_steering.Walk` owns WHERE to press and when a destination is abandoned; this module owns
pressing, running a fixed number of FIELDS, and releasing, and it reports the distance actually
reached. The split is the one spyro1_steering's own docstring already describes: the route policy
is core-independent, the stepping belongs to the driver.

Nothing here writes guest memory. Every step is press / run / release, the same edges a player uses.
"""

from __future__ import annotations

import sys
from typing import Callable, Protocol

import spyro1_steering
from spyro1_steering import Refusal, Walk

# Every target the walk policy was given, named and positioned, before any walking starts. A centre
# in the wrong units or read from the wrong field looks exactly like an unreachable destination once
# the walk starts, so a refused route can say what it was aiming at.
Target = spyro1_steering.Target


class ReplPort(Protocol):
    """The slice of tools/drive.py's Port this module drives. A duck type on purpose: the stepper
    must not import the driver, and the driver must not have to know what a walk is."""

    def press(self, button: str) -> None: ...
    def tap(self, button: str, frames: int = ...) -> None: ...
    def run(self, frames: int) -> int: ...
    def words(self, address: int, count: int = ...) -> list[int]: ...


class Seeker:
    """Walk the product to one of a set of world positions, steering from the guest's own camera.

    Holding one direction cannot reach anything: the pad is camera-relative, and a homeworld's
    destinations sit tens of thousands of units away on a bearing the start position does not face.
    A fixed input list cannot reach them either, for the same reason every fixed route in this
    project was replaced.

    A destination that must make something HAPPEN -- a portal entry, a fairy cutscene, not a visit
    -- gives its own predicate, and the walk keeps closing the gap until that predicate holds.
    Ending at a proximity radius would report "reached" for a walk that never entered anything.
    """

    STEP = 24  # fields per steering decision; a whole walk cycle, short enough to correct a wall

    def __init__(self, port: ReplPort, what: str, targets, budget: int = 6000,
                 arrived: int | None = None, stop: Callable[[], bool] | None = None,
                 stop_is: str = ""):
        self._port = port
        self._walk = Walk(what, list(targets), arrived=arrived)
        self._budget = budget
        self._stop = stop
        self._stop_is = stop_is or "the destination"

    def walk(self) -> int:
        """The view-space distance actually reached, or refuse by name."""
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
