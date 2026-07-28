---
id: C015
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

Fixing the completion latch unblocked the frame loop: 8 -> 218 frames, but content does not progress

## Evidence

Re-arming the CD completion at the READ ISSUE point (instead of watching the gate reach 0, which latches because the guest re-issues before the next sample) raised delivered completions 1 -> 5 and frames 8 -> 218 in the same 30s window. Frame content: sampled frames 10/80/150 are byte-identical in occupancy (2.9% nonzero, the held splash) and frame 217 is blank (0.0%) — so the splash is held then blanks. Distinct LBAs sought remains 1 (LBA 37): no asset data loads.

## What would falsify it

if a later run shows frames progressing visually while still only ever seeking LBA 37, then content progression does not depend on the read advancing and this reasoning is wrong
