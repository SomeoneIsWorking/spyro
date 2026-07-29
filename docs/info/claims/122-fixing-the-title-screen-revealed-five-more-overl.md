---
id: C122
kind: claim
status: holds
created: 2026-07-29
tags: overlay,router
---

## Claim

Fixing the title screen revealed FIVE more overlays. Spyro's overlays are byte ranges inside WAD.WAD streamed into one shared arena, and nothing in the executable enumerates them — the only authority is a run, so the discovered set is bounded by how far the port gets. With the title screen unblocked (C121) the port reaches new code and loads overlays no earlier run could: WAD offsets 0x18F000 (2048), 0x18F800 (524288), 0x20F800 (491520), 0x287800 (75776) and 0x7F2800 (57344), taking the set from 7 to 12. The router error at 0x800857CC was simply an unextracted overlay resident in the arena: its header (word0 = 11 entries, pointer table from 0x8007B0A0) has the same count-plus-table shape as OV_B83800's (word0 = 12, from 0x8007B150) but different values, which is why the closest candidate matched only 10 of 16 signature words.

## Evidence

tools/overlay_scan.py over a FORCE_BUTTONS run that gets past the title reports '12 overlay(s), 5 new this scan' from 8 arena loads. The cdq log shows the two arena loads the router could not match — a3=0x00287800 len=75776 and a3=0x007F2800 len=57344 — and the ovload log already labelled them '(none/unmatched, dest=0x8007AA38)' at load time. ensure_recomp.py then emitted 12 overlay modules, including ov_ov_7f2800 (9 functions recompiled after jal discovery, 109 jump-table case labels pruned).

## What would falsify it

A later run discovering further overlays, which would mean 12 is still only a lower bound — the set is bounded by how far the port gets, so it should be re-scanned after any change that reaches new code.
