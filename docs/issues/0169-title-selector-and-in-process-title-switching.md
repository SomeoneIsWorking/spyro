---
id: 169
title: Title selector and in-process title switching (and the stale-global defects a second boot exposed)
status: resolved
symptom: The product booted one hard-coded title per process; there was no way to choose Spyro 1/2/3 or to return and pick another
tags: picker,selector,session,teardown,globals,asan
created: 2026-10-01
updated: 2026-10-01
---

## Answer

`./run.sh` (zero arguments) now launches `spyro_port` with no argument, which opens an in-window selector
(psxport RmlUi `ChoiceScreen`) over the three catalog titles. An entry is enabled only when
`scratch/assets/<slug>/<serial>` exists and authenticates through `selectExecutableFile`; a disabled entry shows
why (`Not provisioned`, `Identity mismatch ...`). Pad/keyboard (Up/Down, Cross/Start/Enter) and the pointer
choose; the control channel has `picker` (list) and `pick <slug>`. A running title returns through the ESC
menu row "Return to Title Selection" or the control command `session return`; both only record a request in
`SessionControl`, and the host ends the session by destroying its `Game`. Nothing is written into guest state.

Structure: `ProductHost` loops `PickerSession` -> `TitleSession`. Each session owns its own `Game`; destruction is
the whole teardown. The explicit executable argument (`spyro_port <exe>`) is a maintainer override that skips the
selector and runs that one title; the launcher no longer has a title flag or positional disc.

## Defects found by the second boot, all fixed at the cause (psxport `picker`)

1. `GpuDevice` never released its window, device, pipelines or samplers (no destructor).
2. `RmlOverlay`'s destructor did not shut RmlUi down.
3. `DbgServer`'s thread handoff (mutex, condvar, command slot) lived inside the first Game and the detached thread
   kept using it after that Game died; the claim pointer dangled. The channel is now process-lifetime.
4. DMA registers (`s_dma3_*`, `s_dpcr`, `s_dicr`, `s_dma_done`) were file-scope statics in `mem.cpp`; now `Core::dma`.
5. Beetle GTE/SPU/MDEC/XA, `ProjParams` and `ProjPrim` bind points outlived the Game they named. AddressSanitizer
   caught it: heap-use-after-free in `GTE_Power` called from `gte_init` of the second title, freed by the first
   Game's destructor. `Game::releaseHardwareBindings()` returns every one to its default, and `TitleSession` binds
   a Game's peripherals before powering them.
6. `watchdog_init` kept the previous run's state (it would give the second title the 3 s steady budget instead of the
   boot grace) and the alarm outlived a title; `TitleSession` disables it at teardown.
7. A selector must not open or rotate the default pad recording: `Pad::useLiveInputOnly()`.

## Evidence

`tools/title_switch.py --pair A B --frames N` (headless, `agent_environment`, one process at a time): two fresh B
processes agree (control), then B started after A in one process has identical guest RAM and scratchpad at frame
N. All three pairs (1<->2, 2<->3, 1<->3) at N=300, both orders each, on framework 41373bc0 plus the
picker branch (Spyro 1<->2 also under an ASan build): IDENTICAL. Captures read: selector (selection moves with Down), Spyro 2 Insomniac logo, Spyro 1
Universal logo, Spyro 3 Universal/Insomniac legal screen, selector after return.

## Not verified / residual

Other file-scope statics noted but not proven harmful (`spu_beetle` `s_irq_core`/`s_spu_log` are now unbound at
teardown; `gte_beetle` `s_held_pz`/`s_held_src`, `render_queue`, `SpuAudio::sWavOwner`, `lightrec_executor` instance
map): the RAM comparison at frame N does not exercise gameplay, audio output or save state across a switch.
Windowed behaviour (the window is recreated between sessions, so it blinks) was not exercised by an agent.
