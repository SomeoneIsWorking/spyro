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

import guest_globals
import spyro1_steering
from spyro1_steering import Refusal, Walk

# common.h's Gamestate values the walker has to know about. Every other non-zero state (level
# transition, respawn, game over, fairy) is something the walk's own `stop` predicate names or
# something that ends the walk, so it is only waited out, never pressed through.
GS_PLAYING = 0
GS_DRAGON = 8  # the "Press Cross" dragon cutscene that opens a homeworld; it takes Cross to leave

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

    `hop_within` makes the walk jump on every decision inside that view-space distance. A fairy
    hangs a jump above the ground and her trigger has a height window, so walking under her
    never opens it (g_Spyro's z has to be within 0x200 of hers): a player jumps at her, and so
    does the walk.
    """

    # Fields the guest may spend outside GS_Playing before the walk refuses. The opening dragon
    # dialogue alone is ~1600 fields of Cross taps (measured), and it is not walking, so it is
    # bounded on its own instead of eating the walking budget.
    CUTSCENE_BUDGET = 6000
    JUMP_PRESS = 3  # fields Cross is held before it is released
    JUMP_SETTLE = 4  # fields with nothing held between the release and the direction
    JUMP_AIRTIME = 60  # fields during which a jump is not started again
    STEP = 24  # fields per steering decision; a whole walk cycle, short enough to correct a wall

    def __init__(self, port: ReplPort, what: str, targets, budget: int = 6000,
                 arrived: int | None = None, stop: Callable[[], bool] | None = None,
                 stop_is: str = "", step: int | None = None,
                 from_player: bool = False, hop_within: int = 0, hop: bool = True,
                 detour: bool = True):
        self._port = port
        self._budget = budget
        self._stop = stop
        self._stop_is = stop_is or "the destination"
        self._waited = 0
        self._jump_until = 0
        self._step = self.STEP if step is None else step
        self._walk = Walk(what, list(targets), arrived=arrived, step_scale=self._step / self.STEP,
                          detour=detour)
        self._hop_within = hop_within
        self._hop = hop
        self._held: set[str] = set()
        self._view = spyro1_steering.player_view if from_player else spyro1_steering.camera

    def _cutscene_step(self) -> bool:
        """Advance one step through a guest state in which the pad does not steer Spyro.

        Walking in a cutscene is not walking: Spyro does not move, so the stall rule would read
        the frozen frames as a wall and abandon a destination the level never blocked. The
        gamestate is the guest's own word, so the walker defers to it. The dragon cutscene is left
        the way a player leaves it, with Cross; every other state is waited out.
        """
        state = self._port.words(guest_globals.kGamestate, 1)[0]
        if state == GS_PLAYING:
            return False
        if self._stop is not None and self._stop():
            return False
        self._walk.rebase()
        self._hold(set())
        self._waited += self._step
        if self._waited > self.CUTSCENE_BUDGET:
            raise Refusal(
                f"the guest stayed in gamestate {state} for {self._waited} field(s) without "
                "returning to GS_Playing, so the pad is not steering anything"
            )
        print(f"seek: gamestate {state}, not steering", file=sys.stderr)
        if state == GS_DRAGON:
            self._port.tap("cross", 8)
        self._port.run(self._step)
        return True

    def _hold(self, wanted: set[str]) -> None:
        """Make exactly `wanted` the buttons held: press what is new, release what is stale."""
        for button in self._held - wanted:
            self._port.release(button)
        for button in wanted - self._held:
            self._port.press(button)
        self._held = wanted

    def walk(self) -> int:
        """The view-space distance actually reached, or refuse by name. The pad is released on every
        way out, so a walk that ends leaves nothing held for the next one."""
        try:
            return self._walk_loop()
        finally:
            self._hold(set())

    def _walk_loop(self) -> int:
        for target in self._walk.remaining:
            print(f"seek: {target.what} at {target.position}", file=sys.stderr)
        spent = 0
        while spent < self._budget:
            if self._cutscene_step():
                continue
            if self._stop is not None and self._stop():
                print(f"seek: {self._stop_is} after {spent} field(s), "
                      f"{self._walk.closest} from the nearest {self._walk.what}", file=sys.stderr)
                return self._walk.closest or 0
            decision = self._walk.next(self._view(self._port.words))
            if decision is None:
                print(f"seek: reached {self._walk.what} at {self._walk.closest}", file=sys.stderr)
                return self._walk.closest
            print(f"seek: {decision.describe()}", file=sys.stderr)
            # A jump is a Cross PRESS-AND-RELEASE with the direction joining after it. Measured at
            # a pond's curb: Cross held (alone or alongside the direction) never leaves the ground
            # in 8 decisions, while Cross pressed for 3 fields and released, then the direction,
            # clears the curb on the next ascent. The direction is not held during the press
            # because `tap` would replace the pad; the release is the edge the guest acts on.
            jump = (decision.hop and self._hop) or decision.distance <= self._hop_within
            airborne = spent < self._jump_until
            if jump and not airborne:
                self._hold({"cross"})
                self._port.run(self.JUMP_PRESS)
                self._hold(set())
                self._port.run(self.JUMP_SETTLE)
                spent += self.JUMP_PRESS + self.JUMP_SETTLE
                self._jump_until = spent + self.JUMP_AIRTIME
            self._hold(set(decision.buttons))
            self._port.run(self._step)
            spent += self._step
        self._hold(set())
        if self._stop is not None:
            raise Refusal(
                f"walked to within {self._walk.closest} of a {self._walk.what} over {spent} "
                f"field(s), but {self._stop_is} never happened: being next to one is not entering it"
            )
        raise self._walk.exhausted(spent)
