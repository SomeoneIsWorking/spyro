#!/usr/bin/env python3
"""Localise the demo route's gameplay divergence: the FIRST guest word that differs, by measurement.

`tools/oracle_compare.py --policy demo` reports `player.position` first differing at g_GameTick 556
and never recovering. The comparator's declared ranges answer "may the simulation differ here", not
"which word wrote the difference", and one of its ranges is 680 bytes wide. This probe answers the
second question: it drives both cores with the ROUTE's own predicates and advance, steps them in
lockstep, and diffs a WATCH SET of guest blocks at EVERY iteration, recording for each watch word the
first iteration at which it differs. The answer is then the smallest such tick, read off a table
rather than argued.

It re-implements nothing: the sessions, the driver, the route's advance and the route's predicates are
the shipping ones, and both cores are stepped by the same `route.advance(1)` the comparator uses, so
its samples ARE the comparator's samples. Every printed address is a guest address named by
external/spyro-1 (asm/data/game.bss.s, game.sbss.s, include/*.h) or by game/core/guest_globals.h.

The negative first: the verdict states how many iterations it scanned, how many words it compared, and
how many words were equal on every one of them -- so a clean scan is a measurement and not a broken
instrument. Words that already differ on the FIRST sample are reported as PRE-EXISTING, separately
from the first NEW divergence, because a word that was different before the segment began cannot be
the segment's divergence. Nothing is dropped quietly: the full per-iteration count is printed.

THE FOUR WIDENINGS, each still measurement and each with its own denominator printed:
  * `--ram-diff FIRST:LAST` diffs ALL of main RAM (524,288 words per core) at two ticks and reports
    every word that agreed at FIRST and differs at LAST, so the first divergent word is not limited to
    the blocks this file thought to name.
  * `--trace FIRST:LAST` prints the movement words side by side for both cores every tick, so a
    step's SIZE is visible and not only its end state.
  * `--watch-store LO:HI` arms the framework's own store watch (PSXPORT_CW) and reports every hit
    with the PC that made it. KNOWN LIMIT, measured: it sees HOST-side stores only -- armed on
    g_CollisionPoint it reported 3 stores in a whole 560-iteration run, all from boot -- so it
    attributes a native override's write and cannot attribute a guest-executed one.
  * `--observe PC[,PC...]:address:bytes` with `--observe-at TICK` arms the console's read-only PC
    observer (docs/info/instruments/I005) for exactly one update and drains it, so the reference's
    instruction-level path is visible where the product's is not.

    uv run --frozen python tools/probe_tick_divergence.py --ticks 580
    uv run --frozen python tools/probe_tick_divergence.py --selftest
    uv run --frozen python tools/probe_tick_divergence.py --ticks 556 --ram-diff 555:556
    uv run --frozen python tools/probe_tick_divergence.py --ticks 557 --trace 552:558
    uv run --frozen python tools/probe_tick_divergence.py --ticks 557 --observe-at 556 \\
        --observe 0x8003FE7C,0x8003FE40:0x80078A80:16
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "external" / "psxport" / "tools" / "oracle"))

import compare
import compare_cores
import drive
import guest_globals
import oracle_spyro1_demo as route

OUT_DIR = ROOT / "scratch" / "oracle" / "probe"

# The watch set: (label, address, size). Every address is read from the decomp, not guessed.
#
#  * g_Pad covers the STICKS at +0x14, which the route's declared `pad.down/released/held` do NOT
#    cover. During demo playback every word of g_Pad is written by PadDemoUpdate from the recording
#    (external/spyro-1/src/gamepad.c:165-199), so a difference in the recorded stream shows up here
#    and nowhere else in the declared ranges.
#  * g_Spyro is the whole 0x2A8 the sbss reserves, not just m_Position, so the physics
#    (m_Physics at +0xC8) and the collision results are diffed too.
#  * The globals blocks hold the demo clock, the demo pointer walk and the level identity.
G_PAD = guest_globals.kPad
G_SPYRO = guest_globals.kSpyro
G_CAMERA = guest_globals.kCamera
G_DEMO_DATA_PTR = 0x8007585C  # game.sbss.s:378, "g_DemoDataPtr"
G_PADBUFFER = 0x800786A0       # game.bss.s:227, "g_PadBuffer"
G_COLLISION_NORMAL = 0x80077368  # game.bss.s:87, "g_CollisionNormal", 0x10 to g_Pad
# The collision raycast's register spill. func_8004AE38 (asm/collision.s:13-30) stores s0..s7, gp, sp,
# fp and ra to this global as its FIRST act, so at any park this is the register file the last
# raycast call was entered with -- its `ra` is the caller, and it is guest RAM on BOTH cores.
RAYCAST_SPILL = 0x80077DD8
SPYRO_SIZE = 0x2A8             # game.bss.s:239, "Total size from 80078A58 to 80078D00"
CAMERA_SIZE = 0x110            # game.bss.s:49,  "Total size from 80076DD0 to 80076EE0"
PAD_SIZE = 0xA8                # game.bss.s:91,   "Total size from 80077378 to 80077420"

# Words the ROUTE already declares as harness residual, not simulation. They are printed, but
# separately, and never counted as a product divergence.
#  * g_LevelTicks: counted per delivered field by the VSync callback, declared informational in
#    oracle_spyro1.excluded ("g_LevelTicks (informational only)", issue 0110).
#  * g_UnprocessedFrames: the same residual seen at the park, which is a field boundary with the
#    next VBlank handler pending on both cores (oracle_spyro1.lookahead); it does not reach the
#    simulation during a demo because PadDemoUpdate ASSIGNS it 2 (gamepad.c:214).
ROUTE_RESIDUAL = {
    guest_globals.kLevelTicks: "g_LevelTicks (issue 0110)",
    guest_globals.kUnprocessedFrames: "g_UnprocessedFrames at the field-boundary park",
}

# Spyro field names by offset, from include/spyro.h. Only the fields a position divergence can be
# read out of; the probe prints an offset unannotated rather than guessing.
#
# RESOLVED 2026-09-27, and the correction matters. The table used to place the whole 0x18..0xB8 band
# 0x10 too LOW: it called 0x88 m_floorIdleTime and 0x8C m_airTime, so a difference at g_Spyro+0x88
# was reported under a timer's name. Four independent reads of external/spyro-1 put every one of
# these 0x10 higher, and they agree:
#
#   * include/spyro.h carries hand-measured offset comments AND self-named fields whose NAMES are
#     their offsets: `Vector3D m_previousPosition; // 0x8c`, plus `int unk_0x6C` at 0x6C, `int unk_0x74`
#     at 0x74, `int unk_0x84` at 0x84, `unk_0x17c` at 0x17C, `unk_0x194` at 0x194 and
#     `m_headRotationMatrix // 0x1c8`. Six names that are also addresses, all consistent.
#   * The decompiled assembly uses the offsets: `g_Spyro + 0x88` is the word func_8003FE40 clears
#     (m_touchingMoby) and the value 0x80078AE0 = g_Spyro+0x88 is called m_touchingMoby in
#     tools/probe_store_sites.py; `g_Spyro + 0x9C` is read beside a state test in
#     asm/nonmatchings/pete/func_80041670.s:32 and `g_Spyro + 0x80` beside the idle timer in the same
#     function (m_idleTimer); `g_Spyro + 0xC4` is m_onEdge and `g_Spyro + 0x78` is compared with 0x11
#     (m_State) in asm/nonmatchings/overlays/level_10/func_level_10_8007D9C8.s:6599-6602.
#   * The layout is decided by the size of what precedes it, and the three candidate offsets for
#     m_DamageFlags / m_FloorDistance / m_RotationMatrix DISCRIMINATE. The band table above put them
#     at 0x3C / 0x40 / 0x44; the resolved band puts them at 0x2C / 0x30 / 0x34. `g_Spyro + 0x2C` and
#     `g_Spyro + 0x30` appear in the listing as `lw`/`sw` words and `g_Spyro + 0x34` appears as
#     `lui`+`addiu` into a REGISTER that is then dereferenced -- the shape of a MATRIX POINTER, and the
#     only one of the three candidates for which that is true. 0x38, 0x3C and 0x40 appear nowhere in
#     the whole listing.
#   * This repo's own committed registry already said so: game/core/guest_globals.h:30 "g_Spyro
#     (spyro.h): m_Position at +0, m_State at +0x78."
#
# 0x00..0x0C, 0x100..0x14C (the m_Physics sub-fields) and 0x150..0x2A4 were already right and are
# unchanged; the 0xC8 band of the old table is 0xB8 and is the single reason m_Physics looked right
# at 0xC8 by coincidence.
#
# SECOND PASS, 2026-09-27: 0x18 and 0x24 were still wrong (they were named m_headRotation and
# m_tailRotation), and 0x04, 0x08, 0x10, 0x14 and the whole 0x18..0x2B byte block were unannotated, so
# a difference there printed as `g_Spyro+0x018 ?` and sent the reader after nothing. The band is now
# MEASURED, and the measurement is reproducible:
#
#   * THE ANCHORS ARE SELF-NAMED. include/spyro.h carries hand-measured offset comments --
#     `Vector3D m_previousPosition; // 0x8c`, `m_floorIdleTime // 0x98`, `m_airTime // 0x9c`,
#     `m_headRotationMatrix // 0x1c8` -- and fields literally named unk_0x6C, unk_0x74, unk_0x84,
#     unk_0x15c, unk_0x17c, unk_0x194, unk_0x1b0, unk_0x208. Counting the header's own field order
#     forward from those lands every offset below with no slack.
#   * THE 0x00..0x34 BAND IS DECIDED BY HOW THE LISTING USES EACH OFFSET, not by field sizes.
#     `grep -rho 'g_Spyro + 0xNN' external/spyro-1/asm | sort | uniq -c` gives 0x08 -> 1538 as `lw`/`sw`
#     (a WORD, so m_Position.z), 0x09/0x0A/0x0B -> 0, 0x0C/0x0D/0x0E -> 22/28/1054 as `sb`/`lbu` (a
#     rotation triple), 0x0F/0x10/0x12/0x13 -> 0 and 0x11 -> 2, then 0x18/0x19 -> 20/20,
#     0x1A/0x1B -> 10/10, 0x1C/0x1D -> 0/0, 0x1E/0x1F -> 16/26, 0x20/0x21 -> 12/12, 0x22/0x23 -> 0/0,
#     0x24/0x25 -> 18/16, 0x26 -> 0, 0x27 -> 42, and 0x28/0x29/0x2A/0x2B -> 6/6/6/12 (a contiguous
#     quad = m_colorFilter).
#   * THE TWO 0/0 PAIRS ARE THE TAIL COUNTERS, and include/spyro.h says why they have no references of
#     their own: m_seperateTailAnimation at 0x1E8 "Whether the tail is animated separately from the
#     body, otherwise it copies the body". A layout that put the tail counters anywhere else would give
#     them references; one that put m_bodyAnimation at 0x18 would leave 0x1C/0x1D unreferenced for no
#     reason at all. So m_bodyAnimation is 0x18, m_headAnimation 0x1A, m_tailAnimation 0x1C.
#   * m_DamageFlags / m_FloorDistance / m_RotationMatrix DISCRIMINATE at 0x2C / 0x30 / 0x34.
#     `g_Spyro + 0x2C` and `g_Spyro + 0x30` appear as `lw`/`sw` words (246 and 6 references), while
#     `g_Spyro + 0x34` appears 150 times as `lui`+`addiu` into a REGISTER that is then dereferenced
#     (asm/nonmatchings/overlays/level_11/func_level_11_8007DA78.s:7592) -- the shape of a MATRIX
#     ADDRESS -- and 0x34 + 0x20 lands exactly on m_bodyTransitionType. 0x38, 0x3C, 0x40, 0x44, 0x48,
#     0x4C and 0x50 appear NOWHERE in the whole listing, which is what a 0x20-byte MATRIX followed by
#     no other named field looks like.
#   * 0x88 is m_touchingMoby because it is the word func_8003FE40 clears with `sw $zero, 0x88($at)` at
#     0x8003FE7C (asm/nonmatchings/pete/func_8003FE40.s:20), and 0x80078AE0 = g_Spyro+0x88 is the
#     address tools/probe_store_sites.py names m_touchingMoby. 0x8C is m_previousPosition because the
#     header says `// 0x8c`, and the VALUE evidence agrees: at tick 557 the word at 0x8C holds
#     0002F394 / 0002F39D, which is the m_Position the tick-556 sample recorded, and 0x90/0x94 hold
#     its y and z.
#   * m_Physics is at 0xC8 and runs to 0x14C, which the header's own unk_0x138/0x13c/0x140/0x144
#     confirm (m_CollisionMovement is three ints at 0x12C, then those four, then m_TurnMomentum at
#     0x148 and m_gravity at 0x14C). The 0x150..0x2A4 tail is the header's order and is unchanged,
#     including m_damageSoundChannel at 0x2A0 (2 references).
#   * This repo's own committed registry agrees: game/core/guest_globals.h:30 "g_Spyro (spyro.h):
#     m_Position at +0, m_State at +0x78."
SPYRO_FIELDS = {
    0x00: "m_Position.x", 0x04: "m_Position.y", 0x08: "m_Position.z",
    0x0C: "m_bodyRotation.x (x,y,z at 0x0C/0x0D/0x0E)", 0x10: "m_headRotation",
    0x14: "m_tailRotation",
    0x18: "m_bodyAnimation", 0x19: "m_nextBodyAnimation",
    0x1A: "m_headAnimation", 0x1B: "m_nextHeadAnimation",
    0x1C: "m_tailAnimation", 0x1D: "m_nextTailAnimation",
    0x1E: "m_bodyAnimationFrame", 0x1F: "m_nextBodyAnimationFrame",
    0x20: "m_headAnimationFrame", 0x21: "m_nextHeadAnimationFrame",
    0x22: "m_tailAnimationFrame", 0x23: "m_nextTailAnimationFrame",
    0x24: "m_bodyFrameProgress", 0x25: "m_headFrameProgress",
    0x26: "m_tailFrameProgress", 0x27: "m_sortingDepth",
    0x28: "m_colorFilter (r,g,b,interpolation)", 0x2C: "m_DamageFlags", 0x30: "m_FloorDistance",
    0x34: "m_RotationMatrix (0x20 bytes, ends 0x53)",
    0x54: "m_bodyTransitionType", 0x58: "m_bodyAnimationSpeed", 0x5C: "m_lastAnimationState",
    0x60: "unk_0x60", 0x64: "m_headAnimationSpeed", 0x68: "unk_0x68", 0x6C: "unk_0x6C",
    0x70: "m_tailAnimationSpeed", 0x74: "unk_0x74",
    0x78: "m_State", 0x7C: "m_walkingState", 0x80: "m_idleTimer", 0x84: "unk_0x84",
    0x88: "m_touchingMoby", 0x8C: "m_previousPosition", 0x98: "m_floorIdleTime",
    0x9C: "m_airTime", 0xA0: "m_surfaceBelowSpyro", 0xA4: "m_floorPositonOnSlope",
    0xB0: "m_slopeAngle", 0xB4: "m_againstWall", 0xB8: "m_wallAgainstSpyro", 0xC4: "m_onEdge",
    0xC8: "m_Physics.m_TargetSpeedAngle", 0xD8: "m_Physics.m_SlopeGravityZ",
    0xDC: "m_Physics.unk_0xdc", 0xE8: "m_Physics.unk_0xe8", 0xF4: "m_Physics.m_Acceleration",
    0x100: "m_Physics.m_Velocity", 0x10C: "m_Physics.m_TrueVelocity",
    0x118: "m_Physics.m_SpeedAngle", 0x128: "m_Physics.m_TrueSpeed",
    0x12C: "m_Physics.m_CollisionMovement", 0x148: "m_Physics.m_TurnMomentum",
    0x14C: "m_Physics.m_gravity", 0x150: "m_onSlope", 0x154: "m_isGliding",
    0x158: "m_highestFlightPoint", 0x15C: "unk_0x15c",
    0x160: "m_invulverabilityTimer", 0x164: "m_health", 0x168: "m_drowningOffset",
    0x16C: "m_touchingSurface", 0x170: "m_SurfaceProximityState", 0x174: "m_damagingFloorIndex",
    0x178: "m_damagingFloorFlags", 0x17C: "unk_0x17c", 0x180: "m_KnockbackDirection",
    0x194: "unk_0x194", 0x198: "unk_0x198", 0x1A0: "unk_0x1a0", 0x1A4: "m_HeadLookTarget",
    0x1B0: "unk_0x1b0", 0x1C8: "m_headRotationMatrix", 0x1E8: "m_seperateTailAnimation",
    0x1EC: "m_flameableFrames", 0x1F0: "m_noGamepadUpdateFrames", 0x1F4: "m_ControlFlags",
    0x1F8: "m_portalEndPos", 0x204: "m_portalAngle", 0x208: "unk_0x208", 0x214: "unk_0x214",
    0x218: "m_fallingState", 0x21C: "unk_0x21c", 0x220: "unk_0x220", 0x224: "m_mobyInUseBySpyro",
    0x240: "unk_0x240", 0x244: "unk_0x244", 0x248: "unk_0x248", 0x24C: "m_flyingAbility",
    0x250: "m_doingSupercharge", 0x254: "unk_0x254", 0x258: "m_RotXAccumulator",
    0x25C: "m_RotYAccumulator", 0x260: "unk_0x260", 0x264: "unk_0x264", 0x268: "unk_0x268",
    0x26C: "unk_0x26c", 0x270: "unk_0x270",
    0x274: "m_CollisionTriangleIndex", 0x278: "m_collisionTriangleUnpacked",
    0x29C: "m_floorFlagsPointer", 0x2A0: "m_damageSoundChannel",
}

# The `globals` watch block's own names, from asm/data/game.sbss.s. Without them a difference in
# g_DynMobyCount printed as `globals+0x0A4`, which is a worse name than no name: it looks like a
# padding word. Every entry is the label the byte-identical decomp puts at that address.
GLOBALS_NAMES = {
    0x068: "D_80075668", 0x06C: "g_CutsceneIdx", 0x070: "D_80075670", 0x074: "D_80075674",
    0x078: "D_80075678", 0x07C: "D_8007567C", 0x080: "g_CutsceneLayout", 0x084: "g_TracerCount",
    0x088: "g_PreviousLevelIndex", 0x08C: "D_8007568C", 0x090: "g_IsFlightLevel",
    0x094: "D_80075694", 0x098: "D_80075698", 0x09C: "D_8007569C", 0x0A0: "D_800756A0",
    0x0A4: "g_DynMobyCount", 0x0A8: "g_DynMobyMax", 0x0AC: "g_LevelTransTicks",
    0x0B0: "g_LevelTransHudActive", 0x0B4: "D_800756B4", 0x0B8: "D_800756B8",
    0x0BC: "g_UpdateParticle", 0x0C0: "D_800756C0", 0x0C4: "D_800756C4",
    0x0C8: "g_NGemsSinceLevelEntry", 0x0CC: "g_DeltaTime", 0x0D0: "g_HasLevelTransition",
    0x0D4: "D_800756D4", 0x0D8: "g_ActAvailable", 0x0DC: "D_800756DC", 0x0E0: "g_CDMaxReadTime",
    0x0E4: "D_800756E4", 0x0E8: "g_DynMobySpaceEnd", 0x0F0: "D_800756F0", 0x0F4: "D_800756F4",
    0x0F8: "g_CreditsTimer", 0x0FC: "D_800756FC", 0x100: "D_80075700", 0x104: "g_CreditsStage",
    0x108: "D_80075708", 0x10C: "g_ScreenBorderEnabled", 0x110: "g_HudMobys",
    0x114: "g_DemoMode", 0x118: "g_SurfaceBelowFlags", 0x11C: "g_LevelTransGems",
    0x120: "D_80075720", 0x124: "D_80075724", 0x128: "D_80075728", 0x12C: "g_GameTick",
    0x130: "D_80075730", 0x134: "g_UpdateMoby", 0x138: "g_ParticleAllocPtr",
    0x13C: "g_MobyAllocPtr", 0x140: "g_LevelTransChestDuration", 0x144: "D_80075744",
    0x148: "D_80075748", 0x14C: "D_8007574C", 0x150: "g_DragonTotal", 0x154: "D_80075754",
    0x158: "g_KeyMoby", 0x15C: "D_8007575C", 0x160: "g_UnprocessedFrames", 0x164: "D_80075764",
    0x168: "g_CreditsBuffer", 0x16C: "D_8007576C", 0x174: "D_80075774",
    0x178: "g_MobyCollisionChain", 0x17C: "D_8007577C", 0x180: "D_80075780", 0x184: "D_80075784",
    0x188: "D_80075788", 0x18C: "D_8007578C", 0x190: "D_80075790", 0x194: "D_80075794",
    0x198: "D_80075798", 0x19C: "g_StateSwitch", 0x1A0: "D_800757A0", 0x1A4: "g_ActEnabled",
    0x1A8: "D_800757A8", 0x1AC: "g_CreditsSequence", 0x1B0: "D_800757B0",
    0x1B4: "g_MobyPodCount", 0x1B8: "g_DemoIndex", 0x1BC: "g_CreditsDataPtr",
    0x1C0: "g_FlightResultsUpdate", 0x1C4: "D_800757C4", 0x1C8: "D_800757C8",
    0x1CC: "D_800757CC", 0x1D0: "D_800757D0", 0x1D4: "D_800757D4", 0x1D8: "g_Gamestate",
    0x1DC: "g_CreditsTotalEntries", 0x1E0: "g_ActivePad", 0x1E4: "D_800757E4",
    0x1E8: "g_PreviousLevelId", 0x1EC: "D_800757EC", 0x1F0: "D_800757F0",
    0x1F4: "D_800757F4", 0x1F8: "g_MobyPods", 0x1FC: "D_800757FC", 0x200: "D_80075800",
    0x204: "D_80075804", 0x208: "g_CollisionTriangleIndex", 0x20C: "D_8007580C",
    0x210: "g_EggTotal", 0x214: "g_IsSpyroHidden", 0x218: "D_80075818", 0x21C: "g_HudOT",
    0x220: "g_WorldOT", 0x224: "g_Particles", 0x228: "g_LevelMobys",
    0x22C: "g_SpyroLifeCount", 0x230: "g_KeyFlag", 0x234: "D_80075834", 0x238: "D_80075838",
    0x23C: "D_8007583C", 0x240: "D_80075840", 0x244: "D_80075844", 0x248: "D_80075848",
    0x24C: "D_8007584C", 0x250: "D_80075850", 0x254: "D_80075854", 0x258: "D_80075858",
    0x25C: "g_DemoDataPtr", 0x260: "g_GemTotal", 0x264: "g_LoadStage", 0x268: "g_PadMutex",
    0x270: "D_80075870", 0x274: "D_80075874", 0x278: "g_CreditsDisplayedCount",
    0x27C: "D_8007587C", 0x280: "g_LevelCheatActive", 0x284: "g_DemoFadeTimer",
    0x288: "g_CurDB", 0x28C: "g_CDReadTime", 0x290: "g_DynMobys", 0x294: "D_80075894",
    0x298: "g_Sparx", 0x29C: "D_8007589C", 0x2A0: "D_800758A0",
    0x2A4: "g_SavedFairyKissTimer", 0x2A8: "D_800758A8", 0x2AC: "g_PortalLevelId",
    0x2B0: "D_800758B0", 0x2B4: "g_NextLevelId", 0x2B8: "D_800758B8", 0x2BC: "g_PortalCount",
    0x2C0: "D_800758C0", 0x2C4: "D_800758C4", 0x2C8: "g_LevelTicks", 0x2CC: "g_SpawnMoby",
    0x2D0: "D_800758D0", 0x2D8: "D_800758D8", 0x2DC: "D_800758DC", 0x2E0: "D_800758E0",
    0x2E4: "D_800758E4", 0x2E8: "g_LifeOrbCount", 0x2EC: "cheat_HomeworldSelected",
    0x2F0: "cheat_LevelSelected", 0x2F4: "D_800758F4", 0x2F8: "D_800758F8",
    0x2FC: "D_800758FC",
}

# The two globals this investigation is about, named from game.sbss.s. `g_DynMobyCount` is
# incremented exactly once per MobyAlloc call (asm/42CC4.s:12-18, func_800524C4), so a COUNT
# difference is a difference in the number of CALLS -- which puts the defect in whatever decides to
# call the allocator, upstream of it, and not in the allocator.
G_DYN_MOBY_COUNT = 0x800756A4
G_MOBY_ALLOC_PTR = 0x8007573C
G_RAND_SEED = 0x80075AC0             # asm/psyq.s:8142-8158, `rand`'s and `srand`'s only state word
G_SPAWN_MOBY = 0x800758CC     # Moby *(*)(int class, Moby *parent) — the per-level spawner
G_DYN_MOBYS = 0x80075890      # Moby * — first DYNAMIC moby slot; the pool MobyAlloc hands out
G_DYN_MOBY_SPACE_END = 0x800756E8
# func_800524C4's free-list link byte is Moby+0x48 and its stride is 0x58 (asm/42CC4.s:23-34 and the
# mirror walk in func_80052568 at asm/42CC4.s:110-118), which is also what loaders.c:649 divides by
# (`sizeof(Moby) + 24`). So the pool is walked in 0x58 steps.
#
# The `m_State` byte include/moby.h names at 0x57 is NOT the sentinel: measured, the level-11 update
# tests Moby[0x48] < 0x80 before it touches a moby, and MobyAlloc's free-list markers -1/-2/-3 all
# read >= 0x80 there. 0x48 is the one byte that decides liveness, and the census uses it.
MOBY_STRIDE = 0x58
MOBY_STATE_OFFSET = 0x48
MOBY_STATE_UNALLOCATED = 0xFF

PAD_FIELDS = {
    0x00: "m_Down", 0x04: "m_Released", 0x08: "m_Held", 0x0C: "m_Type",
    0x10: "m_LeftStickMoved", 0x14: "m_Sticks (RightX,RightY,LeftX,LeftY)",
    0x18: "m_NoButtonsDown", 0x1C: "m_NoMovementButtonPressed", 0x40: "m_BufferedInputs[0]",
}

WATCH = (
    ("g_Pad", G_PAD, PAD_SIZE),
    ("g_PadBuffer", G_PADBUFFER, 0x28),
    ("g_Spyro", G_SPYRO, SPYRO_SIZE),
    ("g_Camera", G_CAMERA, CAMERA_SIZE),
    ("g_CollisionNormal", G_COLLISION_NORMAL, 0x10),
    ("g_CollisionPoint", 0x80076B80, 0x10),   # game.bss.s:28, g_CollisionPoint + 3 unnamed words
    ("raycast_spill", RAYCAST_SPILL, 0x30),  # func_8004AE38's entry register dump
    ("rand", 0x80075AC0, 4),                 # Sony's rand() seed, game/core/native_rand.cpp:35
    ("globals", 0x80075600, 0x300),
    ("demo+level", 0x80075828, 0xB0),
    ("g_LevelId", guest_globals.kLevelId, 8),
    ("g_TitlescreenState", guest_globals.kTitlescreenState, 0x20),
)

# The raycast's spill, in the order asm/collision.s stores it: s0..s7, gp, sp, fp, ra.
SPILL_FIELDS = ("s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "gp", "sp", "fp", "ra")

# The trajectory view: the words a movement difference is read out of, printed side by side for both
# cores at every tick of a window. `--trace 550:560`.
#
# The rows below were relabelled with the resolved SPYRO_FIELDS band. Six of them were watching the
# WRONG WORD: `airTime` was parked on 0x8C, which is m_previousPosition.x; `floorIdle` on 0x88, which
# is m_touchingMoby; `floorDist` on 0x40, which is two words into m_RotationMatrix; `surfaceBelow`
# on 0x90 (m_previousPosition.y); `slopeDeg` on 0xA0 (m_surfaceBelowSpyro); and `againstWall` on 0xA4
# (m_floorPositonOnSlope.x). The field the row meant now sits at the resolved address, and the row
# for the word the old address actually held is kept under its own name, so nothing is lost.
TRAJECTORY = (
    ("pos.x", G_SPYRO + 0x00),
    ("pos.y", G_SPYRO + 0x04),
    ("pos.z", G_SPYRO + 0x08),
    ("state", G_SPYRO + 0x78),
    ("walkState", G_SPYRO + 0x7C),
    ("idleTimer", G_SPYRO + 0x80),
    ("touchingMoby", G_SPYRO + 0x88),
    ("prevPos.x", G_SPYRO + 0x8C),
    ("prevPos.y", G_SPYRO + 0x90),
    ("prevPos.z", G_SPYRO + 0x94),
    ("floorIdleTime", G_SPYRO + 0x98),
    ("airTime", G_SPYRO + 0x9C),
    ("surfaceBelow", G_SPYRO + 0xA0),
    ("slopePos.x", G_SPYRO + 0xA4),
    ("slopePos.y", G_SPYRO + 0xA8),
    ("slopePos.z", G_SPYRO + 0xAC),
    ("slopeAngle", G_SPYRO + 0xB0),
    ("againstWall", G_SPYRO + 0xB4),
    ("onEdge", G_SPYRO + 0xC4),
    ("vel.x", G_SPYRO + 0x100),
    ("vel.y", G_SPYRO + 0x104),
    ("vel.z", G_SPYRO + 0x108),
    ("tvel.x", G_SPYRO + 0x10C),
    ("tvel.y", G_SPYRO + 0x110),
    ("tvel.z", G_SPYRO + 0x114),
    ("tspeed", G_SPYRO + 0x128),
    ("acc.x", G_SPYRO + 0xF4),
    ("acc.y", G_SPYRO + 0xF8),
    ("acc.z", G_SPYRO + 0xFC),
    ("grav", G_SPYRO + 0x14C),
    ("onSlope", G_SPYRO + 0x150),
    ("floorDist", G_SPYRO + 0x30),
    ("damageFlags", G_SPYRO + 0x2C),
    ("health", G_SPYRO + 0x164),
    ("mobyCount", G_DYN_MOBY_COUNT),
    ("mobyAllocPtr", G_MOBY_ALLOC_PTR),
    ("triIndex", 0x80075808),
    ("hitPt.x", 0x80076B80),
    ("hitPt.y", 0x80076B84),
    ("hitPt.z", 0x80076B88),
    ("normal.x", G_COLLISION_NORMAL + 0),
    ("normal.y", G_COLLISION_NORMAL + 4),
    ("normal.z", G_COLLISION_NORMAL + 8),
) + tuple((f"ray.{field}", RAYCAST_SPILL + 4 * index) for index, field in enumerate(SPILL_FIELDS))

# Moby field labels, each one MEASURED from the listing rather than taken from include/moby.h. The
# header disagrees with the image: it places m_Class at 0x42 as a `short`, but the level-11 moby
# update reads 0x42 with `lbu` and keeps only bits 0 and 1 of it (asm/nonmatchings/overlays/
# level_11/func_level_11_8007DA78.s:388 and :396, storing them to D_80075794 and D_800757F4), so 0x42
# is a pair of flags. Its m_Substate..m_ScaleOverride tail (0x58..0x66) also does not fit the 0x58
# block the allocator's own free list walks. So only the offsets the listing actually shows a use
# for are named, and the rest stay raw offsets -- a wrong name here would send the next reader after
# a field that does not exist.
MOBY_FIELDS = (
    (0x00, "m_Props"),            # NOT written by MobyAlloc: 42CC4.s:57 stores g_PropsAllocPtr.
                                  # Measured 2026-09-27, a dynamic slot the product never allocated
                                  # still held the console's m_Props, so a constructor that does not
                                  # set it leaves the level-data value behind.
    (0x04, "m_CollisionChainNext"),  # func_800529E4 splices it (asm/42CC4.s:100-104)
    (0x08, "m_CollisionGroup"),   # func_800529CC zeroes it (asm/42CC4.s:3)
    (0x0C, "m_Position.x"),       # passed to OctDistance as a0 (level_11 ...:376)
    (0x10, "m_Position.y"),
    (0x14, "m_Position.z"),
    (0x34, "m_CollisionRegion"),  # func_80052568 indexes g_MobyCollisionChain with it
    (0x36, "m_Class"),           # the level-11 update switches the whole thing on THIS halfword
    (0x3C, "m_Renderer"),         # func_800529E4 reads 4 bytes here
    (0x40, "unk_0x40"),           # one byte, tested > 0 by func_800529E4
    (0x42, "unk_0x42_flags"),     # one byte; only bits 0 and 1 are used
    (0x48, "m_AliveAndSpawned"),  # `< 0x80` means live; also the free-list link and the spawn flag
    (0x57, "unk_0x57"),
)
# Offsets the spawner reads, and the class whose handler is the spawner.
#
# MEASURED, not from include/moby.h: the level-11 moby update reads a SIGNED HALFWORD at Moby+0x36
# (asm/nonmatchings/overlays/level_11/func_level_11_8007DA78.s:388) and switches on it, and the same
# arm compares it against 0x1A5, 0x1AA, 0x1C4, 0x1DD, 0xFB and 0xFA -- which are MOBYCLASS values in
# include/moby.h (0x1AA = 426 = MOBYCLASS_LETTER_A, 0x1FA/0x1FB = 506/507). So Moby+0x36 is
# `m_Class`, and the spawner arm is the one for class 9 = MOBYCLASS_EXIT_VORTEX (moby.h:245).
MOBY_CLASS_OFFSET = 0x36
MOBY_ALIVE_OFFSET = 0x48   # one byte, tested `< 0x80` to mean "still live" (…:382-384); the same byte
                           # is MobyAlloc's free-list link, whose -1/-2/-3 all read >= 0x80
MOBY_ALIVE_LIMIT = 0x80
SPAWNER_CLASS = 9
SPAWNER_CLASS_NAME = "MOBYCLASS_EXIT_VORTEX"
SPAWNER_FLAG_OFFSET = MOBY_ALIVE_OFFSET
CAMERA_POSITION = G_CAMERA + 0x28   # include/camera.h: m_ProjectionMatrix + m_ViewMatrix are two
                                    # 0x14-byte SHORTMATRIXes, so m_Position is the third field
SPAWN_DISTANCE = 0x2400             # 9216  -- below this the text is built
RESET_DISTANCE = 0x2800             # 10240 -- above this the already-spawned flag is cleared


def oct_distance(ax, ay, bx, by):
    """OctDistance (0x80017990, asm/math.s:1219-1246), transcribed so the predicate can be evaluated
    here. It is here to ANSWER the predicate the guest asks, not to replace it: the guest still
    executes the real function and this only predicts its branch.

    dx = |bx - ax|, dy = |by - ay|; the SMALLER axis is scaled by 3/8 and the larger is returned
    whole, then the two are added. The scaling is `3 * v >> 3` as a LOGICAL shift of `v + (v << 1)`,
    which is why `abs` first matters.

    CORRECTED 2026-09-27: this transcription had the two arms SWAPPED. It read
    `dx - dy >= 0 -> (3*dx >> 3) + dy`, i.e. it scaled the LARGER axis, while the listing takes the
    branch to the arm that scales `$a3`:

        81BC  sub   $at, $a2, $a3      ; at = dx - dy
        81C0  bgez  $at, .L800179DC    ; dx >= dy  ->  scale a3 (= dy, the SMALLER)
        81C8    sll  $at, $a2, 1        ; fall through, dx < dy -> scale a2 (= dx, the SMALLER)
        81D0    srl  $a2, $a2, 3
        81D8    add  $v0, $a2, $a3
        81DC    sll  $at, $a3, 1
        81E4    srl  $a3, $a3, 3
        81EC    add  $v0, $a2, $a3

    The error is not cosmetic: it is a mirror of the function, so it agrees with the guest on the
    axes (where min == max in effect only at the diagonal) and disagrees everywhere else, which is
    exactly the shape that makes a `--spawner` verdict look plausible and be wrong. The selftest pins
    both arms against hand-computed listing values.
    """
    dx = abs(bx - ax)
    dy = abs(by - ay)
    if dx - dy >= 0:
        return dx + ((dy * 3) >> 3)
    return ((dx * 3) >> 3) + dy


WATCH_BYTES = sum(size for _, _, size in WATCH)
WATCH_WORDS = WATCH_BYTES // 4


def read_watch(core: compare_cores.CoreSession) -> dict[int, int]:
    """Every watch word, keyed by guest address. One dict per core, compared key by key."""
    out: dict[int, int] = {}
    for _, address, size in WATCH:
        raw = core.read(address, size)
        for offset in range(0, size, 4):
            out[address + offset] = int.from_bytes(raw[offset:offset + 4], "little")
    return out


def differing(native: dict[int, int], console: dict[int, int]) -> list[tuple[int, int, int]]:
    """(address, native, console) for every differing watch word, in address order. Nothing is
    filtered here: the caller classifies, so a residual can never hide a simulation difference."""
    return [(address, native[address], console[address]) for address in sorted(native)
            if native[address] != console[address]]


def name_of(address: int) -> str:
    for label, base, size in WATCH:
        if base <= address < base + size:
            offset = address - base
            if label == "g_Spyro":
                return f"g_Spyro+0x{offset:03X} {SPYRO_FIELDS.get(offset, '?')}"
            if label == "g_Pad":
                return f"g_Pad+0x{offset:02X} {PAD_FIELDS.get(offset, '?')}"
            if label == "raycast_spill":
                index = offset // 4
                field = SPILL_FIELDS[index] if index < len(SPILL_FIELDS) else "?"
                return f"raycast_spill.{field}"
            if label == "rand":
                return f"g_RandSeed 0x{address:08X}"
            if label == "globals" and offset in GLOBALS_NAMES:
                # Named, because `globals+0x0A4` is a worse label than none at all: it reads like a
                # padding word, and g_DynMobyCount is the word this investigation turns on.
                return f"{GLOBALS_NAMES[offset]} globals+0x{offset:03X}"
            return f"{label}+0x{offset:03X}"
    return "?"


def _signed(value: int) -> int:
    return value - (1 << 32) if value & 0x80000000 else value


# Sony's `rand`, transcribed from the listing rather than from the C library: asm/psyq.s:8140-8154
# is `lui v1,0x41C6 / lw v0,[D_80075AC0] / ori v1,v1,0x4E6D / mult v0,v1 / mflo a0 /
# addiu v0,a0,0x3039 / sw v0,[D_80075AC0] / srl v0,v0,16 / jr ra / andi v0,v0,0x7FFF`. D_80075AC0
# is 0x80075AC0 (asm/data/psyq.bss.s:49) and `srand` at 0x8006275C is the only other writer of it.
#
# So the seed word is a pure LCG state, and that turns the seed into a COUNT INSTRUMENT: if a core
# called `rand()` n times between two samples then seed_after = LCG^n(seed_before), and because the
# LCG is a bijection on u32 the smallest such n is unique. Counting calls therefore needs no store
# observer, no PC observer and no product edit -- only two reads of one word, which both cores
# already produce. `mult` is signed but the LOW half of a signed and an unsigned product agree, so
# only the low word matters here.
LCG_MUL = 0x41C64E6D
LCG_ADD = 0x3039


def _lcg_step(seed: int) -> int:
    return (seed * LCG_MUL + LCG_ADD) & 0xFFFFFFFF


def _lcg_steps(seed: int, target: int, limit: int = 1 << 20) -> int | None:
    """How many `rand()` calls turn `seed` into `target`, or None if no count up to `limit` does.

    None is reported, never coerced to a number: it means the word changed by something other than
    `rand()` (a `srand`, a level load zeroing .bss) or by more than `limit` calls, and those are
    different findings from "the two cores called it a different number of times".
    """
    value = seed
    for steps in range(limit + 1):
        if value == target:
            return steps
        value = _lcg_step(value)
    return None


def _report_rand_calls(rows: list[tuple[int, int | None, int, int | None, int]], window_limit: int) -> None:
    """The per-update `rand()` call count on BOTH cores, and the first update where they differ.

    This is the finest behavioural clock the two cores share that does not depend on a word having
    already been watched: it reads ONE word whose every write is accounted for, and it reports the
    count difference directly, so "the simulation diverged" becomes "the two cores executed a
    different number of calls to the same function in the same update", which is a control-flow
    statement and names its own next measurement.

    A count that does not reproduce as a bounded number of LCG steps is reported as UNEXPLAINED
    rather than skipped: the denominator of scanned updates, the agreed updates, and the unexplained
    ones are all printed, so "equal everywhere" is a measurement and not an absence.
    """
    scored = [row for row in rows if row[1] is not None]
    print(f"[probe] RAND CALL COUNTS derived from the LCG chain of {name_of(G_RAND_SEED)} over "
          f"{len(scored)} update(s) between the first and last sample")
    if not scored:
        print("[probe]   REFUSED: only one sample, so no update elapsed and no count exists")
        return
    explained = [row for row in scored if row[1] is not None and row[3] is not None]
    unexplained = [row for row in scored if row[1] is None or row[3] is None]
    mismatched = [row for row in explained if row[1] != row[3]]
    print(f"[probe]   {len(explained)} of {len(scored)} update(s) had a bounded LCG path on BOTH cores; "
          f"{len(scored) - len(explained)} did not; {len(mismatched)} of the explained update(s) had "
          f"the two cores call it a different number of times")
    if unexplained:
        print("[probe]   UNEXPLAINED update(s) (the word moved by something other than a bounded run "
              "of rand()): " + ", ".join(f"tick {row[0]}" for row in unexplained[:window_limit]))
    if not mismatched:
        print("[probe]   every explained update agreed on the call count: the counts part company "
              "nowhere in this window")
        return
    first = mismatched[0]
    print(f"[probe]   FIRST COUNT MISMATCH: the update ENDING at g_GameTick {first[0]} ran "
          f"{first[1]} call(s) on the product and {first[3]} on the console "
          f"(delta {first[1] - first[3]:+d})")
    agreed_before = [row for row in explained if row[1] == row[3] and row[0] < first[0]]
    print(f"[probe]   {len(agreed_before)} update(s) before it agreed exactly; the last was "
          f"g_GameTick {agreed_before[-1][0] if agreed_before else 'none'}")
    for row in mismatched[:window_limit]:
        mark = "**" if row is first else "  "
        print(f"[probe]   {mark} update ending at tick {row[0]:>4}: native {row[1]:>5} call(s) "
              f"seed->{row[2]:08X} | console {row[3]:>5} call(s) seed->{row[4]:08X} "
              f"delta {row[1] - row[3]:+d}")
    if len(mismatched) > window_limit:
        print(f"[probe]   ... {len(mismatched) - window_limit} further mismatching update(s) not "
              "printed (--rand-calls-limit)")
    print(f"[probe]   total calls over the window: product {sum(row[1] for row in explained)}, "
          f"console {sum(row[3] for row in explained)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ticks", type=int, default=400,
                        help="main-loop iterations to step in lockstep after arrival (default 400)")
    parser.add_argument("--executable", type=Path, default=Path("build/bin/spyro_port"))
    parser.add_argument("--binary", type=Path, default=Path("scratch/assets/spyro1/SCUS_942.28"))
    parser.add_argument("--bios", type=Path, default=ROOT.parent / "SCPH1001.BIN")
    parser.add_argument("--selftest", action="store_true",
                        help="prove the differ on a fixture instead of driving cores")
    parser.add_argument("--trace", default="",
                        help="print the movement words side by side for FIRST:LAST tick, e.g. 550:560")
    parser.add_argument("--timeline", default="",
                        help="per-tick values of ADDR[,ADDR...] on BOTH cores, with the FIRST tick "
                             "each one differs on and whether the gap is constant or growing. This is "
                             "the mode that answers 'when did they part company', which --ram-diff "
                             "cannot: it sorts by ADDRESS, so its 'first' carries no time at all")
    parser.add_argument("--timeline-window", default="",
                        help="with --timeline, print every tick of FIRST:LAST (default: the first "
                             "differing tick, the two before it, and the last)")
    parser.add_argument("--moby-pool", action="store_true",
                        help="census the DYNAMIC moby pool on both cores at the last park: the block "
                             "base each core's g_DynMobys holds, how far its g_MobyAllocPtr has "
                             "walked, and every allocated slot's class and position. This is the "
                             "product-side half of 'which mobies exist', and it does not need a store "
                             "observer")
    parser.add_argument("--moby-pool-limit", type=int, default=24,
                        help="how many pool slots --moby-pool prints per core (default 24)")
    parser.add_argument("--spawner", action="store_true",
                        help="evaluate level 11's 'RETURN HOME' spawner PREDICATE on both cores at the "
                             "last park: every moby in [g_LevelMobys, g_DynMobys) whose dispatch "
                             "halfword is 9, its position, its already-spawned byte, OctDistance to "
                             "g_Camera.m_Position, and the branch the listing would take. This is the "
                             "one question -- true on the console, false on the product -- and it "
                             "needs no store observer")
    parser.add_argument("--spawner-state", type=int, default=SPAWNER_CLASS,
                        help="the m_Class --spawner looks for (default 9 = MOBYCLASS_EXIT_VORTEX, "
                             "whose handler is the spawner at 0x8007DDAC)")
    parser.add_argument("--moby-raw", action="store_true",
                        help="with --moby-pool / --census, dump each printed slot's whole 0x58 Moby "
                             "block as hex words on BOTH cores, side by side and marked per word. "
                             "include/moby.h disagrees with the image about where m_Class and "
                             "m_State live, so the identification has to come from the bytes: the "
                             "word that holds a letter class on one side is the class field")
    parser.add_argument("--census", default="",
                        help="run the pool census AND the spawner-predicate report at EVERY park in "
                             "FIRST:LAST, so the state on both sides is visible immediately before "
                             "and immediately after a tick where a count changed. One run, both ends "
                             "of the step -- a count read only at the end cannot say what freed them")
    parser.add_argument("--watch-store", default="",
                        help="arm the framework's store watch (PSXPORT_CW) on LO:HI, KSEG0 addresses, "
                             "and report every guest store into it with the PC that made it")
    parser.add_argument("--watch-print", default="",
                        help="with --watch-store, the FIRST:LAST tick whose stores are printed "
                             "(default: the first differing tick and one before)")
    parser.add_argument("--ram-diff", default="",
                        help="dump ALL of main RAM at FIRST and LAST and report every word that "
                             "agreed at FIRST and differs at LAST, e.g. 555:556")
    parser.add_argument("--ram-limit", type=int, default=200,
                        help="how many --ram-diff addresses to print (default 200)")
    parser.add_argument("--ram-word", action="append", default=[], metavar="ADDR",
                        help="print this guest word on BOTH cores at the --ram-diff sample ticks. "
                             "--ram-diff reports only the LAST value of a word that agreed at "
                             "FIRST, which cannot say whether a field was 0 (so a core WROTE a "
                             "new value during the update) or already non-zero (so a core left it "
                             "alone). Those are different causes and only this answers it.")
    parser.add_argument("--observe", default="",
                        help="console PC observation over one update: PC,PC,... with a RAM range of "
                             "address:bytes; armed at --observe-at and drained after ONE update")
    parser.add_argument("--observe-at", type=int, default=0,
                        help="the tick to arm --observe at (default: the first differing tick)")
    parser.add_argument("--rand-calls", action="store_true",
                        help="derive each core's per-update rand() CALL COUNT from the LCG chain of "
                             f"0x{G_RAND_SEED:08X} and report the first update where the two counts "
                             "differ. rand() is Sony's LCG and that word is its only state, so the "
                             "count is recoverable from two reads -- no store observer, no PC "
                             "observer, no product edit. This is a control-flow measurement, so it "
                             "puts a branch difference above a word that merely holds a different "
                             "number")
    parser.add_argument("--rand-calls-limit", type=int, default=40,
                        help="how many mismatching updates --rand-calls prints (default 40)")
    parser.add_argument("--store-observe", default="",
                        help="arm the framework's own translated-store observer "
                             "(PSXPORT_STORE_OBSERVE) on a comma-separated list of guest addresses and "
                             "print the product log's store-observe lines afterwards. The per-TARGET "
                             "report rows carry each address's own store counts and last guest PC, so "
                             "several addresses can be armed in one run and told apart -- which is "
                             "what makes a NEGATIVE interpretable: pair an address you know a guest "
                             "store reaches with one you expect to be silent, and only read the "
                             "silent one if the loud one fired. PSXPORT_DEBUG is set too, because the "
                             "per-callback lines are channel-gated while the arming and run-end report "
                             "are not")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    disc = drive.disc_path()
    if not disc:
        print("REFUSED: no disc; set PSXPORT_SPYRO_DISC in the environment or .env", file=sys.stderr)
        return 2
    environment = drive.environment(disc)
    environment.update(compare.product_env(argparse.Namespace(product_env=[])))
    watch = _parse_range(args.watch_store) if args.watch_store else None
    if watch:
        # The framework's own store watch: every guest store into [lo,hi) logs its width, address,
        # value and the PC that made it (runtime/psx/mem.cpp cw_check_slow). PSXPORT_CW_MAX=0 is
        # "unlimited", because the default cap of 64 would truncate exactly the stores this probe
        # exists to see -- they happen thousands of frames after the watch is armed.
        environment["PSXPORT_CW"] = f"{watch[0]:x},{watch[1]:x}"
        environment["PSXPORT_CW_MAX"] = "0"
        print(f"[probe] store watch armed on [{watch[0]:08X},{watch[1]:08X})")
    if args.store_observe:
        targets = [int(item, 0) for item in args.store_observe.split(",") if item.strip()]
        if not targets:
            print("REFUSED: --store-observe was given no address; nothing would be watched",
                  file=sys.stderr)
            return 2
        environment["PSXPORT_STORE_OBSERVE"] = ",".join(f"0x{target:08X}" for target in targets)
        # Lucent is built with LUCENT_CHANNEL_ENV="PSXPORT_DEBUG" (runtime/cpu/store_observe.cpp:12
        # of its own comment), so the per-callback lines need the channel on. The arming lines and the
        # run-end report are lucent::info and are not gated -- stated here so a quiet run is read
        # against the right expectation rather than the wrong one.
        environment["PSXPORT_DEBUG"] = "store-observe"
        print(f"[probe] store observer armed on {len(targets)} address(es): "
              + " ".join(f"0x{target:08X}" for target in targets))
    product = compare.Product(ROOT / args.executable, ROOT / args.binary, environment, ROOT, Path(disc))
    product = compare.fresh_card(product, OUT_DIR)

    native = compare_cores.NativeReplSession(str(product.binary), str(product.executable),
                                             product.environment, str(product.cwd),
                                             OUT_DIR / "native.log")
    console = compare_cores.ConsoleSession(ROOT / "external" / "psxport", product.disc, args.bios, "na",
                                           OUT_DIR / "console.log")
    try:
        print(f"[probe] watch set: {len(WATCH)} block(s), {WATCH_BYTES} bytes, {WATCH_WORDS} words")
        driver = compare.Driver(route)
        settle = None
        for core in (console, native):
            used, settle = route.reach_demo_playing(driver, core, route.FIELD_BUDGET, settle)
            print(f"[probe] {core.name}: demo_playing after {used} advance steps; "
                  f"session field count {core.frames}")

        # For each watch word: the first iteration at which it differs, its values there, and how
        # many of the scanned iterations it differed on. The answer is the smallest such tick.
        first_seen: dict[int, tuple[int, int, int]] = {}
        counts: dict[int, int] = {}
        scanned = 0
        previous_tick = None
        first_tick = None
        trace = _parse_trace(args.trace)
        ram_window = _parse_trace(args.ram_diff) if args.ram_diff else None
        ram_first: tuple[int, ...] | None = None
        ram_last: tuple[int, ...] | None = None
        ram_ticks: list[int] = []
        # The log is the ONLY place a guest store's PC appears on the product side, so the watch's
        # lines are attributed to ticks by the log byte offset reached at each park: the store that
        # produced a line at offset N happened during the tick whose recorded size first exceeds N.
        log_path = OUT_DIR / "native.log"
        offsets: list[tuple[int, int]] = []
        observe_at = args.observe_at
        observe = _parse_observe(args.observe) if args.observe else None
        timeline = _parse_addresses(args.timeline)
        series: dict[int, list[tuple[int, int, int]]] = {a: [] for a in timeline}
        rand_rows: list[tuple[int, int, int, int | None, int | None]] = []
        census = _parse_trace(args.census) if args.census else None
        for index in range(args.ticks):
            if observe and observe_at and native_words_tick(native) + 1 == observe_at:
                _arm_observer(console, observe)
            for core in (native, console):
                core.hold(frozenset())
                route.advance(core, 1)
            native_words = read_watch(native)
            console_words = read_watch(console)
            tick = native_words[guest_globals.kGameTick]
            if previous_tick is not None and tick != previous_tick + 1:
                print(f"[probe] tick sequence jumped {previous_tick} -> {tick} at iteration "
                      f"{index}: the two cores did not each run one update, so these samples are not "
                      f"the comparator's")
            previous_tick = tick
            if first_tick is None:
                first_tick = tick
            scanned += 1
            if observe and observe_at and tick == observe_at:
                _drain_observer(console, native_words, console_words)
            if ram_window and ram_window[0] <= tick <= ram_window[1]:
                native_ram = _ram_words(native)
                console_ram = _ram_words(console)
                ram_ticks.append(tick)
                if tick == ram_window[0]:
                    ram_first = (native_ram, console_ram)
                if tick == ram_window[1]:
                    ram_last = (native_ram, console_ram)
                print(f"[probe] full main RAM read at g_GameTick {tick}: "
                      f"{RAM_BYTES} bytes, {RAM_WORDS} words per core")
            if watch:
                offsets.append((tick, log_path.stat().st_size if log_path.exists() else 0))
            if trace and trace[0] <= tick <= trace[1]:
                _trajectory_row(tick, native_words, console_words)
            for address in timeline:
                series[address].append((tick, native_words[address], console_words[address]))
            if args.rand_calls:
                if rand_rows:
                    _tick, _, previous_native, _, previous_console = rand_rows[-1]
                    rand_rows.append((tick,
                                      _lcg_steps(previous_native, native_words[G_RAND_SEED]),
                                      native_words[G_RAND_SEED],
                                      _lcg_steps(previous_console, console_words[G_RAND_SEED]),
                                      console_words[G_RAND_SEED]))
                else:
                    rand_rows.append((tick, None, native_words[G_RAND_SEED],
                                      None, console_words[G_RAND_SEED]))
            if census and census[0] <= tick <= census[1]:
                _report_moby_pool(native, console, tick, args.moby_pool_limit, args.moby_raw)
                _report_spawner(native, console, tick, args.spawner_state)
            for address, native_value, console_value in differing(native_words, console_words):
                first_seen.setdefault(address, (tick, native_value, console_value))
                counts[address] = counts.get(address, 0) + 1

        if first_tick is None:
            print("[probe] REFUSED: no iteration was scanned, so nothing was measured")
            return 2
        always_equal = WATCH_WORDS - len(first_seen)
        print(f"[probe] scanned {scanned} iteration(s), g_GameTick {first_tick}..{previous_tick}; "
              f"compared {scanned * WATCH_WORDS} watch words; {always_equal} of {WATCH_WORDS} words "
              f"were equal on every one of the {scanned} iterations; {len(first_seen)} word(s) "
              f"differed on at least one")
        pre_existing = [address for address in first_seen if first_seen[address][0] == first_tick]
        fresh_ticks = [first_seen[address][0] for address in first_seen if address not in pre_existing]
        print(f"[probe] {len(pre_existing)} word(s) already differed on the FIRST sample "
              f"(g_GameTick {first_tick}); {len(fresh_ticks)} word(s) first "
              f"differed later:")
        for address in sorted(pre_existing):
            _report(address, *first_seen[address], counts[address], scanned, "PRE-EXIST ")
        for tick, native_value, console_value in sorted(
                (row for address, row in first_seen.items() if address not in pre_existing),
                key=lambda row: row[0]):
            address = next(a for a, row in first_seen.items() if row == (tick, native_value, console_value))
            _report(address, tick, native_value, console_value, counts[address], scanned, "FIRST AT  ")
        if watch:
            window = _parse_trace(args.watch_print) or _default_window(fresh_ticks, first_tick)
        if timeline:
            _report_timeline(series, first_tick, previous_tick,
                             _parse_trace(args.timeline_window))
        if args.rand_calls:
            _report_rand_calls(rand_rows, args.rand_calls_limit)
        if args.moby_pool:
            _report_moby_pool(native, console, previous_tick, args.moby_pool_limit, args.moby_raw)
        if args.spawner:
            _report_spawner(native, console, previous_tick, args.spawner_state)
        if ram_window:
            _report_ram(ram_first, ram_last, ram_ticks, args.ram_limit)
            if args.ram_word:
                print(f"[probe] the same word(s) at both samples, which is what --ram-diff omits:")
                _report_ram_words(ram_first, ram_last, ram_ticks,
                                  [int(text, 0) for text in args.ram_word])
        result = 0
    finally:
        for core in (native, console):
            core.close()
    # AFTER the product is closed: its log is written by a reader thread through a buffered file, so
    # a report read while the session is live sees a log that stops short of the last update.
    if watch:
        _report_stores(log_path, offsets, window)
    if args.store_observe:
        _report_store_observe(log_path)
    return result


STORE_OBSERVE_LINE = re.compile(r"\[store-observe\]\s?(.*)$")


def _report_store_observe(log_path: Path) -> None:
    """The product's own translated-store observer lines, with the per-target report rows kept whole.

    The arming lines, the callback lines and the per-target rows are three different claims and are
    printed as three: arming says how many addresses were accepted, a callback line says a translated
    block stored to one of them, and a per-target row says that address's own before/after counts plus
    the guest PC of its last store. A row that reads `MATCHED NONE of the N executed JIT
    instruction(s)` is the instrument's own negative WITH its scanned denominator attached, which is
    the only form of it worth quoting.

    The count of lines is printed next to the count of targets so "the observer ran and matched
    nothing" stays distinguishable from "nothing was armed".

    FIXED 2026-09-27 after a run that printed `0 arming/report row(s), 0 per-callback line(s)` over a
    log that held 24,337 of them. The classifier stripped only the FIRST `] `, which removed the
    timestamp but LEFT the `[store-observe] ` channel tag in front of the message, so
    `startswith("guest_pc=")` was false for every real line. That is not cosmetic: the report told a
    reader "0 per-callback lines" over an observer that had fired tens of thousands of times. The
    regex now captures the message after the channel tag, the callbacks are counted PER ARMED PC, and
    a fault line naming an armed PC is printed as the warning it is -- a store the instrument refused
    is a store that HAPPENED, so its absence from the callback list is not a negative result.
    """
    if not log_path.is_file():
        print(f"[probe] store observer: no product log at {log_path}, so nothing was observed")
        return
    lines = [match.group(1) for match in
             (STORE_OBSERVE_LINE.search(line)
              for line in log_path.read_text(errors="replace").splitlines()) if match]
    arming = [line for line in lines if line.startswith("watching ")]
    # The arming block and the per-target report block share the `  [n] 0xADDR` shape, and only the
    # report rows carry text after the address (`stores before=...` or `MATCHED NONE of ...`). Calling
    # the arming list a "report row" is the same class of error as calling a callback an arming line:
    # it makes the reader attribute a number to the wrong claim.
    armed_rows = [line for line in lines if line.startswith("  [") and len(line.split()) == 2]
    reports = [line for line in lines
               if line.startswith("report:") or (line.startswith("  [") and line not in armed_rows)]
    print(f"[probe] store observer: {len(lines)} store-observe line(s) in the log "
          f"({len(arming) + len(armed_rows)} arming line(s), {len(lines) - len(arming) - len(armed_rows)}"
          f" callback/report line(s))")
    callbacks = [line for line in lines if line.startswith("guest_pc=")]
    print(f"[probe]   {len(armed_rows)} armed target(s), {len(reports)} report row(s), "
          f"{len(callbacks)} per-callback line(s)")
    per_pc: dict[str, int] = {}
    for line in callbacks:
        pc = line.split("guest_pc=", 1)[1].split()[0]
        per_pc[pc] = per_pc.get(pc, 0) + 1
    if per_pc:
        print("[probe]   per-callback lines, by the guest PC the observer reported: "
              + ", ".join(f"{pc} x{count}" for pc, count in sorted(per_pc.items())))
    print("[probe]   NOTE: PSXPORT_STORE_OBSERVE's targets are matched against the guest PC of a "
          "translated STORE, not against a data address -- runtime/cpu/lightrec_executor.cpp's "
          "observeStore compares target.guestPc with the store's PC. So these are store INSTRUCTIONS, "
          "and a data address armed here reports nothing and means nothing.")
    for line in log_path.read_text(errors="replace").splitlines():
        if "observer rejected unsupported translated PC" in line:
            print(f"[probe]   THE OBSERVER FAULTED, so a zero for the PC it names is NOT a "
                  f"measurement: that store was REACHED and the instrument refused it. {line.strip()}")
    for line in reports:
        print(f"[probe]   {line}")
    if callbacks:
        print(f"[probe]   first and last per-callback line(s) (each names its own guest PC, so the "
              "per-PC counts above say which armed target fired):")
        for line in callbacks[:2] + (["..."] if len(callbacks) > 4 else []) + callbacks[-2:]:
            print(f"[probe]     {line}")


def _parse_trace(text: str) -> tuple[int, int] | None:
    if not text:
        return None
    first, _, last = text.partition(":")
    if not last:
        raise SystemExit(f"REFUSED: --trace wants FIRST:LAST, got {text!r}")
    return int(first), int(last)


def _parse_range(text: str) -> tuple[int, int] | None:
    first, _, last = text.partition(":")
    if not last:
        raise SystemExit(f"REFUSED: --watch-store wants LO:HI, got {text!r}")
    return int(first, 16), int(last, 16)


def _parse_addresses(text: str) -> list[int]:
    """A comma-separated list of guest addresses, every one of which must already be in the WATCH
    set. A word outside it is REFUSED rather than silently skipped, because a --timeline that
    watched less than it was asked to is the failure this probe exists to stop repeating."""
    if not text:
        return []
    addresses = [int(item, 0) for item in text.split(",") if item.strip()]
    for address in addresses:
        if not any(base <= address < base + size for _, base, size in WATCH):
            raise SystemExit(f"REFUSED: --timeline address 0x{address:08X} is not in the watch set, "
                             f"so it cannot be read; nothing was measured for it")
    return addresses


def _report_timeline(series: dict[int, list[tuple[int, int, int]]], first_tick, last_tick,
                     window: tuple[int, int] | None) -> None:
    """Per-tick values on both cores, and the FIRST tick each word differs on.

    This is the measurement --ram-diff cannot make. --ram-diff sorts by ADDRESS, so the word it
    prints first is the lowest-numbered differing word and carries no temporal information at all;
    the timeline is ordered by TICK, so its first row is the moment the two cores parted company.

    The gap's SHAPE is reported too, because "the counts differ" and "the counts differ by a fixed
    amount from one tick onward" are different diagnoses: a fixed gap is a branch taken a fixed
    number of times differently, while a growing gap is a rate difference. Both are stated, and
    the number of ticks the gap stood still for is a measurement rather than an impression.
    """
    print(f"[probe] TIMELINE over {len(next(iter(series.values())))} tick(s), "
          f"g_GameTick {first_tick}..{last_tick}")
    for address, rows in series.items():
        differing_ticks = [tick for tick, native, console in rows if native != console]
        if not differing_ticks:
            print(f"[probe]   0x{address:08X} {name_of(address):46} EQUAL on all {len(rows)} tick(s)")
            continue
        first = differing_ticks[0]
        native_at = next(n for t, n, _ in rows if t == first)
        console_at = next(c for t, _, c in rows if t == first)
        gap_at = lambda tick: next(n for t, n, c in rows if t == tick) - next(
            c for t, n, c in rows if t == tick)
        gaps = [gap_at(tick) for tick in differing_ticks]
        distinct = sorted(set(gaps))
        # The tick after the LAST tick both cores agreed is the moment the difference was CREATED;
        # the one before is the last agreement. Printed explicitly, because a reader otherwise has
        # to trust that the loop started at the right sample.
        agreed_before = [tick for tick, native, console in rows if native == console and tick < first]
        print(f"[probe]   0x{address:08X} {name_of(address):46} FIRST DIFFERS at g_GameTick {first} "
              f"(native {native_at} / console {console_at}, gap {_signed(native_at) - _signed(console_at):+d}); "
              f"last agreement {agreed_before[-1] if agreed_before else 'NONE IN WINDOW'}")
        print(f"[probe]       differed on {len(differing_ticks)} of {len(rows)} tick(s); gap took "
              f"{len(distinct)} distinct value(s) {distinct}; gap at the first differing tick "
              f"{gaps[0]:+d}, at the last {gaps[-1]:+d} -> "
              f"{'CONSTANT from that tick' if len(distinct) == 1 else 'NOT constant'}")
        # Where the gap changed, so a growing gap is a LIST of the ticks it grew on, not a verdict.
        changes = []
        for tick, gap in zip(differing_ticks, gaps):
            if not changes or changes[-1][1] != gap:
                changes.append((tick, gap))
        if len(changes) > 1:
            print(f"[probe]       gap changed at ticks " +
                  ", ".join(f"{t}({g:+d})" for t, g in changes[:12]) +
                  (" ..." if len(changes) > 12 else ""))
        if window is None:
            window = (max(first_tick, first - 3), first)
        if not (window[0] <= first <= window[1]) and window == (first, first):
            window = (max(first_tick, first - 3), first)
        print(f"[probe]       per-tick, g_GameTick {window[0]}..{window[1]}:")
        for tick, native, console in rows:
            if not window[0] <= tick <= window[1]:
                continue
            mark = "  " if native == console else "**"
            print(f"[probe]        {mark} tick {tick:>4} native {_signed(native):>9} console "
                  f"{_signed(console):>9} gap {_signed(native) - _signed(console):+d}")


def _report_moby_pool(native, console, tick, limit: int, raw: bool = False) -> None:
    """The DYNAMIC moby pool on both cores, slot by slot.

    WHY THIS EXISTS. g_DynMobyCount is a CALL COUNT, so a difference in it says how many allocations
    were missed and nothing about which mobies they would have been. The pool says that directly:
    each core's g_DynMobys gives the first dynamic slot, the 0x58 stride comes from the allocator's
    own free list (asm/42CC4.s:23-34), and a slot's byte of 0x48 is 0 for a live moby. The class is
    read at Moby+0x36, the halfword the level-11 update dispatches on (`lh $v1, 0x36($s3)` at
    0x8007DB30) and the one the per-level constructor writes (`sh $s0, 0x36($s3)`, level_20
    func_level_20_8008A258:16), which is what turns "ten fewer allocations" into "ten letter mobies
    the console made and the product did not". An earlier version of this docstring said the class
    was read at Moby+0x42, repeating external/spyro-1/include/moby.h's wrong offset; 0x42 is the
    moby's animation-flags byte (see anim_state_writers / issue 0133) and reading a class from it
    reports 0x71 for a moby whose flags happen to be 0x71.

    It needs no store observer, which matters: PSXPORT_STORE_OBSERVE is armed from native_boot_run,
    and the Spyro 1 product's frame loop is the title's own driver, so that surface never arms here.
    """
    print(f"[probe] DYNAMIC MOBY POOL at g_GameTick {tick}")
    rows = {}
    for core in (native, console):
        raw = core.read(G_DYN_MOBYS, 4)
        base = int.from_bytes(raw, "little")
        cursor = int.from_bytes(core.read(G_MOBY_ALLOC_PTR, 4), "little")
        count = int.from_bytes(core.read(G_DYN_MOBY_COUNT, 4), "little")
        spawner = int.from_bytes(core.read(G_SPAWN_MOBY, 4), "little")
        end = int.from_bytes(core.read(G_DYN_MOBY_SPACE_END, 4), "little")
        print(f"[probe]   {core.name}: g_DynMobys=0x{base:08X} g_MobyAllocPtr=0x{cursor:08X} "
              f"g_DynMobyCount={count} g_SpawnMoby=0x{spawner:08X} g_DynMobySpaceEnd=0x{end:08X}")
        rows[core.name] = (base, cursor, count, spawner, core)
    names = [core.name for core in (native, console)]
    first_base = rows[names[0]][0]
    if rows[names[1]][0] != first_base:
        print(f"[probe]   the two cores' pools START AT DIFFERENT ADDRESSES "
              f"(0x{first_base:08X} vs 0x{rows[names[1]][0]:08X}), so slot N is not the same moby on "
              f"both sides and no slot-by-slot comparison is meaningful; the counts above stand alone")
        return
    span = max(rows[name][1] for name in names) - first_base
    slots = min(span // MOBY_STRIDE + 1, limit)
    print(f"[probe]   both pools start at 0x{first_base:08X}; the cursor has walked "
          f"{(rows[names[0]][1] - first_base) // MOBY_STRIDE} / "
          f"{(rows[names[1]][1] - first_base) // MOBY_STRIDE} slots; printing {slots} of them")
    allocated = {name: 0 for name in names}
    for slot in range(slots):
        block = first_base + slot * MOBY_STRIDE
        for name in names:
            core = rows[name][4]
            # The guest's own liveness test, at the offset it actually uses: the level-11 update does
            # `lbu 0x48(Moby); sltiu < 0x80` before touching a moby, and MobyAlloc's free-list link
            # lives in the same byte, so 0x80 is the boundary. Testing a different offset here would
            # call a FREED moby "allocated" and hide the very thing this census exists to show.
            state = int.from_bytes(core.read(block + MOBY_ALIVE_OFFSET, 1), "little")
            if state < MOBY_ALIVE_LIMIT:
                allocated[name] += 1
        line = f"[probe]     slot {slot:>3} 0x{block:08X}"
        for name in names:
            core = rows[name][4]
            raw = core.read(block, MOBY_STRIDE)
            moby_class = int.from_bytes(raw[MOBY_CLASS_OFFSET:MOBY_CLASS_OFFSET + 2], "little",
                                        signed=True)
            state = raw[MOBY_ALIVE_OFFSET]
            pos = struct.unpack_from("<iii", raw, 0x0C)
            mark = ("DEAD(free-list 0x%02X)" % state if state >= MOBY_ALIVE_LIMIT else
                    f"class={moby_class:>5} [0x48]=0x{state:02X} pos=({pos[0]},{pos[1]},{pos[2]})")
            line += f" | {name}: {mark}"
        print(line)
    for name in names:
        print(f"[probe]   {name}: {allocated[name]} of the {slots} printed slots are LIVE (Moby[0x48] "
              f"< 0x{MOBY_ALIVE_LIMIT:02X}, the guest's own test); g_DynMobyCount says "
              f"{rows[name][2]} live mobies")
    if not raw:
        return
    print(f"[probe]   RAW 0x{MOBY_STRIDE:02X}-byte blocks, per WORD, '*' where the two cores differ:")
    for slot in range(slots):
        block = first_base + slot * MOBY_STRIDE
        native_raw = rows[names[0]][4].read(block, MOBY_STRIDE)
        console_raw = rows[names[1]][4].read(block, MOBY_STRIDE)
        native_words = struct.unpack(f"<{MOBY_STRIDE // 4}I", native_raw)
        console_words = struct.unpack(f"<{MOBY_STRIDE // 4}I", console_raw)
        if native_words == console_words:
            continue
        print(f"[probe]     slot {slot:>3} 0x{block:08X}")
        for index, (nw, cw) in enumerate(zip(native_words, console_words)):
            mark = " " if nw == cw else "*"
            label = dict(MOBY_FIELDS).get(index * 4, "")
            print(f"[probe]       {mark} +0x{index * 4:02X} {label:20} native {nw:08X} "
                  f"({_signed(nw)}) console {cw:08X} ({_signed(cw)})")
        # Bytes, because the fields that matter here are bytes and a word view hides which.
        differing_bytes = [i for i in range(MOBY_STRIDE) if native_raw[i] != console_raw[i]]
        print(f"[probe]       differing byte offsets: "
              f"{['0x%02X' % i for i in differing_bytes]}")


def _report_spawner(native, console, tick, state: int) -> None:
    """Level 11's EXIT-VORTEX spawner PREDICATE, evaluated on both cores from live guest RAM.

    The recovered control flow, from asm/nonmatchings/overlays/level_11/func_level_11_8007DA78.s:
    the level's moby update gates on Moby[0x48] < 0x80 (0x8007DB10-0x8007DB1C), then switches on a
    SIGNED HALFWORD at Moby+0x36 (0x8007DB30) which is `m_Class`. The arm for class 9 --
    MOBYCLASS_EXIT_VORTEX, include/moby.h:245 -- is reached at 0x8007DB98 and is the only one that
    builds text:

        0x8007DDB8  jal OctDistance          a0 = Moby+0x0C, a1 = &g_Camera + 0x28
        0x8007DDC0  lbu $v1, 0x48(Moby)
        0x8007DDC8  beqz $v1, .L8007DDE4     already-spawned? then use the tighter radius
        0x8007DDD0  slti $v0, $s1, 0x2800    RESET_DISTANCE
        0x8007DDD4  bnez $v0, .L8007DDF0     far but not too far -> keep the flag, no respawn
        0x8007DDE0  sb $zero, 0x48(Moby)     too far -> allow a later respawn
        .L8007DDE4:
        0x8007DDE4  slti $v0, $s1, 0x2400    SPAWN_DISTANCE
        0x8007DDE8  beqz $v0, .L8007DDE34
        .L8007DDF0:                          the spawn arm
        0x8007DDF8  bnez $v0, .L8007DE2C     0x48 already set -> skip the call
        0x8007DE10  jal func_8003C358        a1 = 0 -> g_LevelNames[36] = "RETURN HOME"
        0x8007DE2C  sb $t1, 0x48(Moby)       t1 = 1
        .L8007DDE34: sb $zero, 0x48(Moby)

    So the whole gate is: m_Class == 9 AND Moby[0x48] == 0 AND OctDistance < 0x2400. And
    func_8003C358 calls (*g_SpawnMoby) once per NON-SPACE CHARACTER of the string
    (src/moby_helpers.c:1640-1649), so "RETURN HOME" -- 11 characters, one of them a space -- is
    exactly 10 MobyAlloc calls. A gap of exactly 10 is that one call site.

    Every input is printed, so the answer is a comparison and not an inference. A run that finds the
    predicate FALSE on BOTH cores is a real answer too: it says the spawner did not run here, and
    nothing may be concluded from it about who did.
    """
    print(f"[probe] SPAWNER PREDICATE at g_GameTick {tick}: m_Class == {state} "
          f"({SPAWNER_CLASS_NAME if state == SPAWNER_CLASS else 'class'}) AND Moby[0x48] == 0 AND "
          f"OctDistance(Moby+0x0C, g_Camera+0x28) < 0x{SPAWN_DISTANCE:X}")
    found = 0
    for core in (native, console):
        level = int.from_bytes(core.read(0x80075828, 4), "little")
        dyn = int.from_bytes(core.read(G_DYN_MOBYS, 4), "little")
        camera = struct.unpack("<iii", core.read(CAMERA_POSITION, 12))
        count = int.from_bytes(core.read(G_DYN_MOBY_COUNT, 4), "little")
        print(f"[probe]   {core.name}: g_LevelMobys=0x{level:08X} g_DynMobys=0x{dyn:08X} "
              f"({(dyn - level) // MOBY_STRIDE} level mobies) g_Camera.m_Position at "
              f"0x{CAMERA_POSITION:08X} = ({camera[0]},{camera[1]},{camera[2]}) "
              f"g_DynMobyCount={count}")
        hits = 0
        block = level
        while block + MOBY_STRIDE <= dyn:
            dispatch = int.from_bytes(core.read(block + MOBY_CLASS_OFFSET, 2), "little", signed=True)
            if dispatch == state:
                hits += 1
                found += 1
                raw = core.read(block, MOBY_STRIDE)
                pos = struct.unpack_from("<iii", raw, 0x0C)
                spawned = raw[SPAWNER_FLAG_OFFSET]
                distance = oct_distance(pos[0], pos[1], camera[0], camera[1])
                spawn_now = spawned == 0 and distance < SPAWN_DISTANCE
                reset = spawned != 0 and distance >= RESET_DISTANCE
                print(f"[probe]     0x{block:08X} m_Class={dispatch} pos=({pos[0]},{pos[1]},{pos[2]}) "
                      f"[0x48]={spawned} OctDistance={distance} "
                      f"(<0x{SPAWN_DISTANCE:X}? {'YES' if distance < SPAWN_DISTANCE else 'no'}) "
                      f"-> {'WOULD SPAWN 10 MobyAllocs' if spawn_now else 'no spawn'}"
                      f"{'; flag would be CLEARED (>=0x{RESET_DISTANCE:X})' if reset else ''}")
            block += MOBY_STRIDE
        print(f"[probe]     {hits} moby/mobies with m_Class {state}")
    if not found:
        print(f"[probe]   NO moby in either core's level array has m_Class {state} at this park, so "
              f"the spawner arm was not reachable here at all; nothing is concluded from it")





def native_words_tick(core) -> int:
    return int.from_bytes(core.read(guest_globals.kGameTick, 4), "little")


def _parse_observe(text: str) -> tuple[list[tuple[int, bool]], list[tuple[int, int]]]:
    """`--observe 0x8003FE7C,0x8003FE40:0x80078A80:16` — PCs, then one address:bytes RAM range.
    Addresses are hex (guest addresses); the byte count is DECIMAL, because it is a count."""
    targets_text, _, ranges_text = text.partition(":")
    targets = [(int(pc, 16), False) for pc in targets_text.split(",") if pc]
    ranges = []
    for item in ranges_text.split(","):
        if not item:
            continue
        address, _, size = item.partition(":")
        ranges.append((int(address, 16), int(size)))
    if not targets or not ranges:
        raise SystemExit(f"REFUSED: --observe wants PC[,PC...]:address:bytes[,...], got {text!r}")
    return targets, ranges


def _arm_observer(core, observe) -> None:
    """The console's own read-only PC observer (docs/info/instruments/I005), driven over the session
    the comparator already owns with the protocol documented in external/psxport/tools/oracle/
    CONSOLE.md (`observe` / `observe_read` / `observe_off`). It reports SCANNED and MATCHED with
    denominators, so a target that never fires is a measurement and not a broken instrument."""
    targets, ranges = observe
    status = core._call({"command": "observe",
                          "targets": [{"pc": f"0x{pc:08X}", "return": follow} for pc, follow in targets],
                          "ranges": [{"address": f"0x{address:08X}", "bytes": size}
                                     for address, size in ranges],
                          "capacity": 128})
    print(f"[probe] console observer armed on {[hex(pc) for pc, _ in targets]} with ranges "
          f"{[(hex(a), n) for a, n in ranges]}: scanned={status['scanned']} matched={status['matched']} "
          f"retained={status['retained']} dropped={status['dropped']} {status['observation']}")


def _drain_observer(core, native_words, console_words) -> None:
    drained = core._call({"command": "observe_read"})
    status = drained["status"]
    records = drained["records"]
    print(f"[probe] console observer after the update: {len(records)} record(s); scanned="
          f"{status['scanned']} matched={status['matched']} retained={status['retained']} dropped="
          f"{status['dropped']} pairing_errors={status['pairing_errors']} {status['observation']}")
    targets = status.get("targets") or []
    for target in targets:
        print(f"[probe]   target 0x{target['pc']:08X}: entries={target['entries']} "
              f"returns={target['returns']}  (a COUNT, so a target the run never reached reads zero "
              f"against the scanned denominator above rather than being silently absent)")
    # gpr 31 is $ra, and a record is taken on ARRIVAL at the entry, so $ra is the address the
    # CALLER will resume at: for a callee reached by `jal`, that is the instruction after the call.
    # Naming it is what turns "this function ran" into "this function ran from THERE".
    # $t5 and $t8 because `func_800522C0` (asm/moby_lists.s:293) stores the moby animation state
    # through `sw $at, 0x40($t5)` with $t5 the list entry -- the moby -- and keeps its 8-byte staging
    # copy at $t8 = 0x1F800004, so those two registers are what identify WHICH moby a record is about.
    EXTRA_REGISTERS = ((8, "t0"), (13, "t5"), (16, "t8"))
    for record in records:
        gpr = record["gpr"]
        registers = " ".join(f"{name}={value:08X}" for name, value in
                             zip(("at", "v0", "v1", "a0", "a1", "a2", "a3"), gpr[1:8]))
        registers += "".join(f" {name}={gpr[index]:08X}" for index, name in EXTRA_REGISTERS
                             if len(gpr) > index)
        caller = f"ra={gpr[31]:08X}" if len(gpr) > 31 else "ra=?"
        print(f"[probe]   field={record['field']} pc=0x{record['pc']:08X} "
              f"next_pc=0x{record['next_pc']:08X} instr={record['instruction']:08X} {registers} "
              f"{caller} ram={record['ram']}")
    print(f"[probe] the same update left g_DynMobyCount = {native_words[G_DYN_MOBY_COUNT]} on the "
          f"product and {console_words[G_DYN_MOBY_COUNT]} on the console; "
          f"g_Spyro+0x88 (m_touchingMoby) = {native_words[G_SPYRO + 0x88]} / "
          f"{console_words[G_SPYRO + 0x88]}")
    core._call({"command": "observe_off"})


def _default_window(fresh_ticks: list[int], first_tick: int) -> tuple[int, int]:
    """The tick the first NEW difference appeared on, and the one before it: the first differing tick
    and the last tick that agreed are the two samples that bracket the divergence."""
    if not fresh_ticks:
        return first_tick, first_tick
    return max(first_tick, min(fresh_ticks) - 1), min(fresh_ticks)


STORE_LINE = re.compile(r"\[cw\]\s*#(\d+) store w(\d+) \[([0-9A-F]{8})\]=([0-9A-F]{8})\s+interp_pc=([0-9A-F]{8})")

# ALL of main RAM, for the --ram-diff census. The reference exposes main RAM only (oracle_spyro1's
# module docstring), and 0x200000 is the whole of it.
RAM_BASE = 0x80000000
RAM_BYTES = 0x200000
RAM_WORDS = RAM_BYTES // 4


def _ram_words(core) -> tuple[int, ...]:
    raw = core.read(RAM_BASE, RAM_BYTES)
    if len(raw) != RAM_BYTES:
        raise SystemExit(f"REFUSED: read {len(raw)} bytes of main RAM, expected {RAM_BYTES}")
    return struct.unpack(f"<{RAM_WORDS}I", raw)


def _ram_word_at(snapshot, address: int) -> int | None:
    """One guest word out of a full-RAM snapshot tuple, or None if the address is outside RAM."""
    if snapshot is None or not RAM_BASE <= address < RAM_BASE + RAM_BYTES:
        return None
    return snapshot[(address - RAM_BASE) // 4]


def _report_ram_words(first, last, ticks, addresses) -> None:
    """The SAME word on BOTH cores at BOTH --ram-diff samples, with the change across the update.

    --ram-diff prints only the last value of a word that agreed at the first sample, so it shows
    the two cores DISAGREE and nothing about how they got there. This prints the first sample too,
    which is what separates the two possible causes: a word that was 0 and became 1 was WRITTEN
    during the update, and a word that was already 1 and stayed 1 was simply NOT CLEARED. Those
    are different defects and the previous report could not tell them apart."""
    if not addresses:
        return
    for address in addresses:
        first_native = _ram_word_at(first[0] if first else None, address)
        first_console = _ram_word_at(first[1] if first else None, address)
        last_native = _ram_word_at(last[0] if last else None, address)
        last_console = _ram_word_at(last[1] if last else None, address)
        if None in (first_native, first_console, last_native, last_console):
            print(f"[probe]   0x{address:08X} REFUSED: outside main RAM, or a --ram-diff sample tick "
                  f"was not reached; nothing printed for it")
            continue
        agreed = "AGREED" if first_native == first_console else "ALREADY DIFFERED"
        native_move = _signed(last_native) - _signed(first_native)
        console_move = _signed(last_console) - _signed(first_console)
        print(f"[probe]   0x{address:08X}  at {ticks[0]}: native {first_native:08X} console "
              f"{first_console:08X} ({agreed})  ->  at {ticks[-1]}: native {last_native:08X} "
              f"console {last_console:08X}   native moved {native_move:+d}, console moved "
              f"{console_move:+d}")


def _report_ram(first, last, ticks, limit) -> None:
    """Every word that agreed at the first sample and differs at the second. Words that already
    differed at the first sample are counted and named separately: a word that was different before
    the window opened cannot be this window's divergence."""
    if first is None or last is None:
        print(f"[probe] --ram-diff REFUSED: read main RAM at ticks {ticks}, which is not the "
              f"requested pair; nothing was compared")
        return
    first_native, first_console = first
    last_native, last_console = last
    pre_existing = sum(1 for n, c in zip(first_native, first_console) if n != c)
    fresh = [(RAM_BASE + 4 * index, n, c) for index, (n, c) in enumerate(zip(last_native, last_console))
             if n != c and first_native[index] == first_console[index]]
    still = sum(1 for index, (n, c) in enumerate(zip(last_native, last_console))
                if n != c and first_native[index] != first_console[index])
    print(f"[probe] main RAM at g_GameTick {ticks[0]} and {ticks[-1]}: {RAM_WORDS} words compared; "
          f"{pre_existing} differed at {ticks[0]}; {still} of those still differ at {ticks[-1]}; "
          f"{len(fresh)} word(s) agreed at {ticks[0]} and differ at {ticks[-1]}")
    for address, native_value, console_value in fresh[:limit]:
        inside = ""
        for label, base, size in WATCH:
            if base <= address < base + size:
                inside = f"  <{label}+0x{address - base:X}>"
        print(f"[probe]   0x{address:08X} native {native_value:08X} ({_signed(native_value)}) "
              f"console {console_value:08X} ({_signed(console_value)}) "
              f"delta {_signed(native_value) - _signed(console_value):+d}{inside}")
    if len(fresh) > limit:
        print(f"[probe]   ... {len(fresh) - limit} more not printed (--ram-limit)")


