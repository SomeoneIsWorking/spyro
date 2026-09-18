#!/usr/bin/env python3
"""Spyro 1's side of the oracle comparison (psxport tools/oracle/compare.py, docs/oracle.md):
checkpoint predicates, declared state, exclusions, the game-frame barrier and the input policy.

Guest addresses come from external/spyro-1's symbols over the byte-identical SCUS_942.28, shared
with tools/drive.py. Every predicate reads main RAM only: the console reference cannot read the
scratchpad.
"""

from __future__ import annotations

from dataclasses import dataclass

import guest_globals

from compare import (
    Checkpoint,
    CoreSession,
    DeclaredRange,
    Driver,
    SelftestSeed,
    Settle,
    step_until_counter_resets,
    u32,
)
from drive import (
    G_CAMERA,
    G_GAMESTATE,
    G_LOAD_STAGE,
    G_TITLESCREEN,
    GS_PLAYING,
    GS_TITLE_SCREEN,
    TSM_INIT,
    TSM_LOADING,
    TSM_MENU,
    TitleState,
)

name = "Spyro the Dragon (SCUS_942.28)"

# The shared globals come from the shipping owner through tools/guest_globals.py; see drive.py.
G_GAME_TICK = guest_globals.kGameTick                    # incremented once per GS_Playing update
G_LEVEL_TICKS = guest_globals.kLevelTicks                # incremented by the VSync callback, per field
G_UNPROCESSED_FRAMES = guest_globals.kUnprocessedFrames  # fields since the main loop consumed them
G_DELTA_TIME = guest_globals.kDeltaTime                  # the lag the last update was told about
G_STATE_SWITCH = guest_globals.kStateSwitch              # the draw is skipped this iteration
G_PAD = guest_globals.kPad                               # m_Down +0, m_Released +4, m_Held +8
G_SPYRO = guest_globals.kSpyro                           # m_Position at +0, m_State at +0x78
G_DRAGON_CUTSCENE = guest_globals.kDragonCutscene        # 0x24 WAD header, then the state machine
D_OCCLUSION_RESULT = 0x80075844   # read only here: the collision query's result for the camera group

declared = (
    DeclaredRange("gamestate", G_GAMESTATE, 4, True),
    DeclaredRange("title.mode_state", G_TITLESCREEN, 8, True),       # m_Mode, m_State
    DeclaredRange("title.ticks", G_TITLESCREEN + 8, 8, False),        # m_Tick, m_SubTick
    DeclaredRange("title.sub_state_option", G_TITLESCREEN + 16, 8, True),  # m_SubState, m_OptionSelected
    DeclaredRange("load_stage", G_LOAD_STAGE, 4, True),
    DeclaredRange("game_tick", G_GAME_TICK, 4, True),
    DeclaredRange("level_ticks", G_LEVEL_TICKS, 4, False),
    DeclaredRange("delta_time", G_DELTA_TIME, 4, False),
    DeclaredRange("state_switch", G_STATE_SWITCH, 4, True),
    DeclaredRange("player.position", G_SPYRO, 12, True),
    DeclaredRange("player.state", G_SPYRO + 0x78, 4, True),
    DeclaredRange("player", G_SPYRO, 0x2A8, False),
    DeclaredRange("camera", G_CAMERA, 0x110, False),
    # m_CutsceneIdx, m_State, m_Stage, m_BlocksRead, m_HasOverflow, m_LoadLength,
    # m_CutsceneTicks, unk_0x40, m_Fade: the rescue cutscene's own streaming state machine.
    DeclaredRange("dragon_cutscene", G_DRAGON_CUTSCENE + 0x24, 0x24, False),
    # The camera's occlusion group, recomputed every frame by the handwritten collision query at
    # 0x8004DF24. It is one word, it is wrong long before the camera visibly moves, and a stale
    # Lightrec delay-slot register made it read the wrong table (shared/lightrec 3fddb23).
    DeclaredRange("occlusion_result", D_OCCLUSION_RESULT, 4, True),
    # The pad words are what each core's game actually read; a mismatch there is an input-delivery
    # defect in the harness, not a product divergence, and must fail before anything downstream.
    DeclaredRange("pad.down", G_PAD, 4, True),
    DeclaredRange("pad.released", G_PAD + 4, 4, True),
    DeclaredRange("pad.held", G_PAD + 8, 4, True),
)

excluded = {
    "g_LevelTicks (informational only)":
        "counted per delivered field by the VSync callback. The product spends the two-field draw "
        "wait on drawn iterations only, as retail's main loop does, but a draw-less iteration's "
        "field count is retail's own VSync interrupt phase, which the host clock cannot reproduce. "
        "The counter therefore keeps a constant offset from each load, measured 1 at level entry "
        "growing to 5 across the Artisans route (docs/issues/0110)",
    "g_DeltaTime, g_Camera and the dragon cutscene's tick (informational only)":
        "the lag count, camera phase and cutscene timers are read from the counter above, so they "
        "carry its offset; they are compared to show the size of the residual, not to fail on it "
        "(docs/issues/0110)",
    "g_TitlescreenState m_Tick/m_SubTick (informational only)":
        "the menu counts frames spent in a state; creating the new save takes the console about 50 "
        "frames of memory-card I/O where the product's HLE card completes it in 6 (measured "
        "2026-09-18: TSM_Loading entered at tick 49 versus tick 6), so every later tick differs by "
        "that duration while mode, state, sub-state and option agree",
    "scratchpad 0x1F800000..0x1F800400": "the console reference exposes main RAM only",
    "moby arrays, particle pools, draw environments and packet pools":
        "allocation/rendering state, not gameplay state (docs/oracle.md retained observations)",
    "VRAM, SPU and CD device state": "separate pixel, audio and hardware questions",
}

