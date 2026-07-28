---
id: C031
kind: claim
status: holds
created: 2026-07-28
tags: recomp
---

## Claim

Overlay load bases are statically readable from resident data — no gameplay needed

## Evidence

At the loader call site 0x80012924, a1 (dest) is not computed but LOADED: lui r5,0x8001 / lw r5,0x13A0(r5), i.e. from [0x800113A0]. Reading that word straight out of SCUS_942.28 gives 0x8007AA38 — exactly the OVL0 base observed from a running port, confirming the route end to end. Likewise a0 (base LBA) comes from [0x80076B90] and a3 (offset) from [0x8007A6E8]. The loader has 8 static call sites (func_8001250C, func_800127C0 x4, func_800144C8, func_8002D228, func_8002E12C, func_8002EDF0, func_800334D4, func_8005B7D8), so their dest sources can be read the same way without exercising the paths.

## What would falsify it

if another call site's dest source holds 0 or a non-RAM value in the static image, that site computes its dest at runtime and static reading does not generalise
