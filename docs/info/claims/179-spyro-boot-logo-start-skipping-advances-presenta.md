---
id: C179
kind: claim
status: holds
created: 2026-08-14
tags: 
depends: game/core/vsync.cpp#deliver_field, game/core/cd_queue.cpp#lp_800127C0
---

## Claim

Spyro boot-logo Start skipping advances presentation time without bypassing loading or final setup

## Evidence

Shipping path brackets exact guest boot function 0x800127C0 and advances guest VBlank counter by its two holds' own 0xD2 threshold only on a post-baseline Start edge. scratch/logs/bootskip-positive.log: first-field DOWN suppressed; 2 later edges/advances; loading phase still 0->4->8->10; clean boot exit after 74 fields. scratch/logs/bootskip-negative.log: 437 fields, 0 edges, 0 advances, clean exit. PSXPORT_SELFTEST=bootskip passes 9 checks.

## What would falsify it

A run where held-at-entry advances; Start bypasses phase 4/8/10 or boot final setup; an idle run advances; or a post-baseline fresh edge does not shorten a boot hold.
