---
id: 128
title: "The crash after THE ADVENTURE BEGINS is the particle producer refusing type 3 — and the refusal path killed itself before saying so"
status: open
symptom: the product renders the "THE ADVENTURE BEGINS..." flyby card and then dies. REPRODUCED 2026-09-22 on the player launch path: the field particles producer refuses particle type 3, and abortUnimplemented then segfaults on a null Core::cfg two lines into its own report
state_items: S011
tags: crash,render,producer,transition,repro
created: 2026-09-19
updated: 2026-09-22
---

## The observation outranks the runs

Reported again 2026-09-19 with a screenshot of the card. The same observation falsified claim C228
on 2026-08-27, where it was recorded as "renders THE ADVENTURE BEGINS transition card and then
crashes" — so this is a standing, unresolved user-visible defect, not a new one.

Every agent run below reaches `GS_Playing at frame 6360` and exits 0. That does not weigh against
the report; it means the runs do not cover it. C228's own falsification says exactly this about the
run that preceded it.

## What the card is

`GamestateCutsceneTransition` (`external/spyro-1/src/gamestates/update.c`) — the flyby where Spyro
arcs across a black screen while the level loads, drawn by `func_8001E6B8` with the string chosen
from `g_TitlescreenState.m_DemoType` and `g_VisitedFlags[0]`. It runs at `g_Gamestate == 13`
(GS_TitleScreen) with `m_Mode == TSM_Demo`, `m_State == TSS_Active`. "BEGINS" rather than
"CONTINUES" means a fresh save.

## Reproduction matrix — all NEGATIVE

| varied | value | result |
|---|---|---|
| build | `build/bin` (agent) | reached GS_Playing |
| build | `build/player/bin` (the one run.sh launches) | reached GS_Playing |
| window | `PSXPORT_VK_WINDOW=1` under Xvfb (swapchain up, windowed present confirmed) | reached GS_Playing |
| pacing | `PSXPORT_NOPACE=0`, real-time | reached GS_Playing |
| settings | user's `psxport_settings.ini` (`aspect=3 fps60=1`, both confirmed active in the log) | reached GS_Playing |
| overrun | 900 and 3000 frames past GS_Playing | reached GS_Playing |
| all of the above together | windowed + paced + user settings + 900-frame settle | reached GS_Playing |

## What is NOT yet covered, and is where it must therefore be

- **Audio.** Every agent run keeps `PSXPORT_NOAUDIO`; the player path removes it. Untested here
  deliberately — an unattended run must not seize the audio device.
- **Real SDL input and the hand-navigated menu route.** The driver injects pad state through the
  REPL and takes its own path through the save picker. The user's route through the memory-card
  screens, and what is on their card, may differ.
- **Their GPU.** These runs used llvmpipe under Xvfb, not the real Vulkan device.
- `PSXPORT_DEBUG_SERVER=1` (player) versus `PSXPORT_REPL=1` (driver).

## The likely shape, from the code

`SpyroRenderer::renderScene` aborts by design on a stage with no producer or a producer that refuses
its recipe, printing:

```
[render:error] NATIVE RENDER NOT IMPLEMENTED — stage selector = N (...)
[render:error]   fatal boundary: guest pc=... stage=N/x/y load_stage=... state_switch=...
```

Stages 1, 8, 9, 13, 14 and the field stages all have producers now, so an unregistered stage is
unlikely; a producer REFUSING a recipe is the candidate. Issue 0103 is the same abort on stage 8.

**That line names the stage and the refusing producer.** It is the whole diagnosis, and one line of
it is worth more than another day of negative runs.

## REPRODUCED 2026-09-22, and why nine runs had missed it

Not by driving harder. By running the product the way the PLAYER launches it — `player_environment`,
no pad input at all — and letting the attract demo play itself into gameplay. Every previous run
drove to `GS_Playing` through `tools/drive.py`, which takes its own route and then stops; the demo
keeps going and reaches a scene with type-3 particles in it. The dimension that mattered was not
audio, window, GPU or pacing. It was *not steering*.

```
[render:error] NATIVE RENDER NOT IMPLEMENTED — stage selector = 0 (particles — particles producer
  0x800573C8 refused its atomic type-0/type-2 recipe: status=unsupported_type why=particle_type
  type=3 slot=801C3C38 records=5 points=0 lines=0 type2=0)
[render:error]   fatal boundary: guest pc=0xDEAD0000 ra=0xDEAD0000 sp=0x801FFFF8 stage=0/3/2
```

Deterministic: same refusal, same slot, on every run of the route.

### Why the diagnosis never reached anyone

`abortUnimplemented` printed those two lines and then **segfaulted**, in the one function whose
whole job is to explain itself. `mC->cfg->overlaySlots`: a title need not declare a legacy
`GameConfig` and Spyro never does, so `Core::cfg` is null for the entire run. The arm, the ARMED
field backlog, the projection state and the 2 MB RAM snapshot below it were never written, and the
operator saw a segfault rather than a named refusal. Same class as issue 0090.

Fixed here: the null case is reported as the defined answer it is ("this title declares no
GameConfig"), not skipped. The full report now prints, ending at the intended `abort()`.

### The run log is no longer thrown away

A player's run wrote its diagnostics to a terminal and nowhere else, which is the only reason this
took a month: the abort text existed on the operator's screen every time and was never capturable.
`player_environment` now defaults `PSXPORT_LOG_FILE` to the host's user-state location
(`$XDG_STATE_HOME/psxport/<title>/last-run.log`, `~/Library/Logs/...` on macOS,
`%LOCALAPPDATA%\Logs\...` on Windows), creating the directory and clearing the previous run's file.
It is a default: a caller that sets its own path keeps it. Gated by
`external/psxport/tools/port/test_launch_environment.py`.

## What type 3 is

From `external/spyro-1/asm/renderers/r_particles.s` (the renderer is still hand-written assembly,
not decompiled). `func_800573C8` dispatches on the record's type byte at +1: 0 points, 1 lines,
2 rotated textured quads, **3**, 4, 5, and a sixth default arm — the port implements 0, 1 and 2 and
refuses the rest.

Type 3 (`.L80057A74`) emits the same 0x28-byte `POLY_FT4` as type 2, from the same texture table
(`0x80076278[byte0] -> +8*(halfword@+0x10 & 0xff)`), the same colour word at +0x0C, the same
semi-transparency bit, the same UV mapping and the same OT depth and clip tests. It differs in
exactly one thing: the corners. Type 2 rotates a single `size` through the sine table and
re-projects four model-space offsets; type 3 is **axis-aligned in screen space** with two
independent half-extents, `sizeX = (word@+8 >> 16) & 0xff` and `sizeY = (word@+8 >> 24) & 0xff`,
scaled by the RTPS depth factor (`IR0 = MAC0 >> 12`, then `GPF 0` and `>> 12`), placed as

```
xy3 = center + (hw/2, hh/2)    xy2 = xy3 - (hw, 0)
xy1 = xy3 - (0, hh)            xy0 = xy1 - (hw, 0)
```

so the vertex order is TL, TR, BL, BR, which is what makes the shared UV mapping correct unchanged.

## Next

1. Implement the type-3 arm against the recipe/submitter that already owns types 0/1/2.
2. Types 4, 5 and the default arm are still unported and will refuse the same way. Reaching one is
   now a legible refusal rather than a segfault, but they are the same defect class.

### The superseded plan

## Next

1. Capture the abort text from a real `./run.sh` session — the last ~20 lines of the terminal.
2. Failing that, reproduce with audio enabled and real SDL input on the actual GPU.
3. Do not close this on the strength of green agent runs. See C228.
