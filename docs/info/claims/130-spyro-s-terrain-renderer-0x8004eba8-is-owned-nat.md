---
id: C130
kind: claim
status: holds
created: 2026-07-30
tags: render,ownership,milestone
---

## Claim

Spyro's terrain renderer 0x8004EBA8 is OWNED natively and byte-exact: 400/400 consecutive calls match the recompiled body with zero divergences.

## Evidence

game/core/native_terrain.cpp, installed unconditionally. PSXPORT_NDIFF=400 over a 130s run: 400 'matches the recompiled body exactly', 0 DIVERGES. ndiff compares RAM, the scratchpad, all 31 GPRs and the COP2/GTE register file. Gate 16/16 with 'native bodies verified 168' and 0 divergences.

## What would falsify it

any divergence on a later call or in a different scene. Specifically UNVERIFIED: the packet-pool-exhausted arm (0x8004EF68) never fired in a verified run, so its exit state — including the delay-slot t6 = t4>>20 — is transcribed but unexercised; that call is where a divergence would surface if the pool ever runs out with ndiff budget left.
