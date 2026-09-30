---
id: 157
title: The interpolation census read zero in every layer and misfiled 184,138 items, and both were its own defects
status: fixed
symptom: the first per-category interpolation census reported 0 interpolated items in actors, world, particles and HUD on a route that was emitting millions, and filed 184,138 paired-actor and player items under world geometry
state_items: S020
tags: fidelity,interpolation,diagnostics,dead-tap,census
created: 2026-09-29
updated: 2026-10-01
---

`game/render/interp_census.*` accounts every captured queue item of a logic frame into five
categories (camera, actors, world, particles, HUD) and counts what the in-between present rebuilt
from two captured states, with the reason for every item drawn at its own endpoint. The measurement
it produced on the Artisans gameplay route is in `docs/project-state.md` (S020). Its own test found
two defects in it, recorded here because both are the workspace's known trap: a counter that reads
zero, or a classification that falls through to a plausible default, instead of failing.

1. **The reconstruction queue was walked before the layers emitted into it.** The presenter empties
   the redirect sink immediately before `reconstruct()`, so a walk at the START of the pass counts
   zero reconstructed items by construction while the layers go on to publish millions. It reported
   `interpolated=0` for actors, world, particles and HUD alike, with plausible denominators. The walk
   moved to the END of the pass, after every layer has published.

2. **A producer key was mistyped, and the fallthrough hid it.** `kPairedActor` was `0x80023acu`; the
   real key stamped by `fx_paired_actor.cpp` is `0x80023ac4`. An unknown publisher does not fail the
   classification, it lands in the world bucket, so 184,138 paired-actor and player items were
   reported as world geometry for a whole run with every number looking reasonable. The other fifteen
   keys were checked against the producer constants that define them and all match.

**The rule these confirm:** a partition whose default is a plausible category cannot catch a wrong
member. Every key in the table is asserted by name in `tests/test_interp_census.cpp`.

The earlier version of this issue also recorded nine guest RAM bytes that differed between 4:3 and
16:9. That was the aspect setting writing guest state, and it is issue
[0152](0152-widescreen-must-not-write-guest-state.md), fixed.
