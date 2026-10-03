#!/usr/bin/env python3
"""route_scenes.py — the four Spyro 1 scenes the reach corpus had no route for, and what proves each.

WHY THIS EXISTS. docs/issues/0147's corpus ran six routes, and every one of them stops in a
homeworld or an ordinary level: none reaches a flight level, a boss, a death, or the in-game save.
A scene the corpus never enters is a scene whose guest functions are never a candidate for a native
override (0147's own reach census) and never gated by one, so the gap is not a coverage statistic
-- it decides what the project is allowed to own.

Each scene here is a STATE, and each is reached the way a player reaches it: with pad edges taken
from what the guest's own words say. Nothing writes guest memory. There is no level id write, no
position write, and no health write anywhere in this file; the only writer is the product, reacting
to `press`/`tap` and to the pad reading those produce.

Every address below is derived from external/spyro-1 (the decompilation of the byte-identical
SCUS_942.28) and the instruction that computes it is quoted with it, so a reader can re-derive the
number instead of trusting it. `tools/route_scenes.py --selftest` gates the derivations that can be
gated without the product, and each scene REFUSES by name -- with the words it read -- when its
target is not reached, so a run that failed to arrive is never reported as a scene.

THE FOUR TARGETS, and the guest words each asserts on:

  flight-level   Sunny Flight, level id 15. The guest's own g_IsFlightLevel (0x80075690) is the
                 flag every consumer branches on -- draw.c:534 `if (g_IsFlightLevel == 0)` skips
                 the HUD, update.c:177 skips HudTick -- so it, not the level id alone, is what
                 says a flight level is running.
  boss-level     Toasty, level id 14. Same entry route, different portal: the homeworld's portals
                 are a table of destinations and each carries the level it leads to.
  death-respawn  Spyro below the death plane. g_Spyro + 0x8 is m_Position.z; func_8004A200's
                 `slti $v0,$v0,0x400` / `bnez $v0,.L8004A4D8` (0x8004A4F4, 0x8004A4F8) calls
                 func_8002C85C, which sets GS_Respawn or GS_GameOver. The proof is the gamestate
                 the product itself wrote, plus the life count it decremented.
  save-fairy     The in-game save, which is the FAIRY, not the balloonist. MemCardWriteFile has
                 exactly two callers in the whole image (asm grep over external/spyro-1/asm): the
                 title screen and func_800314B4, the GS_Fairy update. The balloonist
                 (GS_Balloonist, D_800777E8) is a ride across the homeworld and writes nothing --
                 include/overlays/balloonist.inc.h has no card call in any of its eight states. The
                 proof is g_FairyCutscene.m_MenuDialoguePage (0x80078D0C) reaching 4, which is the
                 only value from which SaveCreate (0x800321F4) and MemCardWriteFile (0x80032230)
                 are reached; 7 is the page the same block takes when the card refuses.

Usage:
    uv run --frozen python tools/route_scenes.py --selftest
    uv run --frozen python tools/route_scenes.py --list
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field
from typing import Callable

import guest_globals
import repl_route
import repl_walk
import spyro1_steering
from spyro1_steering import Refusal, moby_class_targets

# --- guest addresses, each with the instruction it comes from -------------------------------
#
# The shared ones are the shipping owner's, read through tools/guest_globals.py, so a driver and
# the product cannot drift onto different memory. The ones only these scenes read are named here,
# in the module that is their only reader, with the derivation that makes them checkable.

# g_IsFlightLevel (common.h). loaders.c and update.c branch on it to drop the walk HUD; it is the
# flag that makes a flight level a flight level rather than an ordinary level with a new id.
G_IS_FLIGHT_LEVEL = 0x80075690
# g_SpyroLifeCount (common.h) and g_Gamestate, both from the shipping owner.
G_LIFE_COUNT = 0x8007582C
# g_Spyro + 0x8. g_Spyro is the shipping owner's 0x80078A58; +0x8 is m_Position.z, the third word
# of the Vector3D m_Position that starts the struct.
G_SPYRO_X = 0x80078A58
G_SPYRO_Y = 0x80078A58 + 0x04
G_SPYRO_Z = 0x80078A58 + 0x08
# g_Spyro + 0x80 is m_health: func_8004A4B0 `lw $v0,%lo(g_Spyro + 0x80)($v0)` then
# `slti $v0,$v0,0x65`, and the fairy trigger refuses to open when it is not positive
# (0x8007D7E8 `blez $v0`), so it is read here as corroboration, never as the death condition.
G_SPYRO_HEALTH = 0x80078A58 + 0x80
# g_Spyro + 0x78 is the body state func_8004A200 switches on (0x1D/0x1E/0x1F are the liquid states
# whose +0x80 timer reaches 0x65/0x7D before func_8002C85C runs).
G_SPYRO_STATE = 0x80078A58 + 0x78
# g_FairyCutscene (fairy.h) starts at 0x80078D00 -- asm/data/game.bss.s places it immediately
# after g_Spyro, which it also sizes: "Total size from 80078A58 to 80078D00".
G_FAIRY_STATE = 0x80078D00
# +0x08 is BOTH the menu selection on page 0 and the memory-card machine's stage afterwards. The
# guest proves it: the page-0 handler increments it and wraps at 3 (0x80031E74 / 0x80031E80) as the
# option, and the page-2 handler loads it as $s1's base and stores 1..4 into it (0x800320C8 and
# 0x800320F8 / 0x80032168 / 0x80032240) as the card stage. So it is only read as an option while the
# page is 0, and only read as a stage after the page has left 0; measured, the very same word read
# 4 on a run whose save then succeeded, which is the card machine four steps in and not a selection.
G_FAIRY_OPTION = 0x80078D00 + 0x08   # m_MenuSelectedOption (0=SAVE, 1=RETRY, 2=CONTINUE) on page 0
G_FAIRY_CARD_STAGE = 0x80078D00 + 0x08  # the card machine's stage on pages 1..7, the same word
G_FAIRY_PAGE = 0x80078D00 + 0x0C     # m_MenuDialoguePage, the 0..7 page the update dispatches on
G_FAIRY_HAS_CARD = 0x80078D00 + 0x14  # m_HasMemoryCard; 0 sends the menu to CONTINUE, not SAVE

# Moby classes, from the level-10 overlay's own dispatcher. Both are read off the compare that
# selects them, in asm/nonmatchings/overlays/level_10/func_level_10_8007D9C8.s.
FAIRY_MOBY_CLASS = 110   # 0x8007DB54 `addiu $v0,$zero,0x6E` ; 0x8007DB58 `beq $v1,$v0` -> the
                         # block that ends in `jal InitFairyCutscene` at 0x80080A1C.
BALLOONIST_MOBY_CLASS = 187  # 0x8007DBAC `addiu $v0,$zero,0xBB` ; 0x8007DBB0 `beq $v1,$v0`.

# common.h's Gamestate enum, the two values these scenes prove.
GS_RESPAWN = 4
GS_GAME_OVER = 5
GS_FAIRY = 11
# func_8004A4F4 `slti $v0,$v0,0x400`: below this Z the guest calls func_8002C85C.
DEATH_PLANE_Z = 0x400
# jtbl_80010E08 in func_800314B4 dispatches on m_MenuDialoguePage, one entry per page. Page 2
# (.L800320C0) is the only page that reaches SaveCreate (0x800321F4) and MemCardWriteFile
# (0x80032230); its handler is also the memory-card machine, which walks the card-stage word from 0
# through 4 across a handful of fields. Page 7 (.L8003245C's jtbl entry) is the page that ends the
# cutscene, and it is the SUCCESS page, not a refusal: the guest's own caption for it is the string
# "GAME SAVED" at 0x80010CC4, which the native fairy menu owner lays out for page 7
# (game/render/frame/scene/fairy_menu_recipe.cpp, `case 7`). Pages 3, 4, 5 and 6 are the failures -- "NO
# MEMORY CARD", "NO SAVE FILE", "SAVE ERROR" and "SAVE FAILED" -- and are the ones a refusal names.
SAVE_PAGE_WRITE = 2
SAVE_PAGE_SAVED = 7
SAVE_PAGE_FAILED_FIRST = 3

# Artisans' portals, by the level ids common.h's layout gives: g_LevelId/10-1 is the homeworld and
# g_LevelId%10 the slot, where 0=home 1..3=levels 4=boss 5=flight. 14 is Toasty, 15 Sunny Flight
# (src/strings.c g_LevelNames: "TOASTY" at index 4 and "SUNNY FLIGHT" at index 5).
BOSS_LEVEL_ID = 14
FLIGHT_LEVEL_ID = 15
# g_LevelIndex (common.h), which the guest computes from the level it loaded.
G_LEVEL_INDEX = 0x80075964
# Artisans is homeworld 0, so loaders.c:1399 makes the index the slot: 4 for the boss, 5 for the
# flight level. Stated as the arithmetic rather than as a number so a reader can check it.
BOSS_LEVEL_SLOT = 14 % 10
FLIGHT_LEVEL_SLOT = 15 % 10


@dataclass(frozen=True)
class Proof:
    """What a scene proved, and what it cost. `words` are the guest words read at the moment of
    proof, so a reader can check the claim against the product rather than against this file."""

    scene: str
    target: str
    frames: int
    words: dict[str, int | str] = field(default_factory=dict)
    detail: str = ""

    def as_json(self) -> dict:
        return {
            "scene": self.scene,
            "target": self.target,
            "frames": self.frames,
            "words": {name: (f"0x{value:08X}" if isinstance(value, int) else value)
                     for name, value in self.words.items()},
            "detail": self.detail,
        }


class ScenePort:
    """The slice of the product a scene drives, plus the two readers every scene needs.

    Wrapping rather than inheriting keeps the scenes testable against a fake port -- a scene that
    only ever saw a real product could not have a negative case, and a negative case is the whole
    point of refusing by name.
    """

    def __init__(self, port, fields_at_entry: int):
        self._port = port
        self.entered = fields_at_entry

    @property
    def raw(self):
        """The product object itself, for the walk stepper, which drives a REPL rather than a
        scene. Public rather than a private attribute read from outside: a scene reaching into
        another object's `_port` is a boundary the type system cannot police."""
        return self._port

    @property
    def frame(self) -> int:
        return self._port.frame

    def words(self, address: int, count: int = 1) -> list[int]:
        return self._port.words(address, count)

    def word(self, address: int) -> int:
        return self._port.words(address, 1)[0]

    def press(self, button: str) -> None:
        self._port.press(button)

    def release(self, button: str) -> None:
        self._port.release(button)

    def tap(self, button: str, frames: int = 4) -> None:
        self._port.tap(button, frames)

    def run(self, frames: int) -> None:
        self._port.run(frames)

    def spent(self) -> int:
        return self._port.frame - self.entered

    def signed(self, address: int) -> int:
        value = self.word(address)
        return value - 0x100000000 if value >= 0x80000000 else value


