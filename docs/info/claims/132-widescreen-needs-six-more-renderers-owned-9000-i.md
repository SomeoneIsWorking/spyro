---
id: C132
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen,ownership
---

## Claim

Widescreen needs six more renderers owned, ~9000 instructions total: 0x800580F4 (244), 0x8004F000 (303), 0x80022A2C (598), 0x8001F798 (1113), 0x80020F34 (1726), 0x800258F0 (4995). Of these, 0x800580F4 and 0x80022A2C are confirmed ACTIVE and differentially validatable.

## Evidence

Clip-bound scan (lui rX,0x0200) over real function extents from generated/rec_decls.h, correcting C127's family-span error. Identity probes at PSXPORT_NDIFF=40: 0x800580F4 40/40 exact, 0x80022A2C 40/40 exact, 0x8004F000 and 0x80050240 never called in the measured scene (0 calls, so unproven rather than failing). 0x8004EBA8 is already owned at 278 instructions and verified 400/400.

## What would falsify it

a renderer whose identity probe DIVERGES (it would need a different acceptance test), or a scene in which one of the never-called ones runs — 'not called' is a statement about the measured scene, not about the game
