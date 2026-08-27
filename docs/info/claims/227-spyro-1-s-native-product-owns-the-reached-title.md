---
id: C227
kind: claim
status: holds
created: 2026-08-27
tags: render,native,title,frame
depends: game/render/title_menu_recipe.cpp#buildMode2, game/render/title_menu_state.cpp#read, game/render/render_frame.cpp#pairedActorScene, titles/spyro1/core/spyro1_frame_driver.cpp#Spyro1FrameDriver::stepFrame
---

## Claim

Spyro 1's native product owns the reached title mode-2 save picker under the host-owned frame loop: state 4 emits its complete eight-command recipe at 16:9 with interpolation enabled, including guest-suppressed updates, without guest VSync or a missing presentation fence.

## Evidence

Real SCUS_942.28, framework 3a8256e9: scratch/logs/spyro-mode2-native-wide-lerp-3a8256e9.log reaches 174 mode-2 state-4 calls, each recipe=8 emitted=8; announces fps60 ON, aspect=1/wide_engine=1/512->684; exits at 900 fields with frame-loop contract SATISFIED and zero bad-status matches. scratch/screenshots/spyro-mode2-wide-lerp-present-380.png was visually inspected and shows all three slots, menu choices, footer, Spyro and widened backdrop.

## What would falsify it

A real-disc native mode-2 run emits a command count different from the binary recipe, reaches guest VSync, advances the presentation fence other than once per product step, or produces a missing/corrupt slot UI at 16:9.