def _walk_until(port: ScenePort, what: str, targets, predicate, description: str,
                budget: int, arrived: int | None = 0, route: str | None = None,
                from_player: bool = False) -> int:
    """Walk to a place and keep going until `predicate` says the thing HAPPENED.

    `arrived=0` is the important part: a proximity radius would report a walk that stopped next to
    a portal for a walk that entered one, which is the failure `Seeker` was given `stop` to avoid.

    `route` names a recorded waypoint route (tools/routes) to follow first, for destinations a
    straight line cannot reach. The level has to be one the route was recorded in; a level with no
    recording for it keeps the plain seek, so the absence of a route is visible rather than faked.

    `from_player` measures distances from Spyro, not from the trailing camera: a proximity trigger
    (the fairy) is a distance from Spyro, and the camera sits about 3000 units behind him.
    """
    if route is not None:
        recorded = repl_route.Route.find(port.word(guest_globals.kLevelId), route)
        if recorded is not None:
            repl_route.follow(port.raw, recorded, stop=predicate, stop_is=description)
    return repl_walk.Seeker(port.raw, what, targets, budget=budget, arrived=arrived,
                            stop=predicate, stop_is=description, from_player=from_player).walk()


def flight_level(port: ScenePort) -> Proof:
    """Walk into Sunny Flight's portal and prove g_IsFlightLevel is set.

    The portals are a table, not a guess: g_Portals (the shipping owner) holds up to six pointers
    and each carries the level it leads to, so the route asks the level for its flight portal
    rather than assuming one exists. A homeworld that does not have one is REFUSED with the level
    ids it does have, which is a fact about the level and not a driver bug.

    MEASURED 2026-10-01, still failing, and this is where: no route for `portal-15` is recorded in
    tools/routes, so `Route.find` answers None and the scene falls back to the straight-line seeker,
    which stalls 5570 view units from the portal to level 15 at (52656, 43245, 8865). The same
    portal is where death-respawn drowns, so the direct line crosses the west pond. Reaching 3000
    view units of that portal needs a route that goes around it, and the level-10 walkable graph
    (scratch-only A* over the render mesh) reaches no closer than 6000 world units at any climb
    bound tried. docs/issues/0170 carries the numbers and what would unblock it.
    """
    portals = spyro1_steering.portal_targets(port.words, level=FLIGHT_LEVEL_ID)
    if not portals:
        available = [target.what for target in spyro1_steering.portal_targets(port.words)]
        raise Refusal(
            f"this homeworld has no portal to level {FLIGHT_LEVEL_ID} (Sunny Flight); it offers "
            f"{', '.join(available) or 'no portals at all'}, so the flight route needs a homeworld "
            "whose flight portal is unlocked, not a different input plan"
        )
    _walk_until(port, f"portal to level {FLIGHT_LEVEL_ID}", portals,
                lambda: port.word(guest_globals.kLevelId) == FLIGHT_LEVEL_ID,
                f"left the homeworld for level {FLIGHT_LEVEL_ID}", budget=9000,
                route=f"portal-{FLIGHT_LEVEL_ID}")
    if not _await(port, lambda: port.word(G_IS_FLIGHT_LEVEL) != 0, "g_IsFlightLevel to be set",
                  budget=1800):
        raise Refusal(
            f"entered level {FLIGHT_LEVEL_ID} but g_IsFlightLevel (0x{G_IS_FLIGHT_LEVEL:08X}) is "
            f"still {port.word(G_IS_FLIGHT_LEVEL)} after {port.spent()} field(s): the level loaded "
            "and the flight flag did not, which is a level-loading defect rather than a route one"
        )
    index = port.word(G_LEVEL_INDEX)
    if index != FLIGHT_LEVEL_SLOT:
        raise Refusal(
            f"g_LevelId is {FLIGHT_LEVEL_ID} but g_LevelIndex is {index}, and the guest's own "
            f"derivation of it for Artisans is {FLIGHT_LEVEL_SLOT} (loaders.c:1398), so the level "
            "index was not computed from the level that loaded"
        )
    # Stop in PLAY, not at the flag. The flag is set while the level streams in, and the functions
    # a flight level owns -- its overlay's update, its plane and its objectives -- run after the
    # hand-off, so a route that stopped at the flag would report a flight level the corpus has
    # measured almost nothing of.
    if not _await(port, lambda: port.word(guest_globals.kGamestate) == 0,
                  "GS_Playing in the flight level", budget=2400):
        raise Refusal(
            f"g_IsFlightLevel is set but the guest never returned to GS_Playing (gamestate "
            f"{port.word(guest_globals.kGamestate)}) after {port.spent()} field(s), so the level "
            "loaded and the hand-off to the player did not"
        )
    return Proof(
        "flight-level",
        f"g_IsFlightLevel=0x{port.word(G_IS_FLIGHT_LEVEL):08X} with "
        f"g_LevelId={port.word(guest_globals.kLevelId)} (Sunny Flight), the guest's own "
        f"g_LevelIndex={index}, at GS_Playing",
        port.spent(),
        {
            "g_IsFlightLevel": port.word(G_IS_FLIGHT_LEVEL),
            "g_LevelId": port.word(guest_globals.kLevelId),
            "g_LevelIndex": index,
            "g_Gamestate": port.word(guest_globals.kGamestate),
        },
    )


