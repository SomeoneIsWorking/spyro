---
id: 128
title: "User-reported crash after THE ADVENTURE BEGINS card: not reproduced by any agent run yet"
status: open
symptom: the operator reports the product rendering the "THE ADVENTURE BEGINS..." flyby card and then crashing before gameplay. Seven agent runs across build, window, pacing, settings and a 900-frame overrun all reach GS_Playing cleanly. The crash text has not been captured
state_items: S011
tags: crash,render,producer,transition,repro
created: 2026-09-19
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

## Next

1. Capture the abort text from a real `./run.sh` session — the last ~20 lines of the terminal.
2. Failing that, reproduce with audio enabled and real SDL input on the actual GPU.
3. Do not close this on the strength of green agent runs. See C228.
