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
G_SPYRO_Z = 0x80078A58 + 0x08
# g_Spyro + 0x80 is m_health: func_8004A4B0 `lw $v0,%lo(g_Spyro + 0x80)($v0)` then
# `slti $v0,$v0,0x65`, and the fairy trigger refuses to open when it is not positive
# (0x8007D7E8 `blez $v0`), so it is read here as corroboration, never as the death condition.
G_SPYRO_HEALTH = 0x80078A58 + 0x80
# g_FairyCutscene (fairy.h) starts at 0x80078D00 -- asm/data/game.bss.s places it immediately
# after g_Spyro, which it also sizes: "Total size from 80078A58 to 80078D00".
G_FAIRY_STATE = 0x80078D00
G_FAIRY_OPTION = 0x80078D00 + 0x08   # m_MenuSelectedOption (0=SAVE, 1=RETRY, 2=CONTINUE)
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
# jtbl_80010E08 in func_800314B4 dispatches on m_MenuDialoguePage. Page 4 is the only predecessor
# of SaveCreate (0x800321F4) and MemCardWriteFile (0x80032230); page 7 is where the same block
# lands when the card's contents do not match (0x8003231C, reached from the checksum compare at
# 0x80032314). 3 is where the write leaves the page machine, so it is what "the write was issued"
# looks like from guest state.
SAVE_PAGE_WRITE = 4
SAVE_PAGE_AFTER_WRITE = 3
SAVE_PAGE_REFUSED = 7

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
    words: dict[str, int] = field(default_factory=dict)
    detail: str = ""

    def as_json(self) -> dict:
        return {
            "scene": self.scene,
            "target": self.target,
            "frames": self.frames,
            "words": {name: f"0x{value:08X}" for name, value in self.words.items()},
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
                budget: int, arrived: int | None = 0) -> int:
    """Walk to a place and keep going until `predicate` says the thing HAPPENED.

    `arrived=0` is the important part: a proximity radius would report a walk that stopped next to
    a portal for a walk that entered one, which is the failure `Seeker` was given `stop` to avoid.
    """
    return repl_walk.Seeker(port.raw, what, targets, budget=budget, arrived=arrived,
                            stop=predicate, stop_is=description).walk()