def boss_level(port: ScenePort) -> Proof:
    """Walk into the boss portal and prove the guest loaded that level.

    Toasty is a level, not a moby: `g_LevelId == 14` is what loaders.c keys the Toasty dialogue
    on ("for artisans, check if the Toasty conditions have been met", g_LevelId == 10 is the home
    world and props->m_CutsceneId == 1 is the boss). So the proof is the level id the product
    itself loaded, plus the boss arena's own overlay being the resident code image.

    MEASURED 2026-10-01, still failing, and this is where: the recorded `portal-14` route reaches
    waypoint 66 of 74 and then stalls 258 view units short of waypoint 67, which is recorded at
    (129225, 81975, z 7042) while the guest's own position word has Spyro at (128933, 80464, z 5090)
    -- a terrace 1952 units above the ground he is standing on. The route crosses a ledge; the
    walker is refusing to walk into a wall. Regenerating the route from the same mesh with a
    clearance margin does reach every one of its own waypoints, so the last 2900 units are the
    whole gap. docs/issues/0170 carries the numbers.
    """
    portals = spyro1_steering.portal_targets(port.words, level=BOSS_LEVEL_ID)
    if not portals:
        available = [target.what for target in spyro1_steering.portal_targets(port.words)]
        raise Refusal(
            f"this homeworld has no portal to level {BOSS_LEVEL_ID} (Toasty); it offers "
            f"{', '.join(available) or 'no portals at all'}, so the boss route needs a homeworld "
            "whose boss portal is unlocked, not a different input plan"
        )
    _walk_until(port, f"portal to level {BOSS_LEVEL_ID}", portals,
                lambda: port.word(guest_globals.kLevelId) == BOSS_LEVEL_ID,
                f"left the homeworld for level {BOSS_LEVEL_ID}", budget=9000,
                route=f"portal-{BOSS_LEVEL_ID}")
    if not _await(port, lambda: port.word(guest_globals.kGamestate) == 0,
                  "GS_Playing in the boss level", budget=1800):
        raise Refusal(
            f"entered level {BOSS_LEVEL_ID} but the guest never returned to GS_Playing "
            f"(gamestate {port.word(guest_globals.kGamestate)}) after {port.spent()} field(s)"
        )
    # g_LevelIndex is the guest's OWN arithmetic on the level it just loaded, loaders.c:1398
    # `g_Homeworld = (g_LevelId / 10) - 1` then `g_LevelIndex = (g_Homeworld * 6) + (g_LevelId % 10)`.
    # Checking it is checking that the guest did that arithmetic and not that a word happens to
    # read 14 -- two different claims, and only the first is about loading a level.
    index = port.word(G_LEVEL_INDEX)
    if index != BOSS_LEVEL_SLOT:
        raise Refusal(
            f"g_LevelId is {BOSS_LEVEL_ID} but g_LevelIndex is {index}, and the guest's own "
            f"derivation of it for Artisans is {BOSS_LEVEL_SLOT}: the level id was reached without "
            "the level index being computed from it, so nothing here proves a level loaded"
        )
    return Proof(
        "boss-level",
        f"g_LevelId={port.word(guest_globals.kLevelId)} (Toasty) with the guest's own "
        f"g_LevelIndex={index}, at GS_Playing",
        port.spent(),
        {
            "g_LevelId": port.word(guest_globals.kLevelId),
            "g_LevelIndex": index,
            "g_Gamestate": port.word(guest_globals.kGamestate),
            "g_IsFlightLevel": port.word(G_IS_FLIGHT_LEVEL),
        },
    )