# Representative Artisans input after arrival: the level fades in and hands the player control some
# frames into GS_Playing (tools/drive.py --settle), so the first segment holds nothing for that
# span. Then Spyro runs forward, turns, jumps standing, charges, and jumps while running.
gameplay = (
    (frozenset(), 120),
    (frozenset({"up"}), 60),
    (frozenset(), 30),
    (frozenset({"left"}), 45),
    (frozenset(), 30),
    (frozenset({"cross"}), 6),
    (frozenset(), 60),
    (frozenset({"square"}), 6),
    (frozenset(), 60),
    (frozenset({"up", "cross"}), 6),
    (frozenset({"up"}), 30),
    (frozenset(), 30),
)

selftest = SelftestSeed(G_SPYRO, "player.position", 0)



def lookahead(core: CoreSession) -> int:
    """Both cores park with the next VBlank handler pending: the console's VBlank step stops there,
    and the product's REPL parks at the start of field delivery, before the host pad sample and the
    guest handler (titles/spyro1/core/spyro1_field_scheduler.cpp). libpad's handler runs the SIO
    exchange, which is what first sees a hold, so a hold committed at either park reaches the same
    game frame's update: no lookahead."""
    return 0


TAP_PERIOD = 20  # tools/drive.py Navigator: a 4-frame tap every 20-frame observation step
TAP_WIDTH = 4
ANSWERABLE_MENU_PROMPTS = {4, 10, 15}  # play without a save, confirm creating this save, pick a card
PRESS_START_PLATFORM = 3               # TSM_Init sub-state that accepts Start
NEW_OR_LOAD_PROMPT = 4                 # TSM_Loading state offering NEW GAME (option 0) / LOAD GAME
SLOT_PROMPT = 1                        # TSM_Loading state picking the slot the new game is written into


@dataclass(frozen=True)
class Observation:
    gamestate: int
    title: TitleState
    game_tick: int

    @property
    def at_save_picker(self) -> bool:
        return self.gamestate == GS_TITLE_SCREEN and self.title.mode == TSM_LOADING

    @property
    def playing(self) -> bool:
        return self.gamestate == GS_PLAYING


def observe(core: CoreSession) -> Observation:
    raw = core.read(G_TITLESCREEN, 24)
    words = [int.from_bytes(raw[i:i + 4], "little") for i in range(0, 24, 4)]
    return Observation(u32(core, G_GAMESTATE), TitleState(*words), u32(core, G_GAME_TICK))


def summary(core: CoreSession) -> dict:
    seen = observe(core)
    return {"gamestate": seen.gamestate, "title": (seen.title.mode, seen.title.state, seen.title.sub_state),
            "tick": seen.game_tick}


def advance(core: CoreSession, frames: int, strict: bool = False) -> None:
    """Step `frames` Spyro game frames: one guest main-loop iteration each (GamestateUpdate then
    GamestateDraw, which waits for at least two fields). The product's REPL step delivers one
    field, as the console's does (measured: g_LevelTicks +1 per `run 1`), so both cores run one
    iteration when the VSync callback's field count (g_UnprocessedFrames) is consumed by the main
    loop. A lag frame simply spans more fields, which g_DeltaTime records for the report, so
    `strict` has nothing further to refuse here."""
    for _ in range(frames):
        step_until_counter_resets(core, G_UNPROCESSED_FRAMES)


def _tapping(frame: int) -> bool:
    return frame % TAP_PERIOD < TAP_WIDTH


def title_menu_pattern(frame: int, seen: Observation) -> frozenset[str]:
    """Only the non-destructive prompts are answered: the FORMAT prompts (5/6/7) are deliberately
    absent, as in tools/drive.py, so no driver ever erases a card to reach a checkpoint."""
    if not _tapping(frame) or seen.gamestate != GS_TITLE_SCREEN:
        return frozenset()
    if seen.title.mode == TSM_INIT and seen.title.sub_state == PRESS_START_PLATFORM:
        return frozenset({"start"})
    if seen.title.mode == TSM_MENU and seen.title.sub_state in ANSWERABLE_MENU_PROMPTS:
        return frozenset({"cross"})
    return frozenset()


def new_game_pattern(frame: int, seen: Observation) -> frozenset[str]:
    """NEW GAME must be selected explicitly: confirming the picker on its default LOAD GAME over
    empty slots bounces back to the title. The overwrite prompt (state 2) is never answered."""
    if not _tapping(frame) or seen.gamestate != GS_TITLE_SCREEN or seen.title.mode != TSM_LOADING:
        return frozenset()
    if seen.title.state == NEW_OR_LOAD_PROMPT:
        return frozenset({"left"}) if seen.title.option != 0 else frozenset({"cross"})
    if seen.title.state == SLOT_PROMPT:
        return frozenset({"cross"})
    return frozenset()


def reach_save_picker(driver: Driver, core: CoreSession, budget: int, settle: Settle) -> tuple[int, frozenset[str]]:
    return driver.drive(core, budget, lambda seen: seen.at_save_picker, title_menu_pattern, "save picker", settle)


def reach_playing(driver: Driver, core: CoreSession, budget: int, settle: Settle) -> tuple[int, frozenset[str]]:
    return driver.drive(core, budget, lambda seen: seen.playing, new_game_pattern, "GS_Playing", settle)


checkpoints = (
    Checkpoint("save_picker", reach_save_picker),
    Checkpoint("playing", reach_playing),
)
