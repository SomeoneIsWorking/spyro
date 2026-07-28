---
id: C066
kind: claim
status: holds
created: 2026-07-28
tags: overlay,recomp,seeds
---

## Claim

Every level overlay has exactly ONE per-frame entry point, installed by the overlay into [0x80075734] and called indirectly from main's stage tick at 0x80033AA4 — invisible to jal discovery, so each needs an explicit overlay seed.

## Evidence

0x80033AA4 is 'lw v0,[0x80075734] ; jalr v0'. Two confirmed so far, each verified against the RAM dumped at its own miss: OV_237D000 0x8007AEB8 (addiu sp,sp,-512) and OV_2F5B000 0x8007B7A8 (addiu sp,sp,-560, resident word matching the slice exactly). Both surfaced as a fail-fast with caller ra=0x80033AAC, the instruction after that jalr. Seeding them takes the port from an abort to rc=137 with zero misses.

## What would falsify it

an overlay that needs more than one seed, or a miss with caller ra=0x80033AAC whose address is NOT a prologue in the resident bytes — either would mean the entry is not a simple per-overlay function pointer