# WHERE THE DEATH IS. Level 10's collision data has 279 triangles of special-surface type 0 at
# z=5888 over x 56000..68000, y 36000..52000 (decoded from the terrain collision at g_Environment
# + 0x2C: 12-byte triangles, flags byte per triangle, surface table type 0 = the damaging floor
# func_8004DF.. applies as m_DamageFlags 0x400). That is the west pond, and walking into it kills
# Spyro through the guest's own drowning. The previous route swept the eight compass sectors off the
# spawn hill for 900 fields each and never reached it (the corpus ledger: "the guest never died",
# 7,200 fields): a fixed bearing from the hub walks into a rim long before it walks into water. The
# recorded "pond" route (tools/routes) crosses the plain to the pond's east shore and wades in; if
# a level carries no such recording the walk falls back to the westernmost portal, whose approach
# in this homeworld also crosses water. The walk ends on the guest's own decision, GS_Respawn or
# GS_GameOver, wherever on the way it comes.
DEATH_BUDGET = 12000


def death_respawn(port: ScenePort) -> Proof:
    """Walk toward the westernmost portal until the guest kills Spyro, then let the respawn finish.

    The death is the guest's own: func_8004A4F4 `slti $v0,$v0,0x400` on m_Position.z branches to
    0x8004A4D8 `jal func_8002C85C`, which decrements g_SpyroLifeCount and sets GS_Respawn (or
    GS_GameOver when there is no life left). This route supplies no damage and no death: it only
    keeps walking, and stops when the gamestate says the guest decided Spyro died.
    """
    lives_before = port.signed(G_LIFE_COUNT)
    portals = spyro1_steering.portal_targets(port.words)
    if not portals:
        raise Refusal("this level carries no portal table, so the death route has no water to cross")
    target = min(portals, key=lambda portal: portal.position[0])
    lowest = port.signed(G_SPYRO_Z)
    died = lambda: port.word(guest_globals.kGamestate) in (GS_RESPAWN, GS_GAME_OVER)
    try:
        _walk_until(port, target.what, [target], died, "the guest killed Spyro",
                    budget=DEATH_BUDGET, route="pond")
    except Refusal as refusal:
        raise Refusal(
            f"walking at {target.what} {target.position} the guest never killed Spyro: "
            f"g_Gamestate stayed {port.word(guest_globals.kGamestate)} (death plane Z "
            f"{DEATH_PLANE_Z}; refusal: {refusal})"
        ) from refusal
    state = port.word(guest_globals.kGamestate)
    if not died():
        raise Refusal(
            f"the walk at {target.what} ended with g_Gamestate {state}, which is not the guest's "
            f"GS_Respawn ({GS_RESPAWN}) or GS_GameOver ({GS_GAME_OVER}): arriving is not dying"
        )
    # Read health HERE, on the field the death was seen. The respawn restores it, so reading it
    # after the wait would report a full bar for a route whose whole point is that the guest
    # decided Spyro was finished.
    health = port.word(G_SPYRO_HEALTH)
    body_state = port.word(G_SPYRO_STATE)
    lowest = min(lowest, port.signed(G_SPYRO_Z))
    return _death_proof(port, target.what, port.spent(), lives_before, lowest, state, health,
                         body_state)


