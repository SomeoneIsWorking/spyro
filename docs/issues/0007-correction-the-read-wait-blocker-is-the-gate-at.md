---
id: 7
title: CORRECTION: the read-wait blocker is the gate at 0x80076BB8, not the CD status bit
status: resolved
symptom: Boot spins after ReadN. In-process instrumentation shows gate=[0x80076BB8]=1 on every iteration, so func_80016500's wait takes the retry path unconditionally and never reaches its CdSync or status tests.
tags: cd,boot,correction
created: 2026-07-28
updated: 2026-07-29
---

## This corrects two earlier conclusions of mine

**Wrong #1 — 'the missing CD status bit 0x40 has no producer' (issue 0005).** It is not missing.
In-process logging shows `[0x800774B4] = 0x40` on every single iteration, from the very first. The
status bit was already set the whole time. The elaborate producer chain decoded in issue 0006
(pending-event 8|9 -> service routine sets 0x40) is real code, but it is NOT what the boot is
waiting on.

**Wrong #2 — 'func_800163E4 is spinning and never returns'.** It enters and exits cleanly every
iteration, taking its early-return path because `[0x800758E0] == 0`. It does nothing at all. A
5-sample stack profile showed it on top simply because it is called first and often in a hot retry
loop — frequency of appearance is not the same as failing to return.

## What is actually true

The wait loop's FIRST test is the gate:

    if ([0x80076BB8] != 0) -> retry     // gate == 1, always taken
    if (CdSync(1,0) != 2)  -> retry     // never reached
    if ([0x800774B4] & 0x40) -> SUCCESS // never reached (and would pass)

So two of the three conditions were already satisfied and I was working on the third, which was
also already satisfied. The gate is the only thing holding the boot.

## Gate writers (static scan)

    WRITE @0x800164A8 in func_80016490
    WRITE @0x80016628 in func_80016500
    WRITE @0x80016754 in func_80016698

Next: determine what the gate MEANS (most likely 'a CD request is in flight') and which of those
writers is supposed to clear it on completion — that writer is the thing that never runs.

## Method note

Both wrong conclusions came from reading STATIC code plus stack samples. Both were corrected in one
step by logging the actual guest words from inside the port. For guest-state questions, instrument
in-process first; static decode is for understanding what the words mean once you know which one is
wrong.

### Resolution (2026-07-29)
Correct at the time and now moot. The gate [0x80076BB8]=1 observation was accurate — the loader sets it to 1 before issuing the read and the completion callback clears it, so it pins at 1 exactly when no data is coming. That is a symptom of the read never delivering, not an independent blocker: with the game-level loaders owned (C106) the bytes are present at issue time, completion is delivered truthfully, and the gate clears. Boot proceeds to gameplay; 138 CD completions per 40s gate. Gate 14/14.
