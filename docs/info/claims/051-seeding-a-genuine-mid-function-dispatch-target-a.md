---
id: C051
kind: claim
status: holds
created: 2026-07-28
tags: recomp,method
---

## Claim

Seeding a genuine mid-function dispatch target as a FUNCTION corrupts the recomp — even when the address is provably correct

## Evidence

0x80062960 is a real computed-jump target: a [recomp-MISS] fired on it, and it decodes as 'j 0x80062860 ; ori s3,s3,0x20', part of a 2-entry stride-8 run. Seeding it (plus its partner 0x80062958) in main+main_reentry REMOVED the fail-fast — and produced 9,418,886 UNMAPPED RAM reads at wild addresses like 0xBEA98F18. Every earlier seeding stage logged ZERO such reads, so the seeds caused it. Removing just those two restores unmapped=0 with the fail-fast back. The mechanism: seeding a mid-function address as a FUNCTION makes emit split the enclosing body there, so the preceding code's register/stack state no longer reaches the split point. The address being right does not make the split safe. I first blamed a threshold of 2 sweeping in coincidental  pairs; that was wrong — threshold 3 plus these two verified addresses corrupts just the same. The correct fix is an emit.py recogniser that emits a LABEL inside the existing function, which is what find_jump_tables does for the table idiom.

## What would falsify it

A build where those two addresses are seeded and the unmapped-RAM count is zero.
