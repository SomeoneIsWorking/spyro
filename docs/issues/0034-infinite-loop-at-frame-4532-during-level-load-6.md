---
id: 34
title: Infinite loop at frame 4532 during level load: 6.3M angle-global writes, no frame ever presented
status: open
symptom: The port stops presenting after frame 4531 and never recovers, while the process stays alive. A write-watchpoint over 0x80078B00-0x80078B80 catches 6,378,459 stores in 45 seconds, ALL stamped frame 4532, all from pc=0x80016AB4 with ra=0x8003DC20. The two globals oscillate between an unwrapped and a wrapped 12-bit angle: [0x80078B7C] alternates 0x00000FBE <-> 0xFFFFFFBE and [0x80078B78] alternates 0x00000FFC <-> 0xFFFFFFFC (4030-4096 = -66; 4092-4096 = -4).
tags: stall,level-load,angle,blocker
created: 2026-07-29
updated: 2026-07-29
---

REACHABLE ONLY SINCE ISSUE 0027 WAS FIXED — before that the port never left the title screen. This is what replaced issue 0033's supposed 'throughput regression': the port is not slower, it advances into level loading and hangs here.

CALL CHAIN, from the watchdog (the gate disables it, so it never surfaced there):
  main -> 0x80012204 -> 0x8003385C -> 0x8004A200 -> 0x80048B9C -> 0x8004888C -> 0x8003DAE4

THE LOOP BODY around the second call:
    8003dbf0  jal 0x80017A38            ; isqrt (owned natively)
    8003dc00  jal 0x80016AB4  (a2=1)
    8003dc18  jal 0x80016AB4  (a2=1)    ; ra=0x8003DC20 — the watched writer
    8003dc20  v1 = [sp+32]
    8003dc28  a2 = [0x80078B7C]
    8003dc34  v1 = v1 - a2
    8003dc38  v1 &= 0xFFF
    8003dc3c  slti v0, v1, 2049          ; the 12-bit shortest-arc test

So this is angle convergence: something is being rotated toward a target and the wrap test never settles. 0x80016AB4 is a function entry (its own prologue) and is what writes the two globals.

DO NOT ASSUME A CONNECTION, but note it: 0x8001796C is the 12-bit signed angle wrap transcribed in issue 0031 and deliberately NOT installed because nothing ever called it. This loop performs that operation INLINE at 0x8003DC34-3C. They may be the same routine open-coded, or unrelated code doing the same arithmetic. Establish which before acting.

FIRST STEPS: (1) disassemble 0x80016AB4 to see what it computes and why it writes both globals; (2) find the loop's back-edge with tools/xrefs.py to see the exit condition it never satisfies; (3) check whether an owned native body feeds it — 0x80017A38 (isqrt) is called immediately before and IS owned, so verify it with PSXPORT_NDIFF over this exact frame rather than trusting the earlier 64-call verification, which covered the title screen only.

### Note (2026-07-29)
NOT OUR NATIVE BODIES — measured, not argued. Added PSXPORT_NO_NATIVE=1 (a new A/B switch, I028-adjacent) which installs none of the 18 natively-owned bodies so every call runs the recompiled substrate. The stall reproduces IDENTICALLY: last frame 4531 with native bodies, 4531 without. So isqrt and the angle-table bodies called around this loop are exonerated, and the loop lives in the recompiled substrate or in the state feeding it.

This mattered enough to build a switch for because the per-call differential could not answer it. PSXPORT_NDIFF verifies the FIRST N calls of each site; a body that misbehaves only after millions of calls, on inputs a later level produces and the title screen never does, is invisible to it. 'Verified 64 calls at the title screen' is not evidence about frame 4532, and treating it as such would have been the hollow-green trap the project rules warn about.

WHERE THAT LEAVES IT. The game plainly works on hardware, so a faithful reproduction of this loop would still terminate — which points at wrong STATE feeding it rather than a wrong loop. Candidates, in order of cheapness: (1) the angle/position globals it converges on being seeded from a bad source; (2) one of the FIVE newly-added overlays (issue 0032) being mis-based or mis-recompiled, since this code path only became reachable alongside them; (3) a genuine recompiler mistranslation in 0x8003DAE4 or 0x80016AB4.

