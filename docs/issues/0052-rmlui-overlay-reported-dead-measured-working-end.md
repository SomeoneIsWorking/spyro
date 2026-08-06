---
id: 52
title: RmlUi overlay reported dead — measured WORKING end-to-end in the windowed leg; the reproducible failure is a missing PSXPORT_ASSET_DIR, which the code then reports as success
status: investigating
symptom: ESC opens no menu / RmlUi overlay does not work in Spyro
tags: rmlui,overlay,ui,framework,psxport,diagnostics,measured
created: 2026-08-06
updated: 2026-08-06
---

## What was measured (2026-08-06)

Agents may not run windowed, and `RmlOverlay::init` is called only from
`gpu_vk.cpp` under `if (!s_headless)` — so the overlay is invisible to every
headless instrument. Worked around WITHOUT taking the user's screen by running
the real windowed leg under `SDL_VIDEODRIVER=offscreen`: `s_headless == 0`, a
real Vulkan swapchain, no window on any display.

Instruments (both in `scratch/rmlprobe/`, gitignored):

* `probe.cpp` — hermetic replica of init()'s asset half (font faces + LoadDocument).
* `esc_inject.c` — LD_PRELOAD: pushes real SDL key events into the process's own
  queue, and interposes `SDL_DrawGPUIndexedPrimitives` (ONE call site in the whole
  port: `rmlui_render_gpu.cpp:171`, the overlay's `RenderGeometry`), plus
  `SDL_CreateGPUBuffer` / `SDL_WaitForGPUFences`.

Results, with `PSXPORT_ASSET_DIR=external/psxport` (what `run.sh` sets):

* fonts 3/3 loaded, `LoadDocument` OK (6 tab / 6 pane / 33 select-button),
  `[rmlgpu] render interface ready`, `[rmlui] overlay up`.
  **NEITHER of the two loud diagnostics fires**, nor the third
  ("render interface init failed").
* ESC -> Down -> Enter x3 mutated the `ires` mod and produced
  `[gpu_vk] ires targets (re)built: 2048x1024 (scale=2) / 3072x1536 / 4096x2048`.
* overlay indexed draw calls: **0 with the menu closed (negative control),
  20654 -> 31467 over ~6 s with it open.**

So the overlay mechanism is NOT broken. The user's symptom was not reproduced
under run.sh's environment.

## The one reproducible failure

Launch the binary directly — the form the workspace CLAUDE.md documents
(`./scratch/bin/<port> scratch/bin/<game>/<EXECUTABLE>`) — i.e. without
`PSXPORT_ASSET_DIR`. `rml_asset()` then resolves cwd-relative to
`assets/rml/...`, and spyro's root has no `assets/`. Measured:

    Failed to load font face from assets/rml/FiraSans-Regular.ttf, could not open file.  (x3)
    [rmlui:warn]  WARNING: no fonts loaded (...)
    [rmlui:error] LoadDocument(assets/rml/menu.rml) FAILED - menu unavailable (...)
    [rmlui]       overlay up (ESC to toggle the menu)      <-- STILL PRINTED

`RmlOverlay::init` sets `mInited = true` and logs the success line after its own
fatal error, so ESC toggles `mVisible` on a null `mDoc` and nothing happens. A
user reading the log is told the overlay is up.

## Framework defects this exposes (psxport — NOT fixable game-side)

1. `rmlui_overlay.cpp:429` — "overlay up" is logged unconditionally after a
   failed `LoadDocument`. It must report the degraded state.
2. Asset resolution is one env var with a cwd-relative fallback and no
   executable-relative or build-baked default.
3. `gpu_vk.cpp:602` — `if (!s_headless) overlay_glue_init(...)`. The window is a
   sink, not a mode (PROTOCOL.md); the overlay's EXISTENCE should not depend on
   it, and this gate is why the failure cannot be diagnosed headless.
4. Scale mismatch: `rmlui_overlay.cpp:511` sizes the Rml context from
   `SDL_GetWindowSize` (LOGICAL points) while `RmlRenderInterfaceGpu`'s viewport
   and `uViewport` come from the SWAPCHAIN size in PIXELS
   (`show_present_image` -> `recordGpu(cmd, rp, sw, sh)`). Equal only at display
   scale 1.0 — which is all the offscreen driver can show, so this is a blind
   spot of the measurement above and the leading suspect for the user's report.
5. `rmlui_render_gpu.cpp` `OneShotWait` fence-waits per uploaded buffer, on the
   guest thread, inside the open present pass. Measured with the menu open:
   ~4200 `SDL_CreateGPUBuffer` and ~4200 `SDL_WaitForGPUFences` per 2 s
   (0 with the menu closed).
6. `overlay_glue.cpp:29` reads Tomba!2 guest addresses (0x1F8000D2.., 0x801FE00C)
   from framework code, and `refreshReadouts` decodes Tomba!2 stage pointers. In
   Spyro the world readout is meaningless and churns every frame, which is part
   of (5). This belongs behind a GameHook.
7. `pad_input.cpp:186` comment claims "the RmlUi overlay is dropped from the
   SDL_GPU build ... stubbed to 0". Stale and false.

## Not verified

Whether the user's actual run hits (4). Needs one windowed run by the user, or a
display-scale-aware measurement. Also unmeasured: legibility/layout of the menu
(the offscreen swapchain is never read back).

### Note (2026-08-06)
Defect 8: mods.cpp:3 says the settings file is '(gitignored)'. It is not — neither spyro/.gitignore nor Tomba2Engine/.gitignore lists psxport_settings.ini, and Mods::save() writes it to the REPO ROOT. Toggling one menu row therefore leaves an untracked file in the tree (observed this session; moved to scratch/rmlprobe/psxport_settings.ini.from_probe). Fix: add it to each .gitignore, or default the path into scratch/.
