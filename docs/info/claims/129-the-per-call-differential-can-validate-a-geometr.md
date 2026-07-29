---
id: C129
kind: claim
status: holds
created: 2026-07-29
tags: render,ownership,differential
---

## Claim

The per-call differential CAN validate a geometry renderer: the generated body of 0x8004EBA8 run against itself reports 8/8 exact matches, 0 divergences.

## Evidence

PSXPORT_NDIFF_IDENTITY=1 PSXPORT_NDIFF=8 installs an identity probe (game/core/native_render.cpp) that hands ndiff_run the generated body as BOTH the native replacement and the substrate reference. ndiff snapshots RAM + scratchpad + all GPRs + the COP2/GTE register file, runs the body, rewinds, runs it again, compares: 8 calls, all 'matches the recompiled body exactly'.

## What would falsify it

a divergence on a LATER call, or on a different renderer — this proves reproducibility for THIS function over its first 8 calls, not for the family. Re-run the probe per renderer before owning it, and note that a renderer which submits to the GPU directly (rather than writing packets to RAM for a later DMA) would leave host state the rewind cannot undo and could NOT be validated this way.
