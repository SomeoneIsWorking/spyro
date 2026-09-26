#!/usr/bin/env python3
"""Spyro 1's attract-demo oracle route: the NO-INPUT route that crosses a level entry.

Why this route exists. The Artisans route cannot leave its level: the d-pad is camera-relative and
the camera's rotation carries the pacing residual, so two cores driven by the same policy steer apart
and enter the next level from two different places (docs/issues/0114). The one boundary that
discards and reloads guest code at a reused load address -- and invalidates every translation that
came from it -- therefore sits outside every comparison.

The attract demo needs no steering at all. From a fresh boot with no pad input, TSM_Init sub-state 3
times out at `m_Tick >= 990`, fades, and hands the title screen to `TSM_Demo` / `TSD_DemoLevel`
(external/spyro-1/src/overlays/titlescreen.c:179-210), which loads `g_DemoLevelIds[g_DemoIndex]`
-- level 11 on a fresh boot, since `g_DemoIndex` is .sbss (update.c:645-655) -- and then plays a
RECORDED input stream out of the level's own data (update.c:684, gamepad.c:165-180). So both cores
run the same 844 ticks of level 11 on their own, and the level entry is compared.

The lens is oracle_spyro1's: the same declared ranges, the same observation, the same input
delivery and the same exclusions. This module adds only what is specific to the demo, and it REPLACES
one thing: the advance unit. `oracle_spyro1.advance` steps a FIELD and returns when the guest's
per-field counter decreases, which is one main-loop iteration; that barrier cannot resolve inside this
route, so this module carries its own (`advance`, below, and docs/issues/0132): one FIELD per step
outside the demo's playback, and inside it one MAIN-LOOP ITERATION waited for on `g_GameTick`, which
is the point at which both cores' iteration boundaries coincide.

WHAT A PASS HERE DOES NOT PROVE. During playback the guest owns the pad: `PadVSync` returns
immediately while `g_DemoMode` is set (gamepad.c:236-238) and `PadDemoUpdate` overwrites `g_Pad`
from the recording (gamepad.c:165-180). The three decisive pad ranges therefore agree for a trivial
reason across the gameplay phase and stop being an input-delivery discriminator there. They remain
one through the title phase, where the pad really is the driver's, and `pad.*` is still compared so
a difference fails rather than hides.
"""

from __future__ import annotations

import oracle_spyro1 as base
from compare import Checkpoint, CoreSession, DeclaredRange, Driver, Settle, u32
from drive import GS_PLAYING, GS_TITLE_SCREEN, TSM_DEMO

name = f"{base.name} — attract demo"

# The demo's own globals, from the authenticated image's symbols: g_DemoMode (include/gamepad.h:41-43,
# DEMO_MODE_NONE/PLAY/RECORD) and the end-of-demo counter (update.c:928-950). m_DemoType is the
# title overlay's +0x1C word (titles/spyro1/core/spyro1_transition_skip.cpp:53).
G_DEMO_MODE = 0x80075714
G_DEMO_FADE_TIMER = 0x80075884
G_TITLE_DEMO_TYPE = base.G_TITLESCREEN + 0x1C

TSS_SETUP, TSS_LOADING = 0, 1   # include/titlescreen.h:58-60
DEMO_TYPE_DEMO_LEVEL = 2        # TSD_DemoLevel: load g_DemoLevelIds[g_DemoIndex]
DEMO_LEVEL_FIRST = 0x0B         # g_DemoLevelIds[0] (asm/data/math.data.s:2751), the fresh-boot demo
DEMO_PLAYBACK_TICKS = 0x35C - 8  # g_DemoLengths[0] - 8 (asm/data/math.data.s:2758), update.c:932
DEMO_FADE_TICKS = 16            # g_DemoFadeTimer counts to 16 and the title screen comes back (update.c:950)