def flight_level(port: ScenePort) -> Proof:
    """Walk into Sunny Flight's portal and prove g_IsFlightLevel is set.

    The portals are a table, not a guess: g_Portals (the shipping owner) holds up to six pointers
    and each carries the level it leads to, so the route asks the level for its flight portal
    rather than assuming one exists. A homeworld that does not have one is REFUSED with the level
    ids it does have, which is a fact about the level and not a driver bug.
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
                f"left the homeworld for level {FLIGHT_LEVEL_ID}", budget=9000)
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
                f"left the homeworld for level {BOSS_LEVEL_ID}", budget=9000)
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


# Walking in one fixed direction off a hub is what a player does when they want to see what is over
# the edge, but it stops working the moment a wall or a hedge is in the way, so the route sweeps
# the eight compass sectors in turn instead of trusting one bearing. Each sector gets a slice of
# the budget; the walk stops at the first death, wherever it happens.
FALL_SECTORS = ("up", "up right", "right", "down right", "down", "down left", "left", "up left")
FALL_FIELDS_PER_SECTOR = 900
FALL_SAMPLE = 10


def death_respawn(port: ScenePort) -> Proof:
    """Walk Spyro off the island until the guest kills him, then let the respawn complete.

    The death is the guest's own: func_8004A4F4 `slti $v0,$v0,0x400` on m_Position.z branches to
    0x8004A4D8 `jal func_8002C85C`, which decrements g_SpyroLifeCount and sets GS_Respawn (or
    GS_GameOver when there is no life left). This route supplies no damage and no death: it only
    keeps walking, and stops when the gamestate says the guest decided Spyro died.
    """
    lives_before = port.signed(G_LIFE_COUNT)
    height_before = port.signed(G_SPYRO_Z)
    lowest = height_before
    fields = 0
    for sector in FALL_SECTORS:
        buttons = tuple(sector.split())
        for button in buttons:
            port.press(button)
        spent = 0
        while spent < FALL_FIELDS_PER_SECTOR:
            port.run(FALL_SAMPLE)
            spent += FALL_SAMPLE
            fields += FALL_SAMPLE
            lowest = min(lowest, port.signed(G_SPYRO_Z))
            state = port.word(guest_globals.kGamestate)
            if state in (GS_RESPAWN, GS_GAME_OVER):
                # Read health HERE, on the field the death was seen. The respawn restores it, so
                # reading it after the wait would report a full bar for a route whose whole point
                # is that the guest decided Spyro was finished.
                health = port.word(G_SPYRO_HEALTH)
                for button in buttons:
                    port.release(button)
                return _death_proof(port, sector, fields, lives_before, lowest, state, health)
        for button in buttons:
            port.release(button)
    raise Refusal(
        f"walked {len(FALL_SECTORS)} compass sectors off the hub for {fields} field(s) and the "
        f"guest never died: g_Gamestate stayed {port.word(guest_globals.kGamestate)} and the "
        f"lowest Z reached was {lowest} against a death plane of {DEATH_PLANE_Z}. This homeworld "
        "is bounded, so a death route needs a level with a fall in it, not more walking"
    )


def _death_proof(port: ScenePort, sector: str, fields: int, lives_before: int,
                 lowest: int, state: int, health: int) -> Proof:
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
            "death_z": lowest,
            "lives_before": lives_before,
            "lives_after": port.signed(G_LIFE_COUNT),
            "g_LevelId": port.word(guest_globals.kLevelId),
        },
        detail=f"fell walking {sector}; lowest Z {lowest} against the death plane {DEATH_PLANE_Z}",
    )


# The fairy's menu, read off func_800314B4 and driven as a closed loop over the guest's own words.
# On page 0 the handler takes SQUARE to move m_MenuSelectedOption up and TRIANGLE to move it down
# (0x80031E40 `andi $v0,$v1,0x4000`, 0x80031E9C `andi $v0,$v1,0x1000`, wrapping at 3), and DOWN
# (0x80031F14 `andi $v0,$v1,0x40`) on option 0 is what sets m_MenuDialoguePage to 2 (0x80031F60
# `addiu $v0,$zero,2` -> 0x80031F68). Page 2 is the card work. The button NAMES are the port REPL's
# own (external/psxport/runtime/psx/repl.cpp repl_btn), and the bit values are the PSX digital pad's:
# square 0x8000, triangle 0x1000, down 0x0040 -- so the immediates above name SQUARE and TRIANGLE,
# not the CROSS that a PSX face-bit reading would guess.
# The loop presses what the READ option calls for rather than a fixed script, because the menu only
# accepts input once the cutscene has finished moving the camera, and how long that takes is the
# guest's business, not the driver's.
FAIRY_MENU_OPTION = 0
FAIRY_MENU_FIELDS_PER_STEP = 60
FAIRY_CARD_FIELDS = 1800
FAIRY_MENU_STEPS = 24


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
    _walk_until(port, f"class {FAIRY_MOBY_CLASS} (the fairy)", fairies,
                lambda: port.word(guest_globals.kGamestate) == GS_FAIRY,
                "the fairy cutscene opened", budget=9000)
    has_card = port.word(G_FAIRY_HAS_CARD)
    page_before, option = _answer_menu(port, has_card)
    return _await_write(port, page_before, option, has_card)


def _answer_menu(port: ScenePort, has_card: int) -> tuple[int, int]:
    """Put the fairy's menu on SAVE with the guest's own controls, and return the page and option
    it was on when the card work started.

    InitFairyCutscene already chooses SAVE when a card is present (init.c:401) and CONTINUE when
    one is not, so on a card the menu is on SAVE before any input. Either way the option is READ
    BACK and the button pressed is the one the handler gives that option, and the loop ends on the
    page leaving 0 -- which is the guest saying it took the choice, not the driver assuming it did.
    """
    page, option = port.word(G_FAIRY_PAGE), port.signed(G_FAIRY_OPTION)
    for step in range(FAIRY_MENU_STEPS):
        if page != 0:
            return page, option
        port.run(FAIRY_MENU_FIELDS_PER_STEP)
        option = port.signed(G_FAIRY_OPTION)
        if page != 0:
            break
        if option != FAIRY_MENU_OPTION:
            print(f"save: menu option is {option}, moving it to {FAIRY_MENU_OPTION}", file=sys.stderr)
            port.tap("square", 8)
            port.run(FAIRY_MENU_FIELDS_PER_STEP)
            option = port.signed(G_FAIRY_OPTION)
            if option != FAIRY_MENU_OPTION:
                raise Refusal(
                    f"the fairy's menu option is {option} after moving it towards "
                    f"{FAIRY_MENU_OPTION} with SQUARE; m_MenuSelectedOption (0x{G_FAIRY_OPTION:08X}) "
                    "is the word the guest's own page-0 handler increments, so that is what the "
                    "guest believes and the route will not press DOWN on a menu it did not set"
                )
            continue
        print(f"save: pressing DOWN on option {option} (step {step})", file=sys.stderr)
        port.tap("down", 8)
        port.run(FAIRY_MENU_FIELDS_PER_STEP)
        page = port.word(G_FAIRY_PAGE)
    return page, option


def _await_write(port: ScenePort, page_before: int, option: int, has_card: int) -> Proof:
    """The proof, or a refusal that says which page the guest stopped on.

    Page 4 is the only value from which SaveCreate (0x800321F4) and MemCardWriteFile (0x80032230)
    are reached, and the block that reaches them leaves the page machine on 3. So "reached 4" and
    "left 4" are the two halves of the claim, and a route that only got the first would be claiming
    a card write that may never have completed.
    """
    if not _await(port, lambda: port.word(G_FAIRY_PAGE) == SAVE_PAGE_WRITE,
                  f"m_MenuDialoguePage to reach {SAVE_PAGE_WRITE}", budget=FAIRY_CARD_FIELDS):
        stopped = port.word(G_FAIRY_PAGE)
        raise Refusal(
            f"the fairy menu is on page {stopped} (it was {page_before} when DOWN was pressed, with "
            f"option {option} and m_HasMemoryCard={has_card}) and never reached page "
            f"{SAVE_PAGE_WRITE}, the only page SaveCreate and MemCardWriteFile are reached from. "
            f"Page {SAVE_PAGE_REFUSED} is the card refusing and page 1 is the no-save-file page; "
            "the route did not get a card write and it names the page the guest stopped on instead "
            "of calling itself a success"
        )
    if not _await(port, lambda: port.word(G_FAIRY_PAGE) != SAVE_PAGE_WRITE,
                  f"the write to leave page {SAVE_PAGE_WRITE}", budget=600):
        raise Refusal(
            f"m_MenuDialoguePage is still {SAVE_PAGE_WRITE} after {port.spent()} field(s): the "
            "write was entered and never completed"
        )
    return Proof(
        "save-fairy",
        f"GS_Fairy with g_FairyCutscene.m_MenuDialoguePage {page_before} -> {SAVE_PAGE_WRITE} -> "
        f"{port.word(G_FAIRY_PAGE)}, the page SaveCreate and MemCardWriteFile are reached from",
        port.spent(),
        {
            "g_Gamestate": port.word(guest_globals.kGamestate),
            "g_FairyCutscene.m_State": port.word(G_FAIRY_STATE),
            "m_MenuSelectedOption": option,
            "m_HasMemoryCard": has_card,
            "m_MenuDialoguePage": port.word(G_FAIRY_PAGE),
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
