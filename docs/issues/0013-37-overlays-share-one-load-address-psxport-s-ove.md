---
id: 13
title: 37 overlays share one load address — psxport's overlay model keys by base and cannot represent them
status: resolved
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
s3 = s5 - [0x8008A6D4]). (Argument a0 resolves to 0x25 = 37 at site 0x8001253C. I first read that as the overlay INDEX matching the
decomps' overlay count — WRONG: 37 is WAD.WAD's base LBA, already recorded in cd_queue.cpp. Corrected in C032.)

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

### Resolution (2026-07-28)
NOT A BLOCKER — the premise was wrong, corrected the same session it was written.

I wrote this issue asserting 'psxport keys overlays by load address and cannot represent 37 at one
address', and listed three options including a framework redesign. I had not read
external/psxport/runtime/recomp/overlay_router.cpp. It opens with exactly Spyro's situation:

  'each OVERLAPPING stage overlay \\BIN\\*.BIN (ov_<tag>_dispatch — they all load to the SAME base
   0x80106228, so a given address is different code per resident overlay)'

The framework already models a shared arena as an overlay SLOT and identifies WHICH overlay is
resident by matching a content SIGNATURE against guest RAM at the slot base (RecOverlay::sig, baked
in at emit time), caching per core and re-scanning when the live RAM stops matching. GameConfig has
`OverlaySlot overlaySlots[3]` for precisely this. Multiple overlays may therefore share one base:
overlay_bases is keyed by TAG, so several tags can name the same address.

The real remaining work is wiring, not a redesign:
  1. GameConfig overlaySlots[0] = { 0x8007AA38, "ARENA" }  (all three are 0 today, so slot_index()
     returns -1 and no load-time identity is ever recorded — it works for the single overlay only
     because resident_overlay() falls back to a full signature scan).
  2. Call overlay_note_load(c, dest) from the native CD loader override in game/core/cd_queue.cpp,
     right after the image is copied — the router's comment is explicit that the signature matches
     THEN, before the game mutates the image's header pointer table.
  3. Add further overlays to tools/ensure_recomp.py OVERLAYS with the SAME base.

C032 (one shared arena, a0 = overlay index) still holds and is unaffected — only my conclusion about
what it implied for the framework was wrong. Lesson, and it is the same one this port keeps
relearning: I reasoned about psxport's capabilities instead of reading psxport.
