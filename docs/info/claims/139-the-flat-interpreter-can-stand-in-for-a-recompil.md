---
id: C139
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen,ownership,interp
---

## Claim

The flat interpreter can stand in for a recompiled renderer BIT-EXACTLY: all four active widescreen-queue renderers (0x80022A2C, 0x8001F798, 0x80020F34, 0x800258F0) match their recompiled bodies on every verified call when run via interp_call. Their clip bounds are immediates in GUEST RAM on that path, not baked C literals — so widening a bound is a one-word write rather than a byte-exact transcription of ~9150 instructions.

## Evidence

scratch/logs/interp4.log — one capture with PSXPORT_INTERP_FN=80022A2C,8001F798,80020F34,800258F0 and PSXPORT_NDIFF=3. Each reports 'call #1..#3 matches the recompiled body exactly'; ndiff compares 2 MB of guest RAM, the scratchpad, every GPR and the whole COP2 register file. Needed one framework addition (interp_call): interp_run poisons r[31] with CORO_SENTINEL, and these bodies SPILL ra to a fixed save area, so the sentinel rather than the return address landed in guest RAM.

## What would falsify it

a renderer whose interpreted run diverges on a path this capture never took — three calls each is coverage of the exercised path, not of the function
