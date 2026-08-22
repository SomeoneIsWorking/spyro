---
id: 75
title: Native title picture is incomplete after removing guest-renderer fallback
status: resolved
symptom: At stage 13 on the shipping native path, widescreen terrain renders but world cliffs, title actors, and parts of the backdrop are black or expose atlas rows
tags: render,widescreen,title,native-producer,reported
created: 2026-08-22
updated: 2026-08-22
---

## Root cause

The stage-13 native seam submitted only the menu sprites (`0x8007CD38`) and static cyclorama
(`0x8004EBA8`). The retail arm also prepares and draws the regular actor list through
`0x800521C0 -> 0x8001F158 -> 0x8001F798` and draws the ground/cliffs through `0x800258F0`.
Removing the prohibited guest OT/GTE fallback exposed those missing display owners as black space.

The first direct actor scene-builder attempt also decoded two handwritten-assembly conventions
incorrectly: the list terminator is selected by the low state byte while list class is the high byte,
and the culling MVMVA loads IR1/IR2/IR3 in Y/Z/X order. Those mistakes rejected all four title
candidates even though the retail preparation retained two.

## What was tried / dead ends

- Reusing the durable record area without running the retail preparation: refused correctly because
  the port-owned frame loop never materializes those transient records.
- Reading guest packets, the OT, or ambient GTE output: rejected as a picture source; that would
  reinstate the fallback whose removal exposed the defect.
- Treating the culling transform as an ordinary XYZ matrix operation: retail-record oracle showed
  different view vectors. Ghidra/decomp plus the live record comparison identified the Y/Z/X lane
  order.

## Resolution

The actor portion now has a direct semantic producer. It walks persistent Moby/model/animation state,
builds immutable actor records, projects float XY/depth, and submits PainterObjects without running a
guest renderer or consuming packet/OT/GTE output. An explicit diagnostic oracle independently runs the
retail list/preparation and compares every semantic prefix output; a 600-present run repeatedly matched
both retained records exactly, while a normal 900-present run completed without producer refusal.

The world portion now also has a direct semantic producer. It reads persistent sector/chunk state,
constructs all reached LQ, HQ-direct, medium, near and adaptive families, and submits immutable
faces without executing the generated body or consuming guest packet/OT/GTE output. Its final-stream
oracle matched 1,275 changing retail calls over 3,000 frames; depth is proven separately through the
shipping pure-projection seam because final GT3/GT4 packets do not encode vertex SZ.

Two later visual defects had separate owners:

- The shipping native frame omitted the selected title overlay's transient write of `0x0001C000`
  to `g_Environment+0x28` immediately before `RenderWorldChunks(-1)`. Zero made the semantic LQ
  far limit reject every low-detail face, which was the missing mountain layer. The typed
  `stage13_scene_recipe` now owns that invocation state. It also refuses the common backdrop for
  title mode 3, whose retail handler never calls actor/world/cyclorama.
- Actor, world and cyclorama were flattened as separate painter composites, so the far OT-2047
  cyclorama could depth-cover nearer actors and world. The framework now merges them through one
  authored replay domain using explicit OT-bin/link/suborder keys and preserves physical flush
  boundaries.

The remaining top/bottom noise is not missing Spyro geometry. The guest draw area is y=8..231;
the reference leaves rows 0..7 and 232..239 black. The authored untextured painter shader omitted
the draw-area clip that the ordinary untextured and all textured paths already enforce, so sky
triangles spilled into both guard bands. The apparent upper-right black triangle is the only part
of the guard band the spilling face did not overwrite. Resolution therefore belongs in the generic
painter shader by carrying and enforcing each item's existing draw-area rectangle, with outside and
inside readback controls; clearing or adding Spyro geometry would be a bandaid.

The framework fix is verified through the Spyro executable's real Vulkan selftest:
`outside=0000`, `inside=001F`, both exact. The final 16:9 stage-13 capture is
`scratch/screenshots/world-stage13-authored-clip-wide-f701.png`: all 684 columns are authored scene
content, actor/world/cyclorama share one replay domain, and both eight-row guard bands contain one
color, black. No atlas pixels, triangular holes, or noisy strips remain.
