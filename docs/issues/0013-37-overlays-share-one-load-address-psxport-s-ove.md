---
id: 13
title: 37 overlays share one load address — psxport's overlay model keys by base and cannot represent them
status: open
symptom: Adding a second overlay to tools/ensure_recomp.py OVERLAYS + game/recomp_seeds.json overlay_bases would emit a second module at the SAME base 0x8007AA38 as OVL0, producing duplicate function addresses in the generated substrate.
tags: overlay,recomp,framework
created: 2026-07-28
updated: 2026-07-28
---

## What is true (C032)

All 11 static call sites of the CD loader 0x80016500 were read with tools/callsite_args.py (I006).
Six pass a1 = 0x8007AA38, each materialising it with the identical `lui a1,0x8001 / lw a1,0x13a0(a1)`
— one shared global, not six equal constants. All 11 references to [0x800113A0] in the resident text
are LOADS; there are zero stores (and none inside OVL0's body either). So it is a .data constant
holding the base of a single staging ARENA that sits just past text_end (0x80075800).

The remaining sites pass a cursor INTO that arena (0x80012994 passes s5; 0x800129C0 passes
s3 = s5 - [0x8008A6D4]). Argument a0 is the overlay INDEX — site 0x8001253C resolves it to 0x25 = 37,
matching the overlay count the public decomps describe.

## Why this blocks the obvious next step

psxport keys an overlay BY its load address: that is what makes a wrong base catastrophic, and it is
also what makes 37-overlays-at-one-address unrepresentable. The plan that was queued — 'read the
remaining call sites, pair with WAD index entries, extend OVERLAYS + overlay_bases' — assumed one
base per overlay. That premise is now falsified (C031 falsified, superseded by C032).

## Options, none yet chosen

1. Recompile each overlay into its own ADDRESS SPACE and dispatch on the currently-resident index
   (needs the substrate to route a call by (index, address) rather than address alone — a framework
   change, and the honest one).
2. Recompile only the overlay(s) a given boot/level actually needs. Works today, does not scale to
   the full game, and silently limits what the port can reach.
3. Native reimplementation of the overlay bodies, indexed. Jumps the RE frontier — not now.

Do NOT pick one by guessing which is cheaper. What decides it is how psxport's RecompRegistry keys
lookups and whether an index-qualified key is a small change or a redesign; read that first.
