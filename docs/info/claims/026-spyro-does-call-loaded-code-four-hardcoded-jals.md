---
id: C026
kind: claim
status: holds
created: 2026-07-28
tags: recomp
---

## Claim

Spyro DOES call loaded code: four hardcoded jals target 0x8007AA50-0x8007CEE4, above the resident text

## Evidence

Scanning direct jal instructions inside recompiled function bodies for targets outside text [0x80010000,0x80075800) yields two populations. (1) ~81 targets that are plainly garbage (0x8FFC7FFC, 0x8C3C3C00, 0x88C0C0C0 — repeating byte patterns above 2MB RAM), ALL called from 0x8006C000-0x80073000, i.e. data that emit.py recompiled as functions. (2) FOUR targets clustered just above text end and around heapBase 0x8007AA38: 0x8007AA50 (site 0x80033A44), 0x8007ABAC (site 0x800339DC — the observed recomp-MISS), 0x8007BFD0 (site 0x8001EF60), 0x8007CEE4 (site 0x8001EF18) — all called from genuine low game code. The miss instruction itself is 0x0C01EAEB at 0x800339DC, a DIRECT jal decoding to 0x8007ABAC, not an indirect jump through a pointer. A hardcoded call to a non-resident address is the game statically expecting code to be loaded there.

## What would falsify it

if those four call sites are shown to be unreachable, or the region is shown to be loaded with non-code at the moment they execute, this is wrong
