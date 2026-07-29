---
id: C127
kind: claim
status: holds
created: 2026-07-29
tags: gpu,widescreen,re
---

## Claim

Spyro's GTE projects ~25% of vertices outside the visible 512-wide frame (16.9% at sx<0, 7.9% at sx>=512), so a widened view has real geometry to show — but the terrain renderer trivially rejects faces using clip bounds that are IMMEDIATE constants in guest code (right bound 512<<16 at 0x8004ED8C).

## Evidence

PSXPORT_DEBUG=sxhist over a level run, f104500: n=3248364 verts, below0=548706 (16.9%), atOrAbove320=683934; bucketed, [512,inf) = 257142 (7.9%) and [0,512) = 2442516 (75.2%). Clip bounds read from the disassembly at 0x8004ED84-8C: lui t5,0x0001 / lui t6,0x0100 / lui t7,0x0200, tested at 0x8004EE0C-EE3C as sy<=0, sy>=256, sx<=0, sx>=512 with the codes ANDed across a face's 3 vertices.

## What would falsify it

a scene whose sxhist shows almost nothing outside [0,512) — that would mean this game culls before projecting and widescreen needs frustum work, not just clip bounds