def _death_proof(port: ScenePort, sector: str, fields: int, lives_before: int,
                 lowest: int, state: int, health: int, body_state: int) -> Proof:
    """A death is only proved once the guest has put Spyro back in the world, so this waits for
    GS_Playing and reports the life count the guest decremented."""
    if not _await(port, lambda: port.word(guest_globals.kGamestate) == 0,
                  "GS_Playing after the respawn", budget=1800):
        raise Refusal(
            f"the guest entered gamestate {state} (the death) but never returned to GS_Playing "
            f"after {port.spent()} field(s); it is still {port.word(guest_globals.kGamestate)}, "
            "so the respawn never completed"
        )
    return Proof(
        "death-respawn",
        f"g_Gamestate went to {state} ("
        f"{'GS_Respawn' if state == GS_RESPAWN else 'GS_GameOver'}) walking {sector}, and Spyro is "
        f"back at GS_Playing with {port.signed(G_LIFE_COUNT)} life/lives from {lives_before}",
        port.spent(),
        {
            "death_gamestate": state,
            "death_health": health,
            "death_body_state": body_state,
            "death_z": lowest,
            "lives_before": lives_before,
            "lives_after": port.signed(G_LIFE_COUNT),
            "g_LevelId": port.word(guest_globals.kLevelId),
        },
        detail=(f"the guest killed Spyro walking {sector} in body state {body_state:#x}; lowest Z {lowest} "
                f"(death plane {DEATH_PLANE_Z}, liquid timers in func_8004A200)"),
    )


# The fairy's menu, read off func_800314B4 and driven as a closed loop over the guest's own words.
# On page 0 the handler takes the guest pad word's 0x4000 bit to move m_MenuSelectedOption up and
# 0x1000 to move it down (0x80031E40 `andi $v0,$v1,0x4000`, 0x80031E9C `andi $v0,$v1,0x1000`,
# wrapping at 3), and 0x40 (0x80031F14 `andi $v0,$v1,0x40`) on option 0 is what sets
# m_MenuDialoguePage to 2 (0x80031F60 `addiu $v0,$zero,2` -> 0x80031F68). Those are the standard
# PSX pad bits, DOWN 0x4000, UP 0x1000 and CROSS 0x40; the port REPL's button names (repl.cpp
# repl_btn) carry the byte-swapped masks, so the REPL's "down" is the guest's 0x4000. MEASURED, not
# read off the table: "down" on option 0 left the option at 1, and the earlier names (SQUARE moves
# it, DOWN confirms) were wrong. The handler ignores input until its own timer reaches 8
# (0x80031EF4 `slti $v0,$v0,0x8`), which is why the loop waits on the guest and reads the option
# back instead of pressing on a schedule.
FAIRY_MENU_OPTION = 0
FAIRY_MENU_FIELDS_PER_STEP = 60
FAIRY_CARD_FIELDS = 1800
FAIRY_MENU_STEPS = 24
# The card machine's own steps, watched this finely because it finishes inside one menu step
# (measured: page 0 -> 2 -> 7 across 26 fields, with the card stage climbing 0..4 every two or three
# fields), so a sample coarser than this walks over page 2 and cannot prove the write was entered.
FAIRY_WATCH_FIELDS = 2


# The fairy's trigger radius is 0x400 world units and a view unit is a quarter of one, so 150 view
# units (600 world) is well inside it. The stand is the guest's own idle wait, in fields; the fairy
# wanders, so the approach is repeated against her current position.
FAIRY_STAND_VIEW_DISTANCE = 150
FAIRY_STAND_FIELDS = 45
FAIRY_APPROACH_ATTEMPTS = 30
FAIRY_APPROACH_FIELDS = 900


