---
id: C050
kind: claim
status: falsified
created: 2026-07-28
tags: recomp
falsified_on: 2026-08-21
---

## Claim

Six computed-jump case labels seeded correctly advance the port — the recompiler's mid-function mechanism is seed + main_reentry TOGETHER

## Evidence

I first added the six addresses to main_reentry ALONE and nothing changed — no label emitted, miss unmoved. Reading emit_func shows why: main_reentry does not create a label, it makes a body that runs off its end FALL THROUGH into the named address instead of returning, and that address must ALSO be seeded as a function in main. That is the documented Tomba!2 pattern (0x8010637C falling through into 0x801063F4, 'both seeded'). With the six addresses in BOTH lists, discovery goes 246 seeds -> 667 functions and the fail-fast MOVES from 0x8004C4EC to 0x8004C650 — a different case in the same unrolled region, i.e. the port now executes past the first run. Gate otherwise unchanged: 3931 frames, 3686400 bytes, 682 late prim-submitting frames, so the seeds regressed nothing.

## What would falsify it

The 0x8004C4EC miss returning, or any gate metric dropping below its pre-seed value.

## FALSIFIED 2026-08-21

Framework emitter semantics changed: current emit.py unions main_reentry directly into resident discovery and its positive test emits a wrapper, body, and dispatcher case from main_reentry alone. The old requirement to duplicate an interior PC in both main and main_reentry is historical evidence, not current guidance; duplicating it creates two apparent authorities.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