Note (2) deserves suspicion precisely because it is new: the overlays were added by the same change that first made this path reachable, so 'it only breaks here' does not distinguish 'new code' from 'new bug'.

### Note (2026-07-29)
WHAT THE LOOP IS DOING, and one hypothesis raised then weakened.

0x80016AB4 is ratan2. Its body takes |a0| and |a1|, normalises them using the GTE's LEADING-ZERO COUNT (MTC2 v1,LZCS at reg 30; MFC2 v1,LZCR at reg 31; then 17 - clz and srav on both operands), ensures the larger is the dividend, divides, and then selects a quadrant from the signs of a0/a1. The caller at 0x8003DBF0 does isqrt (0x80017A38) then two ratan2 calls, then the 12-bit shortest-arc test (v1 - [0x80078B7C]) & 0xFFF vs 2049. So the whole loop is angle/rotation convergence.

HYPOTHESIS RAISED: if the GTE's LZCS/LZCR were unimplemented, the normalising shift would be wrong, the ratio wrong, and the returned angle wrong — which would explain a convergence that never settles. WEAKENED on inspection: the Beetle GTE this port uses does implement it, computing LZCR on every write to DR[30] as MDFN_lzcount32(value ^ (0u - (value >> 31))) (gte.c:663), which is the correct sign-aware form. So this is NOT obviously broken and should not be assumed to be. It is still worth CONFIRMING end to end rather than by code reading, because a correct implementation reached through a wrong recompiler translation of MTC2/MFC2 to regs 30/31 would look identical from here.

ADDRESS ARITHMETIC ALREADY RULES OUT THE OVERLAYS as the direct site: 0x8003DAE4, 0x80016AB4 and the globals 0x80078B08/B74/B78/B7C are all BELOW the arena base 0x8007AA38, so neither the looping code nor its state lives in overlay space. The five new overlays can still be a source of bad input data, but they are not where the loop runs.

NOTE ON THE OSCILLATING VALUES: 0x0FFC is 4092 and (4092 - 4096) = -4, i.e. an angle four units from the target out of 4096 — essentially converged. The pair being written alternately looks like the raw 12-bit value and its sign-extended form, so the two globals may simply be a (wrapped, unwrapped) pair rather than evidence of thrashing. Confirm what 0x80016AB4 stores where before reading the oscillation as a symptom.

### Note (2026-07-29)
TWO OF MY OWN READINGS CORRECTED, and the entity-count hypothesis refuted.

1. THE 'OSCILLATION' IS BY DESIGN, not a symptom. 0x8003DCE0-0x8003DD28 writes each global TWICE per pass on purpose: first the raw 0..4095 value, then — only if it exceeds 2048 — the signed form (value - 4096). That is the standard shortest-arc wrap written out longhand. So 0x0FFC followed by 0xFFFFFFFC is one correct computation, not thrashing, and 6.3M stores means roughly 1.6M passes at 4 stores each. I presented the alternation as evidence of a problem; it is evidence of nothing.

2. THE STORES ARE NOT IN ratan2. The watchpoint said pc=0x80016AB4, but writers.py finds no store to these addresses inside that function — the real sites are in the CALLER at 0x8003DCF0/DD04/DD14/DD28, right after two ratan2 calls. Cause: wwatch's pc is the last function ENTERED and is stale after a call returns (now recorded as an instrument caveat). The ra it reported, 0x8003DC20, was right all along.

3. ENTITY COUNT REFUTED. 0x80048B9C's two loops both iterate [0x800756CC] times, which looked like the runaway. Measured: [0x800756CC] = 2, written repeatedly by pc=0x80056ED4 ra=0x80012238. Two iterations, not millions. Neither 0x8003DAE4 nor 0x8004888C contains a backward branch either, so nothing in this part of the chain loops.

WHERE THE REPETITION MUST COME FROM. If the inner chain runs a bounded number of times per pass, then an OUTER loop is re-entering it without ever completing a frame — the frame counter is pinned at 4532 while work continues. That puts the loop at 0x8003385C / 0x80012204 / main, above everything examined so far. 0x8003385C is already instrumented (it is one of the level_load_probe sites), which is the cheapest place to start.

This is the same SHAPE as issue 0027: not a runaway computation, but a wait for something that never arrives, with the per-pass work being ordinary. Look for what the outer loop is polling rather than for a counter that is too large.
