---
id: C048
kind: claim
status: holds
created: 2026-07-28
tags: overlay
---

## Claim

TWO overlays now share the arena and the router tells them apart — the shared-slot architecture is proven end to end

## Evidence

OVL1 = WAD.WAD +0xB83800, 65536 bytes (index entry 11), recompiled to 9 functions and declared at the SAME base 0x8007AA38 as OVL0 in game/recomp_seeds.json. A run logs '[ovload] core ? slot 0 <- OVL0' and later '[ovload] core ? slot 0 <- OVL1' — psxport's content-signature keying distinguishes them at the one slot, exactly as C032/issue-0013 predicted it would. The base was OBSERVED, not inferred: the streaming load writes 65536 bytes to 0x8007AA38 and the previously-failing address 0x8008772C falls inside [0x8007AA38, 0x8008AA38). This closes the thread that began with 'overlays each have their own base' (C031, falsified) and ran through the wrongly-filed framework-redesign issue 0013.

## What would falsify it

An ovload line reporting '(none/unmatched)' for a dest of 0x8007AA38 after OVL1's load, which would mean the signature keying is not in fact distinguishing them.
