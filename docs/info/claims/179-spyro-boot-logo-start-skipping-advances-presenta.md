---
id: C179
kind: claim
status: falsified
created: 2026-08-14
tags: 
depends: game/core/vsync.cpp#deliver_field, game/core/cd_queue.cpp#lp_800127C0
---

## Claim

Boot-logo Start clock advancement is not a valid skip route

## Evidence

The removed shipping path bracketed `0x800127C0` and advanced the guest VBlank counter by `0xD2` on a host Start edge. Although it preserved the observed loading phases and final setup, it fast-forwarded the simulation clock rather than taking a recovered title-owned cancellation/transition route. No such route is currently known, so the enhancement and its selftest were removed; only passive `skipmap` observation remains.

## What would falsify it

The route is reinstated without a complete title-owned cancellation/transition routine, or Start changes a boot timer, phase, scene, or callback lifetime.
