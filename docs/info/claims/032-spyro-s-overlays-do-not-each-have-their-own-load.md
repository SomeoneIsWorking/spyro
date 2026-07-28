---
id: C032
kind: claim
status: holds
created: 2026-07-28
tags: overlay
---

## Claim

Spyro's overlays do NOT each have their own load base: they share ONE fixed staging arena at 0x8007AA38, sourced from a read-only constant at [0x800113A0]

## Evidence

tools/callsite_args.py (I006) over all 11 static call sites of the loader 0x80016500: 6 sites pass a1 = 0x8007AA38, and every one of them materialises it with the IDENTICAL pair 'lui a1,0x8001 / lw a1,0x13a0(a1)' — the same global, not six coincidentally-equal constants. A scan of all 11 references to offset 0x13A0 in the resident text finds ELEVEN LOADS AND ZERO STORES, so [0x800113A0] is a .data constant, not a runtime-updated pointer; its image value is 0x8007AA38, just past text_end 0x80075800. The remaining sites pass a CURSOR into the same region rather than an independent base: 0x80012994 passes s5 and 0x800129C0 passes s3 = s5 - [0x8008A6D4]. CORRECTION (same session): I first wrote that a0 = 0x25 = 37 'matches the decomps' overlay count, so a0 is the overlay INDEX'. That is WRONG. game/core/cd_queue.cpp already records a0 as constant 37 across every observed call because 37 is WAD.WAD's base LBA on this disc (established when Cd::setloc_lba landed). Two unrelated 37s; I read the coincidence as a finding. a0 is the archive's base LBA, and the per-call selector is a3 (byte offset into the archive) plus a fifth STACK argument. The shared-arena conclusion below does not depend on a0 and is unaffected.

## What would falsify it

Find a store to 0x800113A0 anywhere reachable (including in an overlay body, which this resident-text scan does not cover), or observe a running port loading overlay code to a base that is not derived from 0x8007AA38.