def _report_stores(log_path: Path, offsets: list[tuple[int, int]], window: tuple[int, int]) -> None:
    """Every watch hit in the log, attributed to the tick whose park first passed its byte offset.
    The negative is printed too: a window with no lines says the watch saw no store there, and the
    scanned/match totals above say whether the instrument ran at all."""
    if not log_path.is_file():
        print(f"[probe] store watch: no product log at {log_path}, so no store was observed")
        return
    data = log_path.read_bytes()
    rows: list[tuple[int, int, str]] = []
    total = 0
    for match in STORE_LINE.finditer(data.decode("utf-8", "replace")):
        total += 1
        end = match.end()
        tick = next((t for t, size in offsets if size >= end), None)
        rows.append((tick if tick is not None else -1, int(match.group(1)), match.group(0)))
    print(f"[probe] store watch: {total} store line(s) in the log, {len(offsets)} tick park(s) "
          f"recorded; printing ticks {window[0]}..{window[1]}")
    printed = 0
    for tick, ordinal, line in rows:
        if window[0] <= tick <= window[1]:
            printed += 1
            print(f"[probe] store tick {tick:>4} hit {ordinal:>7} {line[line.index('store'):]}")
    print(f"[probe] store watch: {printed} of {total} store line(s) fell in the window")


