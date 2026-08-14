---
id: C193
kind: claim
status: holds
created: 2026-08-14
tags: render,terrain,producer
depends: game/core/native_terrain.cpp#terrain_submit_direct, game/render/render_frame.cpp#SpyroRenderer::renderScene
reconfirmed: 2026-08-14 08:09:22
verified_at: 2026-08-14 08:09:22
---

## Claim

Spyro's stage-13 seam directly owns the reached 0x8004EBA8 opaque untextured F3/G3 output as one authored-order PainterObject without guest packet/OT mutation.

## Evidence

scratch/logs/terrain_direct_oracle_85b535ef_final.log: 17/17 generated-leg calls PASS, 8,368 faces exact in object/source FIFO, F3/G3 class, SXY and RGB, all 11,800 guest N+1 vertex iterations exact, forced SXY corruption rejected; scratch/logs/terrain_direct_native_4100_85b535ef_postmode.log: rc=0, 876,293 native faces over 1,557 nonempty frames, 275 valid-empty calls, producer row native876293 guest0, unscoped-native0, no refusal/FATAL.

## What would falsify it

Any direct-vs-generated mismatch in ordered source/object, F3/G3, SXY/RGB or vertex-iteration count; any guest packet/OT write on the direct leg; any queue refusal/partial group, duplicate terrain object, unscoped native primitive, or changed producer total on the same corpus falsifies this claim. Same-frame paired/terrain coexistence and independently oracle-proven fractional raw XYZ are not claimed until a live corpus reaches and compares them.

## Re-confirmed 2026-08-14 08:09:22

Reverified combined default-pinned build at psxport 85b535ef; terrain oracle 17/17 and native 4100 gate remain green; terrainrecipe/pairedpose/default build pass