# The same lens plus the two words this route is about. g_DemoMode decides where the pad comes from
# and gates the demo-mode caption the renderer draws (game/render/scene.cpp:128-131), so a difference
# in it is a difference in the simulation. The fade counter is decisive here where the dragon
# cutscene's tick is informational, because it is incremented once per demo update (update.c:945)
# rather than read from g_LevelTicks.
declared = base.declared + (
    DeclaredRange("demo.mode", G_DEMO_MODE, 4, True),
    DeclaredRange("demo.fade_timer", G_DEMO_FADE_TIMER, 4, True),
)

excluded = base.excluded
selftest = base.selftest
picture_decisive = base.picture_decisive
observe = base.observe
lookahead = base.lookahead
summary = base.summary
Observation = base.Observation


# WHY THIS ROUTE HAS ITS OWN ADVANCE AT ALL, AND WHY IT HAS TWO CLAUSES. `oracle_spyro1.advance` is
# the framework's game-frame barrier: step a field, return when the guest's per-field counter
# `g_UnprocessedFrames` DECREASES, i.e. when the guest main loop consumed it. That barrier cannot
# resolve on this route, and the guest source says why on both cores:
#
#   * outside a demo the counter is a plain per-field count -- `PadVSync` increments it once per field
#     (gamepad.c:508) and the main loop zeroes it each iteration (main.c:32) -- so `current < previous`
#     fires when a field step straddles that zeroing, and a phase paced two fields per main-loop
#     iteration resolves every second or third field. That is what makes the barrier usable in gameplay.
#   * during a demo it never moves. `PadDemoUpdate` ASSIGNS it 2 instead of counting ("No lag frames
#     in demo recordings", gamepad.c:214) and the main loop reads and zeroes it in the SAME iteration
#     (main.c:24-32), while `PadVSync` returns before its increment while `g_DemoMode != NONE`
#     (gamepad.c:237-238). The counter is therefore 0 at every field boundary, `current < previous` is
#     never true, and the barrier runs a whole phase as ONE "game frame". The flyby sets
#     `g_DemoMode = DEMO_MODE_PLAY` on its last tick (update.c:780), so this covers the entire 867-tick
#     playback and the teardown behind it, not just a corner of it.
#
# CLAUSE ONE, everywhere else: one FIELD per advance. A field is the one unit both cores resolve,
# because it is already the unit each session's `step` is: `ConsoleSession.step(1)` is one reference
# VBlank, and the product's REPL `run 1` delivers one field (measured: `g_LevelTicks` +1 per `run 1`,
# the measurement `oracle_spyro1.advance` cites, and `g_LevelTicks` is incremented by `PadVSync` ahead
# of the demo early return at gamepad.c:228, so it counts fields during a demo on both cores). What it
# buys is OBSERVABILITY and a COMMON DENOMINATOR: the drive is offered every field, so a state the
# guest holds for hundreds of fields is caught on the first field it holds instead of at whatever
# barrier edge comes next, and both cores' pacing is reported in one unit. What it does NOT buy is the
# two cores standing at the same guest moment at the same field, and it must not be described as if it
# did: censused one advance step at a time (scratch/oracle/flyby_census_probe.py) the two cores run the
# SAME 383 flyby iterations -- the title overlay's `m_Tick` goes 1 to 384 on both -- but the product
# spends 2.00 fields per iteration on them and the reference 1.00, so at a given field the product is
# at half the reference's `m_Tick` through the flyby. That is a real pacing difference, it is in the
# report as a field count, and it is why every comparison point below is a guest STATE rather than a
# field count. (docs/issues/0132's per-phase table gives the flyby as 768 fields on the product and
# 777 on the reference. The product's 768 reproduces exactly here; the reference's does not, and those
# counts are `g_LevelTicks` deltas taken across a window containing the level load's
# `g_LevelTicks = 0` (loaders.c:400), which no delta-based count survives. The reference's own counter
# agrees with this census: 898 guest fields over 900 advance steps.)
#
# CLAUSE TWO, inside the playback: one MAIN-LOOP ITERATION, waited for on `g_GameTick`. A field is the
# right unit for pacing and the WRONG unit for a state comparison, and the measurement says so: with
# clause one alone, both cores execute the same updates in the same order but their iteration
# boundaries sit one field apart, so a sample whose `g_GameTick` reads 701 may be taken at the start
# of update 701 on one core and at its end on the other, and a segment-end comparison then reports
# `player.position` differing by exactly one update's movement. Measured over 48 consecutive fields
# from `demo_playing` (scratch/oracle/tick_alignment_probe.py): the product's position at field N is
# the console's at field N+1 on every one of them, and every position value one core shows appears on
# the other in the same order -- a phase offset, not a state difference, and a coin flip per
# comparison point. `g_GameTick` is incremented by the FIRST statement of the GS_Playing update
# (update.c:1094, before the demo input playback and before the physics), so waiting for it to change
# parks both cores at the same point of the same update: the state after N completed updates. That is
# the alignment the Artisans route gets for free from the framework barrier, and it is the alignment
# issue 0114's 485 per-frame comparisons rest on. `g_GameTick` is also a DECISIVE declared range, so a
# wrong park cannot hide: the comparison checks the alignment it was given.
#
# The clause is narrow on purpose. `g_DemoMode` is DEMO_MODE_NONE until the flyby's last tick
# (update.c:780) and the guest is not in GS_Playing until after the flyby, so outside the playback
# neither half of the condition holds and no per-iteration clock is claimed to exist. The tick
# increments unconditionally once per GS_Playing update, so the wait is bounded by the guest's own
# iteration however long that is, exactly like the framework barrier's; a lag frame simply spans more
# fields inside one wait, which `g_DeltaTime` carries into the report, so `strict` has nothing further
# to refuse here either.
DEMO_MODE_PLAY = 1  # include/gamepad.h:41-43


