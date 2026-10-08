#!/usr/bin/env python3
"""repl_route.py — follow a recorded waypoint route through a level, then hand over to the seeker.

WHY THIS EXISTS. tools/repl_walk.py's Seeker closes the straight-line gap to a destination, which
is all it can do: it has no map. A homeworld's portals and fairies sit behind rims, ramps and
rings that no straight line crosses, so the seeker stalls at the base of the first wall and the
corpus's flight, boss and fairy scenes never arrive (docs/issues/0147).

A route is a list of world positions through walkable ground, recorded from the level's own render
mesh (tools/routes/*.json). Following it is the same press / run / release the seeker does, one
Seeker per leg, so cutscene handling and the stall rule live in one place. A route is only valid
for the level it was recorded in: the file names its level id and the follower refuses to walk it
anywhere else, because a route followed in the wrong level is a walk into a wall that looks like a
defect in the walker.

Nothing here writes guest memory, and no leg is timed: a leg ends when the guest's own camera
and position words put Spyro within ARRIVED of the waypoint.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Callable

import guest_globals
import repl_walk
import spyro1_steering
from spyro1_steering import Refusal, Target

ROUTES_DIR = Path(__file__).resolve().parent / "routes"
LEG_ARRIVED = 200    # view-space units (~800 world, measured from Spyro): a waypoint is a way
LEG_BUDGET = 1500    # fields one leg may take before the route refuses
# Causeways are narrower than the 24-field decision the seeker defaults to: Spyro covers ~1000 units
# in one, so a bearing error that a hub forgives walks off a ramp. Routes decide every 8 fields.
ROUTE_STEP = 8
CLIMB_ALWAYS = 1 << 30  # a hop_within no distance exceeds: hop on every decision


class Route:
    """One recorded route: the level it belongs to and its world-space waypoints."""

    def __init__(self, level: int, name: str, waypoints: list[tuple[int, ...]]):
        if not waypoints:
            raise Refusal(f"route {name!r} has no waypoints")
        self.level = level
        self.name = name
        self.waypoints = waypoints

    @classmethod
    def load(cls, path: Path, name: str) -> "Route | None":
        """The named route, or None when the file records no such route (a fact about the
        recording, so the caller can fall back to the seeker). A malformed file refuses."""
        data = json.loads(path.read_text())
        raw = data["routes"].get(name)
        if raw is None:
            return None
        points = []
        for point in raw:
            if len(point) not in (3, 4) or not all(isinstance(v, int) for v in point):
                raise Refusal(f"route {name!r} in {path.name} has a malformed waypoint {point!r}")
            points.append(tuple(point))
        return cls(int(data["level"]), name, points)

    @classmethod
    def find(cls, level: int, name: str) -> "Route | None":
        """The recorded route `name` for `level`, searching every file under tools/routes."""
        for path in sorted(ROUTES_DIR.glob("*.json")):
            if json.loads(path.read_text())["level"] != level:
                continue
            route = cls.load(path, name)
            if route is not None:
                return route
        return None


def follow(port, route: Route, stop: Callable[[], bool] | None = None, stop_is: str = "") -> None:
    """Walk every waypoint of `route` in order. Refuses by name if the level is not the one the
    route was recorded in, or if a leg never arrives. `stop` ends the route early, on the guest's
    own word, when the thing the route exists to cause (a death, a cutscene) has happened."""
    level = port.words(guest_globals.kLevelId, 1)[0]
    if level != route.level:
        raise Refusal(
            f"route {route.name!r} was recorded in level {route.level} but g_LevelId is {level}"
        )
    for index, point in enumerate(route.waypoints):
        if stop is not None and stop():
            return
        what = f"waypoint {index + 1}/{len(route.waypoints)} of {route.name}"
        print(f"route: {what} at {point}", file=sys.stderr)
        # A fourth element of 1 marks a leg that climbs a wall: it jumps on every decision.
        climbing = len(point) == 4 and point[3] == 1
        repl_walk.Seeker(port, what, [Target(what, tuple(point[:3]))], budget=LEG_BUDGET,
                         arrived=LEG_ARRIVED, step=ROUTE_STEP, from_player=True,
                         hop_within=CLIMB_ALWAYS if climbing else 0, hop=False,
                         detour=False, stop=stop, stop_is=stop_is).walk()