def _trajectory_row(tick: int, native: dict[int, int], console: dict[int, int]) -> None:
    """One tick, both cores, the words a movement difference is read out of, with the per-tick delta
    so a step's SIZE is visible and not only its end state."""
    print(f"[trace] tick {tick}")
    for label, address in TRAJECTORY:
        n, c = native[address], console[address]
        mark = "  " if n == c else "**"
        print(f"[trace]  {mark} {label:12} native {_signed(n):>9} console {_signed(c):>9} "
              f"delta {_signed(n) - _signed(c):+d}")


def _report(address: int, tick: int, native_value: int, console_value: int, count: int, scanned: int,
            tag: str) -> None:
    residual = f"  [{ROUTE_RESIDUAL[address]}]" if address in ROUTE_RESIDUAL else ""
    print(f"[probe] {tag} tick {tick:>4} 0x{address:08X} {name_of(address):46} native "
          f"{native_value:08X} ({_signed(native_value)}) console {console_value:08X} "
          f"({_signed(console_value)}) delta {_signed(native_value) - _signed(console_value):+d} "
          f"differed {count}/{scanned}{residual}")


def selftest() -> int:
    """Both answers: the differ must report a difference and must report none when there is none."""
    words = {address: address for address in range(0x100, 0x140, 4)}
    words[guest_globals.kLevelTicks] = 4
    assert differing(words, dict(words)) == [], "an identical pair must produce no difference"
    other = dict(words)
    other[0x110] = 0xDEADBEEF
    found = differing(words, other)
    assert found == [(0x110, 0x110, 0xDEADBEEF)], found
    noise = dict(words)
    noise[guest_globals.kLevelTicks] = 7
    rows = differing(words, noise)
    assert [row[0] for row in rows] == [guest_globals.kLevelTicks], rows
    assert all(row[0] in ROUTE_RESIDUAL for row in rows), "a residual must be classifiable"
    print(f"[probe] differ selftest PASS: 1 of 1 planted difference reported, 0 of 1 when the words "
          f"are equal, 1 of 1 declared residual reported AND classifiable ({WATCH_WORDS} watch "
          f"words, {WATCH_BYTES} bytes)")
    # The rand()-count instrument, both answers: a real chain resolves to its length, and a word that
    # did NOT move through the LCG resolves to None rather than to a confident wrong number.
    seed = 0x60E3883E
    assert _lcg_steps(seed, seed) == 0, "no call must read as zero calls"
    target = seed
    for expected in range(1, 6):
        target = _lcg_step(target)
        assert _lcg_steps(seed, target) == expected, (expected, _lcg_steps(seed, target))
    assert _lcg_steps(seed, seed ^ 0x1, limit=64) is None, "a foreign word must not resolve"
    assert G_RAND_SEED == 0x80075AC0, "the seed word must be the one the listing names"
    print("[probe] rand-count selftest PASS: LCG^0..LCG^5 resolve to 0..5 calls, and a word the LCG "
          "cannot reach in 64 calls reads UNEXPLAINED rather than a number")
    # OctDistance, both arms, from the listing's own arithmetic at asm/math.s:81BC-81EC.
    #   dx=100, dy=40: at = 60 >= 0 -> scale a3 (dy): 100 + ((40*3)>>3) = 100 + 15 = 115
    #   dx=40,  dy=100: at = -60 < 0  -> scale a2 (dx): ((40*3)>>3) + 100 = 15 + 100 = 115
    #   dx=dy=64: at = 0 >= 0 -> scale a3: 64 + 24 = 88
    #   dx=100, dy=0: 100 + 0 = 100 ; dx=0, dy=100: 0 + 100 = 100
    for (ax, ay, bx, by), expected in (((0, 0, 100, 40), 115), ((0, 0, 40, 100), 115),
                                       ((0, 0, 64, 64), 88), ((0, 0, 100, 0), 100),
                                       ((0, 0, 0, 100), 100)):
        got = oct_distance(ax, ay, bx, by)
        assert got == expected, f"OctDistance({ax},{ay},{bx},{by}) = {got}, listing says {expected}"
    # The discriminator, not a coincidence: the two arms must be MIRRORS, and a transcription that
    # swapped them (the bug this test was added for) would agree on the symmetric case above and on
    # the axes, so it needs an asymmetric case where the arms give different numbers.
    assert oct_distance(0, 0, 100, 40) != 100 + ((100 * 3) >> 3) + 40, "the arms are still swapped"
    print("[probe] oct-distance selftest PASS: 5 of 5 listing-derived values match, including both "
          "asymmetric arms that a swapped transcription gets wrong")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
