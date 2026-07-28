---
id: C075
kind: claim
status: holds
created: 2026-07-28
tags: ownership,native
---

## Claim

This port owned almost nothing natively: of 21 installed overrides, 20 super-call the recompiled body (observation wrappers) and exactly one is a native body. rand() 0x8006272C is now the first real replacement, verified byte-exact against the substrate on 200 consecutive calls.

## Evidence

Inventory of every shard_set_override/platform_hle.register_ site in game/core/, checking each handler for a gen_func_* super-call (including through the CD_PROBE and lp_ macros): 20 observe, 1 owns (vblank_wait). The CD and pad overrides are platform-level SUPPLY — they provide what the hardware would have, then run the guest body. native_rand.cpp replaces 0x8006272C outright; under PSXPORT_NDIFF=200 every call matched the recompiled body in RAM, scratchpad, all 31 GPRs and hi/lo.

## What would falsify it

any new override that super-calls would keep the ratio honest — re-run the inventory rather than assuming it improved
