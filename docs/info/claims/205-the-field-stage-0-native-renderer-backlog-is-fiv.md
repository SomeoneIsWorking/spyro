---
id: C205
kind: claim
status: holds
created: 2026-08-19
tags: render,widescreen,fps60,backlog,re
depends: game/render/scene.cpp#kFieldLayers
---

## Claim

The FIELD (stage 0) native-renderer backlog is FIVE 3D layers, not ten — and the ten layers' roles and the sixteen stage arms' identities are all RE'd. Stage 0's layers by measured GTE traffic: 0x80019698 actor pass 1244 COP2, 0x8002B9CC environment 276 (all in RenderWorldChunks 0x800258F0), 0x80050BD0 cyclorama 230 (bottoms out in EmitStaticActorMeshList 0x8004EBA8), 0x800573C8 particles 166 ALL IN ITS OWN BODY, 0x800189F0 tracers 15 (shared math helpers only); the remaining five (0x800521C0 moby list build, 0x80019300 collectables, 0x80018908 demo text, 0x800190D4 fade, 0x80018F30 border) are 0 COP2 over 64..459 instructions scanned each. Two of the five 3D layers bottom out in renderers this port already owns byte-exactly.

## Evidence

tools/field_layers.py over SCUS_942.28 (779 function extents, 511 nodes, 3050 direct edges): per-layer direct-call closure with COP2/LWC2/SWC2 counted over the closure AND separately over the layer's own body, every layer printing its closure size and instructions scanned so a zero is 'scanned N, found none'. The own-body count is what makes this measurement correct rather than plausible: 0x800573C8 particles carries all 166 of its COP2 ops in its own 843-instruction body and calls nothing, so the obvious method (walk the call graph to a known renderer) scores it 0 and would have dropped a whole 3D layer from the backlog. INDEPENDENT CORROBORATION of the roles, from a source that could not have produced this repo's addresses: the vendored decomp's GamestateDraw (external/spyro-1/src/gamestates/draw.c:2652) is the same if-chain and agrees with this repo's byte-derived transcription on all SIXTEEN arms — every handler address, both pairs that share a handler (4/5 -> 0x8001CA38, 2/3/6 -> 0x8001A40C, 1/9 -> 0x8001A050), the indirect arm 7, and both two-way splits; and its stage-0 arm (draw.c:2716-2747) has the same ten calls in the same order under the same gates, including the shift this repo derived on its own ([0x80075918]<<3 == g_Fade*8), which names the four gate globals g_IsFlightLevel/g_DemoMode/g_Fade/g_ScreenBorderEnabled.

## What would falsify it

a layer scored 2D that is seen SUBMITTING projected geometry on a real field frame (the closure walk is blind to jalr, so a table-dispatching layer could hide 3D work behind a 0), or a field frame in which one of the five 3D layers never runs — this is a static census of the arm, not a measurement of one scene
