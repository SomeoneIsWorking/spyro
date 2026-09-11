---
id: 107
title: Entering a level aborts — stage 1 (GS_LevelTransition) has no native producer, and its one missing piece is the HUD text builder
status: open
symptom: crossing a portal reaches the level-transition tally screen and the renderer aborts with "no producer is registered for this stage"
tags: render,transition,hud,text
created: 2026-09-11
updated: 2026-09-11
---

## Symptom

With the cyclorama facing test (0106) and the CdControlF binding both fixed, the portal walk crosses
into level loading and stops here:

    [render:error] NATIVE RENDER NOT IMPLEMENTED — stage selector = 1 (no producer is registered for this stage)
    [render:error]   fatal boundary: guest pc=0xDEAD0000 ra=0xDEAD0000 sp=0x801FFFF8 stage=1/3/2 load_stage=2 state_switch=0

Reproduce: `tools/drive.py gameplay --gate-teleport 0:0 --seek-portal --skip-transitions --after 1200`.

This is the honest boundary doing its job, not a regression. Stage 1 simply has no arm in
`SpyroRenderer::renderScene`.

## What stage 1 draws

`external/spyro-1` src/gamestates/draw.c: `GamestateDraw` sends both `GS_LevelTransition` and
`GS_EntranceAnimation` to `func_8001A050` (0x8001A050), which is short and mostly already owned:

1. Copy `g_Cyclorama.m_BackgroundColor` into both draw environments — the cyclorama clear colour the
   port's `cutscene_scene_recipe` already reads.
2. `Memset(&g_SonyImage, 0, 0x900)` — a lifecycle clear, like the `m_ShadedMobys` clear the field
   arm already reproduces.
3. `if (g_LevelTransHudActive) func_8001973C();` — the tally HUD. **The only unowned piece.**
4. `func_80023AC4()` — the Spyro actor producer. **Already owned** (`field_model_chain`).
5. When `g_Cyclorama.m_SectorCount != 0`, draw the cyclorama sectors through `func_8004EBA8`.
   **Already owned.** The transition uses the camera's own view/projection matrices except while
   `D_80075910` is winding down by 2 a frame, during which it builds a substitute pair by rotating
   about the camera's Y less that residual, then Z, then X, and scaling row 1 by 320/512.

So the arm is a composition of three owned producers plus one new recipe — not a from-scratch stage.

## The one missing piece, and why it is worth more than this screen

`func_8001973C` (0x8001973C) builds the tally: an "ENTERING <level>..." / "CONFRONTING <level>..." /
"RETURNING HOME..." caption whose Y eases in and out through `SINE_8`, a "TREASURE FOUND" /
"TOTAL TREASURE" caption on its own schedule keyed to `g_LevelTransTicks`, and a running gem counter.
Every one of those goes through **`func_800181AC`**, which lays a string into `g_HudMobys` — and
`func_800181AC` is unowned by the port.

That builder is the actual blocker, and it is shared: the pause menu and inventory
(`func_8001A40C`), and other screens, all draw their text through it. Porting it unblocks stage 1 and
several stages after it, so it belongs in its own cohesive owner rather than inside a transition
recipe.

After `func_800181AC` returns, `func_8001973C` walks back over the mobys it just appended and sets
each one's `m_Rotation.z` from `COSINE_8(g_LevelTransTicks * 2 + i * 12 & 0xFF) >> 7` — the
per-character wobble. That per-glyph post-pass is part of the tally, not of the text builder.

## The builder is ported, and it found a live bug

`game/render/hud_text_builder.{h,cpp}` owns both guest builders. They are deliberately two
functions, not one with a flag: `layoutCounter` (0x80017FE4) is fixed pitch and knows `/`, `%`, `^`
and `+`; `layoutCaption` (0x800181AC) is proportional, carries a full-width/narrow run across
characters, and knows `!`, `,`, `?` and `.`. Merging their class tables would put a slash in a
caption and an apostrophe in a counter. The layout half is pure and unit-tested; `append` performs
the guest's descending arena writes and refuses atomically when the arena cannot hold the string.

Consolidating it exposed a real defect. `fx_field_collectables.cpp` had a hand-written partial copy
of the counter builder for the completed-gem tally, and it had drifted from
`func_80017FE4("%d/%d", &vec{90,36,2880}, 28, 11)`: it wrote **every** glyph at x = 90, so the whole
string stacked into one column, and it omitted the per-glyph `m_Rotation.z` wobble the guest applies
from `g_Hud.m_GemSteadyTicks`. Both are fixed by routing it through the shared owner.

The Moby offsets it writes are anchored by three constants already in this tree — `m_Class` 0x36
(`actor_scene_oracle`), `m_DepthOffset` 0x47 (`moby_shadow_recipe`), `sizeof(Moby)` 0x58
(`actor_scene_builder`) — and the derivation from `include/moby.h` reproduces `m_State` 0x48
independently, so the layout is not a guess.

## What remains for stage 1

`func_8001973C`'s own schedule — the caption text selection from `g_NextLevelId`, the `SINE_8` ease
in and out keyed to `g_LevelTransTicks`, the "TREASURE FOUND" / "TOTAL TREASURE" phase changes, the
running gem counter — plus the per-glyph rotation post-pass, and then the stage-1 arm in
`renderScene` composing it with the three already-owned producers.

## Why this is the next task

It is the screen the Start-cancellation work targets. `spyro1_transition_skip` already classifies and
cancels both the stage-1 tally and the stage-10 return-home glide, and both are unit-tested, but
neither has ever been observed live because the route to them aborted first. With stage 1 rendering,
the tally cancellation becomes observable, and pressing on to the pause menu's Quit makes the
stage-10 one observable too.

## Related

- 0106 — the portal-entry refusals that were in front of this one.