def _approach_fairy(port: ScenePort, opened, fairies) -> None:
    """Get Spyro standing still inside the fairy's trigger, which is a moving target.

    The fairy hovers and wanders (measured: 84992,52224,10087 then 85204,52498,9831 then
    84799,51964,10041 over the same level visit), and the trigger needs OctDistance < 0x400 AND
    |dz| < 0x200 against a Spyro in body state 0 or 0xD. So a walk to where she WAS parks Spyro out
    of range. This follows her CURRENT position, stands still for a short guest wait, and repeats;
    the guest's own gamestate is the only success. The recorded route gets Spyro to her first.
    """
    route = f"fairy-{FAIRY_MOBY_CLASS}"
    for attempt in range(FAIRY_APPROACH_ATTEMPTS):
        targets = moby_class_targets(port.words, FAIRY_MOBY_CLASS) or fairies
        try:
            _walk_until(port, f"class {FAIRY_MOBY_CLASS} (the fairy)", targets, opened,
                        "the fairy cutscene opened", budget=FAIRY_APPROACH_FIELDS,
                        arrived=FAIRY_STAND_VIEW_DISTANCE, route=route if attempt == 0 else None,
                        from_player=True)
        except Refusal:
            if opened():
                return
            raise
        if opened() or _await(port, opened, "the fairy cutscene to open once Spyro stands still",
                              budget=FAIRY_STAND_FIELDS):
            return
        print(f"fairy: attempt {attempt + 1} stood still without the trigger firing", file=sys.stderr)
    fairy = moby_class_targets(port.words, FAIRY_MOBY_CLASS)[0].position
    raise Refusal(
        f"{FAIRY_APPROACH_ATTEMPTS} approach(es) to within {FAIRY_STAND_VIEW_DISTANCE} view units of "
        f"the fairy, each followed by {FAIRY_STAND_FIELDS} field(s) standing still, and g_Gamestate "
        f"stayed {port.word(guest_globals.kGamestate)}: the trigger (OctDistance < 0x400, |dz| < "
        f"0x200, body state 0 or 0xD, health > 0) was not met; Spyro is at "
        f"({port.signed(G_SPYRO_X)}, {port.signed(G_SPYRO_Y)}, {port.signed(G_SPYRO_Z)}) in body "
        f"state {port.word(G_SPYRO_STATE):#x}; the fairy is now at {fairy}"
    )


def save_fairy(port: ScenePort) -> Proof:
    """Walk to the fairy, answer her save menu, and prove the guest issued the card write.

    The trigger is proximity, not a button: 0x80080788 `slti $v0,$v0,0x400` after OctDistance
    against g_Spyro, and 0x80080A1C `jal InitFairyCutscene`. So the route walks until the gamestate
    changes, which is also why it refuses on a proximity radius alone.
    """
    fairies = moby_class_targets(port.words, FAIRY_MOBY_CLASS)
    if not fairies:
        raise Refusal(
            f"this level carries no Moby of class {FAIRY_MOBY_CLASS}, which is the class the level "
            "overlay's own dispatcher calls InitFairyCutscene for, so there is nobody here to save"
        )
    opened = lambda: port.word(guest_globals.kGamestate) == GS_FAIRY
    _approach_fairy(port, opened, fairies)
    has_card = port.word(G_FAIRY_HAS_CARD)
    page_before, option = _answer_menu(port, has_card)
    return _await_write(port, page_before, option, has_card)


