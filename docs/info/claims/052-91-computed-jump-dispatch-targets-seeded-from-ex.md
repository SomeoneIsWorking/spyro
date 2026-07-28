---
id: C052
kind: claim
status: falsified
created: 2026-07-28
tags: recomp
falsified_on: 2026-07-28
---

## Claim

91 computed-jump dispatch targets seeded from exactly-enumerated 3+ entry runs — no corruption, and the GTE region is passed

## Evidence

Enumerated off the instruction stream, not heuristically: a run of  at stride 8 is self-terminating, so each run's bounds are read rather than guessed. 14 runs of >=3 entries (85 addresses) outside the suspected-data range 0x8006C000-0x80073000, plus 6 hand-verified 16-byte  case blocks — a second idiom in the same region. The delay slot is NOT always nop and often carries the per-case work (0x8004C830 is 'j 0x8004C838 ; addi s1,t4,0'); a nop-only detector missed exactly those and cost a build+run cycle. Discovery goes 246 -> 331 seeds and 667 -> 769 functions. The fail-fast walked forward through the whole GTE dispatch region 0x8004C4EC -> 0x8004C650 -> 0x8004C830 and out of it entirely to 0x80062960, with unmapped-RAM reads at ZERO throughout.

## What would falsify it

Any unmapped-RAM read appearing with this seed set, or a fail-fast returning to an address inside 0x8004Cxxx.

## FALSIFIED 2026-07-28

I filed this after watching the fail-fast walk forward through the GTE region and seeing unmapped-RAM stay at zero, and called it 'no corruption'. I never measured FRAMES for that seed set. With all 91 dispatch targets seeded the port produces ZERO frames — it dies during ResetGraph instead of running 3931. So the seeding was strictly WORSE than the six-seed state, and 'the fail-fast moved further along' measured code progress while the port regressed to dying at boot. The absence of one bad signal (unmapped reads) is not the presence of health, and I had a frame count available the whole time. Reverted to the six hand-verified seeds: 3931 frames, 3,686,400 bytes, overlay identified, one known fail-fast at 0x80062960. C051 (seeding a mid-function target as a function splits the body and corrupts, even when the address is right) still holds and is the reason.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
