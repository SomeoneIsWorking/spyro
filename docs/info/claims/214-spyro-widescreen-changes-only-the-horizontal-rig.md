---
id: C214
kind: claim
status: holds
created: 2026-08-22
tags: render,widescreen
depends: game/core/wide_clip_plan.h#clipCode, game/core/wide_clip_plan.h#isRightBoundLoad, game/core/wide_clip.cpp#run, game/core/native_terrain.cpp#terrain_build_direct
---

## Claim

Spyro widescreen changes only the horizontal right clip plane: the eligible guest load is lui ...,0x0200, while lui ...,0x0100 is the fixed 256-pixel vertical bound; the direct terrain producer uses the same axis mapping.

## Evidence

SCUS_942.28 words at 0x8004ED88/0x8004ED8C and 0x80026268/0x8002626C identify the adjacent 0x0100 vertical and 0x0200 horizontal loads; test_wide_clip_plan exercises both opcode discriminators and all clip planes; detached psxport 858b39cf Clang build passed 14/14 CTests; native boot gate gate-boot-20260822-120604.log passed 14/14; 16:9 present_800_16x9.png removes the reported long top/bottom triangles.

## What would falsify it

A real wide frame shows top/bottom geometry admitted by widening, isRightBoundLoad accepts a 0x0100 load, or changing the right bound alters above/below classification.