def _answer_menu(port: ScenePort, has_card: int) -> tuple[int, int]:
    """Put the fairy's menu on SAVE with the guest's own controls, and return the page and option
    the guest was on when it took the choice.

    InitFairyCutscene already chooses SAVE when a card is present (init.c:401) and CONTINUE when
    one is not, so on a card the menu is on SAVE before any input. Either way the option is READ
    BACK and the button pressed is the one the handler gives that option, and the loop ends on the
    page leaving 0 -- which is the guest saying it took the choice, not the driver assuming it did.

    Two things the loop must not do, both measured. The option word must be re-checked against the
    PAGE first, because from page 1 on that word is the card machine's stage: pressing DOWN to "fix"
    a selection of 4 while the guest is already saving presses a button the guest's page does not
    read. And the choice must be followed by FAIRY_WATCH_FIELDS-sized steps rather than one long
    run, because the guest ignores CROSS until its own m_AnimationTimer reaches 8 (0x80031EF4) and
    the whole card machine then finishes inside one 60-field step -- measured: page 0 -> 2 -> 7 in
    26 fields, the stage word climbing 0..4 every two or three fields. A route that ran a coarse
    step after pressing CROSS walked straight over page 2 and could not prove the write at all.
    """
    option = port.signed(G_FAIRY_OPTION)
    for step in range(FAIRY_MENU_STEPS):
        page = port.word(G_FAIRY_PAGE)
        if page != 0:
            return page, option
        option = port.signed(G_FAIRY_OPTION)
        if option != FAIRY_MENU_OPTION:
            print(f"save: menu option is {option}, moving it to {FAIRY_MENU_OPTION}", file=sys.stderr)
            port.tap("down", 8)
        else:
            print(f"save: pressing CROSS on option {option} (step {step})", file=sys.stderr)
            port.tap("cross", 8)
        # Watch at the card machine's own resolution until the page leaves the menu.
        for _ in range(FAIRY_MENU_FIELDS_PER_STEP // FAIRY_WATCH_FIELDS):
            port.run(FAIRY_WATCH_FIELDS)
            if port.word(G_FAIRY_PAGE) != 0:
                return port.word(G_FAIRY_PAGE), option
    page = port.word(G_FAIRY_PAGE)
    if page == 0:
        raise Refusal(
            f"the fairy's menu stayed on page 0 with m_MenuSelectedOption {option} after "
            f"{FAIRY_MENU_STEPS} step(s) of {FAIRY_MENU_FIELDS_PER_STEP} field(s): DOWN moves the "
            f"option towards {FAIRY_MENU_OPTION} and CROSS takes it, and the guest took neither"
        )
    return page, option


def _await_write(port: ScenePort, page_before: int, option: int, has_card: int) -> Proof:
    """The proof, or a refusal that says which page the guest stopped on.

    Page 2 is the only page whose handler reaches SaveCreate (0x800321F4) and MemCardWriteFile
    (0x80032230), and page 7 is the page the guest ends the cutscene on, captioned "GAME SAVED"
    (the string at 0x80010CC4, laid out for page 7 by game/render/frame/scene/fairy_menu_recipe.cpp). So the
    proof is the page SEQUENCE the guest walked -- it must contain 2 and end on 7 -- and not a
    sample of the page alone, because the machine finishes inside one 60-field step.

    Every other page it can land on (3, 4, 5, 6) is a failure the guest words for itself: "NO
    MEMORY CARD", "NO SAVE FILE", "SAVE ERROR", "SAVE FAILED". They are named, never folded into a
    success.
    """
    seen: list[int] = []
    spent = 0
    while spent < FAIRY_CARD_FIELDS:
        page = port.word(G_FAIRY_PAGE)
        if not seen or seen[-1] != page:
            seen.append(page)
            print(f"save: m_MenuDialoguePage {page} at field {port.spent()}", file=sys.stderr)
        if page == SAVE_PAGE_SAVED:
            break
        if page in range(SAVE_PAGE_FAILED_FIRST, SAVE_PAGE_FAILED_FIRST + 4):
            break
        if page == 0:
            break
        port.run(FAIRY_WATCH_FIELDS)
        spent += FAIRY_WATCH_FIELDS
    walked = " -> ".join(dict.fromkeys(
        [str(page_before), str(SAVE_PAGE_WRITE)] + [str(page) for page in seen]))
    if SAVE_PAGE_WRITE not in seen:
        raise Refusal(
            f"the fairy's page machine walked {' -> '.join(str(page) for page in seen)} and never "
            f"entered page {SAVE_PAGE_WRITE}, "
            f"the only page SaveCreate and MemCardWriteFile are reached from (it was on page "
            f"{page_before} when CROSS was pressed, with option {option} and "
            f"m_HasMemoryCard={has_card}). Pages {SAVE_PAGE_FAILED_FIRST}-"
            f"{SAVE_PAGE_FAILED_FIRST + 3} are the guest's own failure pages and page 0 is the "
            "menu, so this is the route not getting a card write and naming where it stopped"
        )
    if port.word(G_FAIRY_PAGE) != SAVE_PAGE_SAVED:
        raise Refusal(
            f"m_MenuDialoguePage walked {walked}: the write page {SAVE_PAGE_WRITE} was entered and "
            f"the guest ended on {port.word(G_FAIRY_PAGE)} after {port.spent()} field(s) instead "
            f"of page {SAVE_PAGE_SAVED} ('GAME SAVED'), so the guest did not report the save"
        )
    return Proof(
        "save-fairy",
        f"GS_Fairy with g_FairyCutscene.m_MenuDialoguePage {walked}, page "
        f"{SAVE_PAGE_WRITE} being the only one SaveCreate (0x800321F4) and MemCardWriteFile "
        f"(0x80032230) are reached from and page {SAVE_PAGE_SAVED} the one the guest captions "
        f"'GAME SAVED', the card machine's stage word reaching {port.word(G_FAIRY_CARD_STAGE)}",
        port.spent(),
        {
            "g_Gamestate": port.word(guest_globals.kGamestate),
            "g_FairyCutscene.m_State": port.word(G_FAIRY_STATE),
            "m_MenuSelectedOption": option,
            "m_HasMemoryCard": has_card,
            "m_MenuDialoguePage": port.word(G_FAIRY_PAGE),
            "pages_walked": walked,
            "card_stage_at_end": port.word(G_FAIRY_CARD_STAGE),
            "g_LevelId": port.word(guest_globals.kLevelId),
        },
    )


def _await(port: ScenePort, predicate: Callable[[], bool], what: str, budget: int) -> bool:
    """Advance in small steps until `predicate` holds or the budget runs out. Never sleeps, never
    guesses: a state that appears for one field is a state a once-per-call reader would miss."""
    spent = 0
    while spent < budget:
        if predicate():
            return True
        port.run(10)
        spent += 10
    return predicate()


@dataclass(frozen=True)
class Scene:
    name: str
    description: str
    run: Callable[[ScenePort], Proof]


SCENES = {
    scene.name: scene
    for scene in (
        Scene("flight-level", "a flight level (Sunny Flight)", flight_level),
        Scene("boss-level", "a boss fight (Toasty)", boss_level),
        Scene("death-respawn", "a player death and respawn", death_respawn),
        Scene("save-fairy", "the in-game save (the fairy's card write)", save_fairy),
    )
}


def run_scene(name: str, port) -> Proof:
    """Run one named scene against a live product and return what it proved.

    The two scenes that need a place to walk to share one refusal, so a level that carries neither
    the fairy nor the flight portal cannot be mistaken for a driver that cannot steer.
    """
    if name not in SCENES:
        raise Refusal(f"unknown scene {name!r}; scenes: {', '.join(sorted(SCENES))}")
    return SCENES[name].run(ScenePort(port, port.frame))


def _selftest() -> int:
    """Both answers for every rule the scenes encode: a reader that must resolve, and each refusal
    that must fire. A scene module whose selftest only proves the happy path is a module that can
    report a scene it never reached."""
    failures = 0

    def check(ok: bool, label: str) -> None:
        nonlocal failures
        if not ok:
            failures += 1
            print(f"  FAIL {label}")

    # The derivations, restated as the arithmetic that must reproduce them from the owner's base.
    check(G_SPYRO_Z == 0x80078A60, "g_Spyro.m_Position.z derivation")
    check(G_SPYRO_HEALTH == 0x80078AD8, "g_Spyro.m_health derivation")
    check(G_FAIRY_PAGE == 0x80078D0C, "g_FairyCutscene.m_MenuDialoguePage derivation")
    check(guest_globals.kGamestate == 0x800757D8, "the shipping owner's g_Gamestate")
    check(guest_globals.kLevelId == 0x8007596C, "the shipping owner's g_LevelId")

    # _await: a predicate that only ever becomes true on a later field must still be seen, and one
    # that never does must report the budget spent rather than True.
    class FakePort:
        def __init__(self, frame: int = 100):
            self.frame = frame
            self.ran: list[int] = []

        def words(self, address: int, count: int = 1):
            return [0] * count

        def press(self, button: str) -> None:
            pass

        def release(self, button: str) -> None:
            pass

        def tap(self, button: str, frames: int = 4) -> None:
            self.ran.append(frames)

        def run(self, frames: int) -> None:
            self.ran.append(frames)
            self.frame += frames

    fake = FakePort()
    port = ScenePort(fake, fake.frame)
    check(_await(port, lambda: fake.ran == [10, 10, 10], "a predicate met on the third step",
                 budget=100), "_await returned False for a predicate that did become true")
    fake = FakePort()
    port = ScenePort(fake, fake.frame)
    check(not _await(port, lambda: False, "a predicate that never holds", budget=40),
          "_await claimed success for a predicate that never held")
    check(sum(fake.ran) == 40, f"_await overran its budget ({fake.ran})")

    # A scene name that does not exist must refuse by name, listing the ones that do.
    try:
        run_scene("no-such-scene", FakePort())
    except Refusal as refusal:
        print(f"  refuses an unknown scene: {refusal}")
    else:
        check(False, "an unknown scene was accepted")

    # Each of the four scenes must refuse when its target is not there, and the refusal must name
    # what it looked at. These are the negative cases: without them a route that never arrived
    # would be indistinguishable from one that did.
    class EmptyPortal:
        def __init__(self):
            self.frame = 0
            self.ran: list[int] = []

        def words(self, address: int, count: int = 1):
            if address == guest_globals.kPortalCount:
                return [2]
            if address == guest_globals.kPortals:
                return [0x80100000]
            # Two portals, neither of them the one the scene asked for.
            if address == guest_globals.kPortals + 4:
                return [0x80101000]
            return [0] * count

        def press(self, button: str) -> None:
            pass

        def release(self, button: str) -> None:
            pass

        def tap(self, button: str, frames: int = 4) -> None:
            pass

        def run(self, frames: int) -> None:
            self.frame += frames

    for scene, address in ((flight_level, G_IS_FLIGHT_LEVEL), (boss_level, G_IS_FLIGHT_LEVEL)):
        try:
            scene(ScenePort(EmptyPortal(), 0))
        except Refusal as refusal:
            print(f"  refuses {SCENES_BY_FUNCTION[scene].name}: {refusal}")
        else:
            check(False, f"{SCENES_BY_FUNCTION[scene].name} ran without a matching portal")

    try:
        save_fairy(ScenePort(EmptyPortal(), 0))
    except Refusal as refusal:
        print(f"  refuses save-fairy: {refusal}")
    else:
        check(False, "save-fairy ran with no Moby of the fairy's class")

    try:
        death_respawn(ScenePort(EmptyPortal(), 0))
    except Refusal as refusal:
        print(f"  refuses death-respawn: {refusal}")
    else:
        check(False, "death_respawn reported a death in a level that never set one")

    print(f"scene routes selftest: {'PASS' if not failures else f'{failures} FAILURE(S)'}")
    return 1 if failures else 0


SCENES_BY_FUNCTION = {scene.run: scene for scene in SCENES.values()}


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--list", action="store_true", dest="list_scenes")
    args = parser.parse_args()
    if args.selftest:
        return _selftest()
    for scene in SCENES.values():
        print(f"{scene.name:16} {scene.description}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