def _in_demo_playback(core: CoreSession) -> bool:
    return (u32(core, G_DEMO_MODE) == DEMO_MODE_PLAY
            and u32(core, base.G_GAMESTATE) == GS_PLAYING)


def advance(core: CoreSession, frames: int, strict: bool = False) -> None:
    for _ in range(frames):
        if _in_demo_playback(core):
            previous = u32(core, base.G_GAME_TICK)
            while u32(core, base.G_GAME_TICK) == previous:
                core.step(1)
        else:
            core.step(1)


# `--budget` IS A COUNT OF ADVANCE STEPS, so the framework's default of 6000 is 6000 FIELDS on this
# route's drive, not 6000 game frames, and it is too small: the measured reference arrival at this
# route's first checkpoint is field 5928 (scratch/oracle/compare.json, `console_vblanks` on
# `demo_level_load`), and that was read at a barrier edge that can only be LATE, because the barrier
# cannot resolve before the flyby ends. Measured with this route's own field unit the same arrival is
# field 4766. 8000 is 1.7x the larger of the two and the second checkpoint is a fraction of it; the
# gameplay segments are not bounded by it. The route raises a smaller budget to this floor and says
# so, rather than letting a run die with the framework's word for the unit ("game frames") naming a
# different quantity than this route counts on the way to a checkpoint.
FIELD_BUDGET = 8000


def _field_budget(budget: int) -> int:
    if budget >= FIELD_BUDGET:
        return budget
    print(f"[oracle] demo route: --budget {budget} counts advance steps, and one step is one field "
          f"here, so {budget} fields cannot reach this route's first checkpoint (measured reference "
          f"arrival at that checkpoint: field 4766). Using {FIELD_BUDGET}.")
    return FIELD_BUDGET


def no_input(frame: int, seen: Observation) -> frozenset[str]:
    """No policy at all. The demo exists BECAUSE the title times out: one Start or Cross press at
    TSM_Init sub-state 3 cancels the timeout and opens the menu instead (titlescreen.c:176-181), so
    a route that presses anything is not this route."""
    return frozenset()


