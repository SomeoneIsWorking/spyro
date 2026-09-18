---
id: 113
title: "Secondary actors abort outside Artisans: the specular lighting program is unimplemented"
status: open
symptom: the port aborts with 'secondary/shaded actor producers 0x80020F34/0x80022A2C refused their combined atomic recipe' whenever a secondary actor with control bit 2 is drawn; first seen in the attract demo, now reproduced on demand by entering Stone Hill
tags: render,field,actor,secondary,lighting,re,crash
created: 2026-09-14
updated: 2026-09-18
---

## Symptom

```
[render:error] NATIVE RENDER NOT IMPLEMENTED — stage selector = 0
  (secondary/shaded actor producers 0x80020F34/0x80022A2C refused their combined atomic recipe)
[render:error]   fatal boundary: guest pc=0xDEAD0000 ra=0xDEAD0000 sp=0x801FFFF8
  stage=0/3/2 load_stage=4294967295 state_switch=0
[watchdog] FAULT (signal): backtrace:
  SpyroRenderer::abortUnimplemented -> renderScene -> drawFrame -> Spyro1FrameDriver::stepFrame
```

## Reproduction (deterministic, 2026-09-18)

Walk out of Artisans through any portal. The refusal follows within a few hundred fields of the
level becoming playable:

```
uv run --frozen python tools/drive.py gameplay --seek-portal --debug fieldactors --after 900
```

The same route inside `tools/oracle_compare.py`'s `level` checkpoint aborts the product the same
way, after both cores have entered level 11.

This supersedes the earlier attract-demo observation, which was never deterministic. The attract
demo simply happened to be the only route that drew such an actor; the discriminator is the actor,
not the demo. Every ordinary driven run stayed inside Artisans, whose secondary actors do not use
this program, which is why nothing saw it for so long.

## Root cause

`fieldactors` names it exactly:

```
[fieldactors] REFUSED secondary recipe=3 reason=0 record=0 source_word=65
```

Status 3 is `secondary_actor_recipe::Status::UnsupportedLighting`. `game/render/secondary_actor_recipe.cpp`
refuses any emitted candidate whose first prefix word has bit 2 set:

```cpp
// Bit 2 selects the distinct view-normal/specular program at 0x80021C70.
// Base material colour does not reproduce that lighting contract.
if (candidate.evaluation.emitted && (candidate.input.words[0] & 4u) != 0u) {
```

The refusal is the designed deliverable for an unported variant, not a defect in itself. The gap is
the missing program: retail's `func_80020F34` (external/spyro-1 `asm/renderers/r_moby.s`, from asm
line 2069; the arm begins at guest `0x80021C70`) computes a per-face specular term from the view
normal instead of taking the base material colour. It is not decompiled to C, so recovering it is
MIPS reading.

## Why it matters

It is the first hard stop on any route that leaves Artisans, so it blocks representative gameplay
(state item S011) rather than one cutscene. It also bounds what the oracle's `level` checkpoint can
compare: the product dies partway through the segments that follow the level entry.

## Not the same as issue 0103

0103 is the dragon cutscene's shaded producer refusing a single un-implemented variant; that is fixed.
