---
id: C215
kind: claim
status: holds
created: 2026-08-22
tags: render,world,re
depends: game/render/world_recipe.cpp#adaptiveSubdivide
---

## Claim

RenderWorldChunks 0x800258F0 ends with generic adaptive subdivision of oversized near GT3/GT4 packets, not a special-surface renderer: D_8006D5E4 and D_8006D5C8 select triangle/quad child descriptors and the deferred FIFO is processed breadth-first while each child chain replaces its parent in the same OT slot.

## Evidence

Vendored target assembly external/spyro-1/asm/renderers/r_environment.s at 0x8002A0A0..0x8002A6B0: packet code at gp+3 selects GT4 vs GT3, projected SXY/RGB/UV fields are midpoint-expanded, D_8006D5E4/D_8006D5C8 provide child descriptors, 0x8002A668 appends oversized children to the FIFO, and 0x8002A63C..0x8002A674 splice the child chain into the original link. The body never reads g_Environment+0x10 m_SurfaceData.

## What would falsify it

A byte-derived trace shows any 0x8002A0A0+ record sourced from g_Environment.m_SurfaceData or a non-adaptive special-surface list.
