---
id: 136
title: Start in gameplay aborts the product — GS_PauseMenu (stage 2) has no native producer
status: open
symptom: pressing Start during gameplay sets g_Gamestate to 2 and SpyroRenderer::renderScene calls abortUnimplemented, so the process dies with SIGABRT
state_items: S011
tags: render,menu,input,blocking,user-reported
created: 2026-09-27
updated: 2026-09-27
---

## User-reported, and it makes the title unplayable

Operator, 2026-09-27, with a backtrace from the player build:

    SpyroRenderer::renderScene -> SpyroRenderer::abortUnimplemented -> abort (SIGABRT, signal 06)
    SpyroRenderer::drawFrame -> spyro1::Spyro1FrameDriver::stepFrame -> FrameLoopShell::step -> main

`[render:error] NATIVE RENDER NOT IMPLEMENTED` precedes it. **This is the reason Spyro 1 does not currently
count as playable, and it was found only because the operator hit it, not by a gate.**

## Reproduced on demand

    $ uv run --frozen python -u tools/drive.py gameplay --tap start --after 300
    exit=1   (BrokenPipeError — the product process died mid-drive)

and from the run log (`scratch/logs/drive.log`):

    [render:error] NATIVE RENDER NOT IMPLEMENTED — stage selector = 2 (no producer is registered for this stage)
    [render:error]   fatal boundary: guest pc=0xDEAD0000 ra=0xDEAD0000 sp=0x801FFFF8 stage=2/3/2 load_stage=4294967295

`stage=2/3/2` is `g_Gamestate` / title state / sub-substate, and `load_stage=0xFFFFFFFF` is the
not-loading sentinel. `pc=0xDEAD0000` is a tombstone, not a live PC, so this is not a bad fetch that led
here — the renderer refused on a state it has no producer for.

## Root cause

`Scene::stage` is `g_Gamestate`. `game/render/render_frame.cpp` has a native producer for stages
**0, 4, 5** (`isFieldStage`), **1, 9** (`level_transition_scene`), **8** (dragon), **13** (front end) and
**14** (cutscene). Everything else falls through to `render_frame.cpp:295` and aborts.

**EIGHT OF SIXTEEN STAGES ABORT:**

| stage | gamestate | note |
|---|---|---|
| 2 | `GS_PauseMenu` | **this defect.** one press from gameplay |
| 3 | `GS_InventoryMenu` | same handler `0x8001A40C` as stage 2 |
| 6 | `GS_OldDragon` | same handler again |
| 7 | `GS_FlightResults` | INDIRECT: calls through `(*[0x8007567C])()` |
| 10 | `GS_ExitLevel` | **`TransitionSkip` already authorises a cancellation for it** |
| 11 | `GS_Fairy` | |
| 12 | `GS_Balloonist` | |
| 15 | `GS_Credits` | SPLIT on `[0x80075704] < 99` |

**Stage 10 is the sharp one.** `spyro1::TransitionSkip::classify` returns `Cancellation::ReturnHomeSequence`
when `stage == kStageExitLevel`, so the port can *authorise* a skip into a state the renderer cannot draw.
That is a latent version of this same defect and it is reachable without the player opening a menu.

## Why this also explains "Start should skip transitions but doesn't work"

`TransitionSkip::classify` returns `Cancellation::None` when it does not authorise, and its own comment says
the press then "falls through to whatever reads input next rather than counting as a skip". **What reads it
next is the menu.** So the two operator reports are one defect: the press does not skip, and then it kills
the process.

**The tally skip itself is NOT broken, and that was measured rather than assumed.** `tools/probe_skip_start.py`
drives a real portal crossing and taps Start on the first frame stage 1 is up. Two arms, same walk:

| arm | result |
|---|---|
| control, no press | reaches stage 1 with the tally HUD active, runs 400 more frames, **no abort**, ends in stage 9 |
| Start pressed | stage 1 goes **straight to stage 0**, stage-1 samples drop 43 -> 14, **no abort** |

So `Cancellation::LevelTransitionTally` works, and the card in the operator's screenshot ("In The World Of")
is a *different* screen that the probe never reached. Which screen it is, is open.

## What the fix must NOT be

`renderScene` refusing to present a scene it cannot draw is **correct behaviour**, and this issue must not be
closed by weakening it. No producer may be stubbed to return success, no guest byte may be written, and the
refusal must not be bypassed. The defect is that the port can put the guest into a state it cannot present,
so the fix is a real producer for stage 2 (and its stage-3 sibling, which shares the handler).

Handler `0x8001A40C` is a composition, not a monolith: it reads a gate global at `0x800758B8` and, when it
is zero, calls `0x800521C0`, `0x80019698`, `0x800573C8`, `0x80050BD0`, `0x8002B9CC`, `0x8005F764`; when the
gate is non-zero it branches to `0x8001A5E0`, which calls `0x8005FDD8` and writes `[0x800757B0]` while
clearing `[0x800758B0]`. **Three of those already have producers** — particles `0x800573C8`, cyclorama
`0x80050BD0`, environment `0x8002B9CC` — so this is composition plus the missing UI layer rather than a
from-scratch screen.

## Falsifier

A run in which `g_Gamestate` reaches 2 and the log contains `stage selector = 2` means the producer is still
absent. A run that reaches 2 and draws a black screen is **not** a fix either: the acceptance test is a
screenshot showing the menu, and "it stopped aborting" is not the deliverable.
