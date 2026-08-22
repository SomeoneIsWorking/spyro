---
id: 74
title: Widescreen title corrupts top and bottom geometry after clip-site rewrite
status: resolved
symptom: At a wide aspect the Spyro title backdrop grows long black/atlas triangles and missing polygon bands at the top and bottom, while 4:3 remains mostly intact
tags: render,widescreen,regression,clipping
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

The widescreen patch table had been shifted four bytes backward at every renderer site. It therefore
replaced the adjacent `lui ...,0x0100` **vertical** 256-pixel bound instead of the verified
`lui ...,0x0200` **horizontal** 512-pixel bound. The direct terrain producer repeated the same axis
swap: its clip code compared `sy` with the dynamic wide width. Widening the vertical plane admitted
off-screen vertices and produced the long top/bottom triangles visible in the report.

An unfinished title-backdrop path obscured that defect by calling guest renderer bodies from the
shipping native picture path, clearing guessed guest buffers, and walking their guest OT output. It
duplicated the terrain producer and did not establish native ownership of the world or actor layers.

## What was tried / dead ends

The guest-renderer fallback could make the title appear more complete at 4:3, but it was not a valid
fix: it bypassed the native-producer boundary and retained the axis-corrupting patches. Extending the
frame clear would also be a presentation mask rather than a geometry fix; issue 0039 already measured
that approach changing no pixels on the underlying guest path.

## Resolution

`wide_clip_plan.h` is now the single source of truth for Spyro's clip axes and patch-word validation.
Only a `lui ...,0x0200` right-bound load is eligible for replacement; a `0x0100` vertical load is
rejected. `wide_clip.cpp` uses the verified horizontal addresses and `native_terrain.cpp` uses the
same clip-code function, widening only the right plane while leaving the 256-pixel vertical plane
unchanged. The guest-renderer/OT fallback was removed.

`test_wide_clip_plan.cpp` includes positive and negative instruction discriminators, all four clip
planes, a wide-X recovery case, and a proof that changing the right bound cannot change vertical
classification. A detached clean-framework Clang build passed 14/14 CTests and the native boot gate
passed 14/14 checks at stage 13 with 704012 attributed native primitives. A real 16:9 capture no
longer contains the corrupt top/bottom triangles.

The capture also exposes a separate, pre-existing frontier item: the native title backdrop remains
incomplete until the world and actor guest-packet renderers become direct native producers. This
resolution deliberately does not hide that missing work by restoring guest geometry to the native
picture.
