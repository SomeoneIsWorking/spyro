---
id: C228
kind: claim
status: holds
created: 2026-08-27
tags: render,cutscene,runtime,widescreen,vsync
depends: game/render/render_frame.cpp#SpyroRenderer::drawFrame, titles/spyro1/core/spyro1_frame_driver.cpp, titles/spyro1/core/spyro1_field_scheduler.cpp, game/render/cutscene_scene_recipe.cpp, game/render/fx_screen_fade.cpp
---

## Claim

Spyro 1's native New Game route reaches stage 14 under host-owned timing and presents the binary-derived cutscene recipe at 16:9 without guest VSync.

## Evidence

Isolated real SCUS_942.28 run scratch/logs/spyro-new-game-isolated-wide-3c342ec3.log on Clang build b50db90-dirty+psxport-3c342ec3 exited rc 0 via REPL end after reaching stage selector 14. It announced aspect=1, wide_engine=1, native_width=512, render_width=684; reconciled 1962 logic frames with 0 dropped layers; reported frame-loop contract SATISFIED; re-earned actor/world/cyclorama rows and first-earned native fade 0x800190D4 on 7 frames. The 684x240 capture scratch/screenshots/spyro-stage14-wide-3c342ec3.png and six consecutive presents were visually inspected: coherent animated cutscene geometry fills the widened scene, with stable background/actors, no black side bars, corruption, or missing layer. No guest-VSync fatal/trap/violation appears in the complete log.

## What would falsify it

Any isolated native New Game run fails before or within stage 14, reaches the guest-VSync trap, reports a dropped layer or unsatisfied frame-loop contract, fails to emit a reached binary-required cutscene producer, resolves aspect=1 without a 684-wide picture, or produces a flat, corrupt, black-sided, or missing-layer stage-14 capture.
