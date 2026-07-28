---
id: C054
kind: claim
status: holds
created: 2026-07-28
tags: recomp,framework
---

## Claim

The computed-offset jump idiom is now handled by a recompiler RECOGNISER — six function-splitting seeds replaced by zero

## Evidence

Added _scan_computed_offset to emit.py find_jump_tables as a THIRD idiom, running LAST so it can only fire where both table idioms already found nothing — the same strict-first discipline those two use on each other. It recognises the shape: lui/addiu builds an immediate base, sll scales the index, add combines them, jr dispatches. It emits case LABELS inside the existing function, which is what makes it correct where seeding was not (C051: a seed SPLITS the body and breaks the register/stack state reaching the split point). The case COUNT is not guessed: the index is assigned CONSTANTS by a branch cascade dominating the jr, so the reachable set is exactly those constants — the first dispatcher assigns 0, 1 and 2 at three sites, giving exactly three targets. It emits NOTHING when constants cannot be recovered. Result: 8 dispatchers recovered in one function alone, and with the seed file back to ONE entry the fail-fast reaches 0x8004C650 — the same point six seeds had reached, now with NO seeds, no corruption (unmapped-RAM zero), and the gate otherwise unchanged: 3931 frames, 3686400 bytes, overlay identified.

## What would falsify it

Any unmapped-RAM read appearing, a previously-recovered table switch changing, or a recovered target falling outside its enclosing function.