def _demo_owns_title(seen: Observation) -> bool:
    """The title overlay has handed itself to the demo and the level load is under way or done:
    TSS_Setup writes `g_LevelId = g_DemoLevelIds[g_DemoIndex]`, TSS_Loading blocks until
    `g_LoadStage >= 6` (update.c:629-694)."""
    return (seen.gamestate == GS_TITLE_SCREEN
            and seen.title.mode == TSM_DEMO
            and seen.title.state >= TSS_LOADING)


def _demo_playing(seen: Observation) -> bool:
    """The demo itself: the game update is running in the demo's own level, past the tick that
    zeroes it on every level load (loaders.c:401-402)."""
    return (seen.gamestate == GS_PLAYING
            and seen.level == DEMO_LEVEL_FIRST
            and seen.game_tick >= 1)


# WHY THESE TWO PREDICATES ARE PARKS AND NOT PASS-THROUGHS, which is the rule the advance unit exists
# to satisfy. A predicate may only name a state a core can be PARKED at, because the comparator reads
# the declared ranges exactly where the drive parks: a counter a core merely passes through is invisible
# to a core whose single step spans it, and the drive then runs on to whatever it finds next. Under the
# framework's game-frame barrier that is what `g_GameTick >= 180` was here -- the reference retired the
# whole teardown inside one barrier step (measured 2007 fields at its game frame 1559), so it passed
# tick 180 without ever being observable at a step boundary, and the drive reported `game_tick=927,
# level=55`, two demos later. Both clauses of this route's advance are small enough that neither can
# span a state this route waits for: the field clause is one field, and the playback clause ends at
# `g_GameTick`'s next increment, which is the first statement of the next update. So both predicates
# below are read on the first step at which they hold -- `_demo_owns_title` the first step the title
# overlay is in TSS_Loading, `_demo_playing` the first step the demo's own level is running an update
# -- and the comparator's settle step then adds one more advance of the same kind.
def reach_demo_level_load(driver: Driver, core: CoreSession, budget: int, settle: Settle) -> tuple[int, frozenset[str]]:
    return driver.drive(core, _field_budget(budget), _demo_owns_title, no_input, "attract demo level load", settle)


def reach_demo_playing(driver: Driver, core: CoreSession, budget: int, settle: Settle) -> tuple[int, frozenset[str]]:
    return driver.drive(core, _field_budget(budget), _demo_playing, no_input, "attract demo gameplay", settle)


checkpoints = (
    Checkpoint("demo_level_load", reach_demo_level_load),
    # The level entry itself. `level_id` and `load_stage` are decisive, so arriving with a different
    # level resident or a different load stage fails here rather than three segments later.
    Checkpoint("demo_playing", reach_demo_playing),
)

# The rest of the first demo with nothing held, from the arrival tick. Split so the phase boundaries
# are visible in the report instead of hiding inside one long segment.
#
# THESE ARE MAIN-LOOP ITERATIONS, and they are the same numbers this route always had, because every
# segment here runs inside the demo's playback, where the advance unit is one iteration (see
# `advance`). 700 iterations of a playback the demo ends at `g_DemoLengths[0] - 8` (update.c:932,
# measured 867 game ticks end to end), so both segments stay inside the playback and never reach the
# teardown -- the demo's end, its synchronous title-overlay reload (update.c:950-985) and the second
# demo, which this route does not compare and quotes no field measurement for, because the only one on
# record (issue 0132) comes from the `g_LevelTicks`-delta method that also mis-measured the flyby (see
# `advance`). Every segment holds nothing -- the demo owns the pad (see the module docstring) -- so the
# unit change moves where each comparison lands and not the input. The comparator's progress lines
# still call these "frames"; that label is the framework's.
gameplay = (
    (frozenset(), 400),
    (frozenset(), 300),
)
