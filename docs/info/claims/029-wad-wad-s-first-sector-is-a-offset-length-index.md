---
id: C029
kind: claim
status: holds
created: 2026-07-28
tags: recomp
---

## Claim

WAD.WAD's first sector is a (offset,length) index; 79 entries, 36 score as code

## Evidence

Parsed the archive's first sector as u32 pairs: entry 2 is (0x5B800, 0x3800) — EXACTLY the overlay already located and recompiled — and every load the running port made corresponds to an entry (0x800/0x1B000 = the 110592-byte load, 0x1B800/0x40000 = the 262144-byte load). 79 entries parse before the table terminates. Scoring each by common-opcode share (tools/wad_index.py, instrument I005): 36 clear 90%, with the known overlay highest at 99.5% against neighbours at 64.2% and 87.6%.

## What would falsify it

if an entry scoring >=90% turns out not to be loaded as code by any code path, the threshold admits data; and the 90% cutoff is a chosen value, so the count 36 is not evidence for the decomps' 37
