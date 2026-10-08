#!/usr/bin/env python3
"""The walker and the recorded-route follower, against a fake product (docs/issues/0147).

Each case pins one defect the corpus's flight, boss, death and fairy routes were failing on, and has
the negative that shows the fix is not a blanket pass:

  * a cutscene is not a wall (the dragon at the spawn path froze Spyro and the stall rule abandoned
    a destination the level never blocked), but a state that never ends still refuses;
  * distances are measured from Spyro, not from the camera that trails him thousands of units;
  * a jump holds Cross ALONGSIDE the direction (`tap` replaces the whole pad);
  * a route is walked only in the level it was recorded in, and a malformed one refuses.
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import guest_globals  # noqa: E402
import repl_route  # noqa: E402
import repl_walk  # noqa: E402
import spyro1_steering  # noqa: E402
from spyro1_steering import Refusal, Target  # noqa: E402


def pack_half(low: int, high: int) -> int:
    return (low & 0xFFFF) | ((high & 0xFFFF) << 16)


class FakePort:
    """A product whose Spyro walks straight along the camera's forward axis (+y) at a fixed speed
    while `up` is held, with a camera trailing him by `lag` units. Gamestate comes from a script."""

    FIELD_SPEED = 20  # world units per field

    def __init__(self, gamestates=(0,), lag=3000, level=10, start=(0, 0, 0)):
        self.position = list(start)
        self.lag = lag
        self.level = level
        self.gamestates = list(gamestates)
        self.held: set[str] = set()
        self.pressed_log: list[frozenset] = []
        self.taps: list[str] = []
        self.fields = 0

    @property
    def gamestate(self) -> int:
        return self.gamestates[min(self.fields, len(self.gamestates) - 1)]

    def press(self, button: str) -> None:
        self.held.add(button)

    def release(self, button: str) -> None:
        self.held.discard(button)

    def tap(self, button: str, frames: int = 4) -> None:
        self.taps.append(button)

    def run(self, frames: int) -> int:
        self.pressed_log.append(frozenset(self.held))
        for _ in range(frames):
            if self.gamestate == 0 and "up" in self.held:
                self.position[1] += self.FIELD_SPEED
            self.fields += 1
        return self.fields

    def words(self, address: int, count: int = 1) -> list[int]:
        if address == guest_globals.kGamestate:
            return [self.gamestate]
        if address == guest_globals.kLevelId:
            return [self.level]
        if address == guest_globals.kSpyro:
            return [v & 0xFFFFFFFF for v in self.position[:3]]
        if address == guest_globals.kCamera:
            # The rows consume (dy, dz, dx). These are the rows a real homeworld camera holds when
            # it looks along +y: view x is world x, view depth is minus the camera-to-target y.
            matrix = [0, 0, 0x1000, 0, 0, 0, -0x1000, 0, 0]
            words = [pack_half(matrix[0], matrix[1]), pack_half(matrix[2], matrix[3]),
                     pack_half(matrix[4], matrix[5]), pack_half(matrix[6], matrix[7]),
                     pack_half(matrix[8], 0)]
            words += [0] * 5
            words += [self.position[0] & 0xFFFFFFFF, (self.position[1] - self.lag) & 0xFFFFFFFF,
                      self.position[2] & 0xFFFFFFFF, 0]
            return words[:count]
        return [0] * count


def forward_target(distance: int) -> Target:
    return Target("ahead", (0, distance, 0))


class CutsceneHandling(unittest.TestCase):
    def test_a_cutscene_is_waited_out_and_is_not_a_stall(self) -> None:
        # 400 fields of GS_Dragon, far more than the 15-decision stall rule would tolerate.
        port = FakePort(gamestates=[8] * 400 + [0])
        repl_walk.Seeker(port, "ahead", [forward_target(8000)], budget=4000, arrived=400,
                         from_player=True).walk()
        self.assertGreater(port.taps.count("cross"), 10, "the dragon is left with Cross")
        self.assertGreater(port.position[1], 3000, "and the walk then arrived")

    def test_a_cutscene_that_never_ends_refuses(self) -> None:
        port = FakePort(gamestates=[8])
        with self.assertRaises(Refusal) as refusal:
            repl_walk.Seeker(port, "ahead", [forward_target(8000)], budget=4000,
                             from_player=True).walk()
        self.assertIn("gamestate 8", str(refusal.exception))

    def test_only_the_dragon_is_pressed_through(self) -> None:
        port = FakePort(gamestates=[4] * 100 + [0])
        repl_walk.Seeker(port, "ahead", [forward_target(3000)], budget=4000, arrived=400,
                         from_player=True).walk()
        self.assertEqual(port.taps, [], "a respawn is waited out, never pressed through")

    def test_the_stop_predicate_still_ends_a_walk_inside_a_cutscene(self) -> None:
        port = FakePort(gamestates=[11] * 10)
        repl_walk.Seeker(port, "ahead", [forward_target(8000)], budget=400,
                         stop=lambda: port.gamestate == 11,
                         stop_is="the fairy opened", from_player=True).walk()
        self.assertEqual(port.position[1], 0)
        self.assertEqual(port.fields, 0, "the stop is honoured before any cutscene step")


class Measurement(unittest.TestCase):
    def test_distance_is_from_spyro_not_from_the_trailing_camera(self) -> None:
        port = FakePort(lag=3000)
        camera = spyro1_steering.camera(port.words).bearing((0, 8000, 0))[0]
        player = spyro1_steering.player_view(port.words).bearing((0, 8000, 0))[0]
        self.assertEqual(camera - player, 3000 >> 2, "the camera is 3000 units behind Spyro")

    def test_a_route_leg_arrives_where_spyro_is_not_where_the_camera_is(self) -> None:
        port = FakePort(lag=3000)
        repl_walk.Seeker(port, "ahead", [forward_target(6000)], budget=4000, arrived=100,
                         from_player=True).walk()
        self.assertGreater(port.position[1], 5000)


class Jumping(unittest.TestCase):
    def test_a_jump_is_a_cross_press_and_release_before_the_direction(self) -> None:
        port = FakePort()
        with self.assertRaises(Refusal):  # the budget ends before 6000 units are walked
            repl_walk.Seeker(port, "ahead", [forward_target(6000)], budget=200, arrived=100,
                             hop_within=1 << 30, from_player=True).walk()
        self.assertEqual(port.taps, [], "tap replaces the pad, so it would drop the direction")
        self.assertEqual(port.pressed_log[0], frozenset({"cross"}))
        self.assertEqual(port.pressed_log[1], frozenset(), "Cross is released before steering")
        steered = [held for held in port.pressed_log if "up" in held]
        self.assertTrue(steered and all("cross" not in held for held in steered))

    def test_a_jump_is_not_restarted_while_airborne(self) -> None:
        port = FakePort()
        with self.assertRaises(Refusal):
            repl_walk.Seeker(port, "ahead", [forward_target(6000)], budget=200, arrived=100,
                             hop_within=1 << 30, from_player=True).walk()
        presses = sum(1 for held in port.pressed_log if held == frozenset({"cross"}))
        self.assertLessEqual(presses, 200 // repl_walk.Seeker.JUMP_AIRTIME + 1)

    def test_no_hop_means_no_cross(self) -> None:
        port = FakePort()
        with self.assertRaises(Refusal):
            repl_walk.Seeker(port, "ahead", [forward_target(6000)], budget=200, arrived=100,
                             from_player=True).walk()
        self.assertTrue(all("cross" not in held for held in port.pressed_log))


class StallRule(unittest.TestCase):
    def test_a_finer_step_keeps_the_same_patience_in_fields(self) -> None:
        coarse = spyro1_steering.Walk("x", [forward_target(1)], step_scale=1.0)
        fine = spyro1_steering.Walk("x", [forward_target(1)], step_scale=1 / 3)
        self.assertEqual(fine.stall_steps, coarse.stall_steps * 3)
        self.assertAlmostEqual(fine.progress * 3, coarse.progress)

    def test_a_route_leg_never_detours(self) -> None:
        walk = spyro1_steering.Walk("x", [forward_target(1)], detour=False)
        self.assertEqual(set(walk.detours), {0})

    def test_a_cutscene_rebases_the_progress_baseline(self) -> None:
        walk = spyro1_steering.Walk("x", [forward_target(1)])
        walk.best, walk.stalled = 10, 7
        walk.rebase()
        self.assertEqual((walk.best, walk.stalled), (None, 0))


class RecordedRoutes(unittest.TestCase):
    def write(self, directory: str, data: dict) -> Path:
        path = Path(directory) / "route.json"
        path.write_text(json.dumps(data))
        return path

    def test_a_route_is_refused_in_a_level_it_was_not_recorded_in(self) -> None:
        route = repl_route.Route(10, "r", [(0, 100, 0)])
        with self.assertRaises(Refusal) as refusal:
            repl_route.follow(FakePort(level=11), route)
        self.assertIn("level 10", str(refusal.exception))

    def test_a_malformed_waypoint_refuses(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = self.write(directory, {"level": 10, "routes": {"r": [[1, 2]]}})
            with self.assertRaises(Refusal):
                repl_route.Route.load(path, "r")

    def test_an_unrecorded_route_is_absent_not_invented(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = self.write(directory, {"level": 10, "routes": {"r": [[1, 2, 3]]}})
            self.assertIsNone(repl_route.Route.load(path, "other"))
            self.assertEqual(repl_route.Route.load(path, "r").waypoints, [(1, 2, 3)])

    def test_the_shipped_level_10_routes_load_and_name_their_level(self) -> None:
        for name in ("portal-14", "fairy-110"):
            route = repl_route.Route.find(10, name)
            self.assertIsNotNone(route, name)
            self.assertTrue(all(len(p) in (3, 4) for p in route.waypoints))
        self.assertIsNone(repl_route.Route.find(11, "portal-14"))

    def test_following_a_route_walks_every_waypoint(self) -> None:
        port = FakePort()
        repl_route.follow(port, repl_route.Route(10, "r", [(0, 1500, 0), (0, 3000, 0)]))
        self.assertGreater(port.position[1], 2000)


if __name__ == "__main__":
    unittest.main()
