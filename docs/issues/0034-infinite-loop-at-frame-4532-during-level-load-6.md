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

### Note (2026-07-29)
LOOP LOCATED EXACTLY, and the evidence now points at a REGISTER-PRESERVATION failure rather than at bad data.

fntrace call counts over one 40s run:
    0x8003385C   2244 calls          (per-frame, normal)
    0x8004A200    302 calls
    0x8004888C   64,043,023 calls    from ra=0x80048C0C
    0x8003DAE4   64,043,023 calls    from ra=0x80048B40

So the runaway is the loop at 0x80048C04:

    80048c04  jal   0x8004888C
    80048c08  addiu s0, s0, 1        ; DELAY SLOT — runs with the jal
    80048c0c  v0 = [0x800756CC]
    80048c18  slt   v0, s0, v0
    80048c1c  bne   v0, zero -> 0x80048c04

The counter is s0, compared against [0x800756CC], which is MEASURED = 2 at the stall (a watchpoint's last write to it is =2 at f4530, from pc=0x80056ED4). s0 is incremented every single iteration by the delay slot. Two iterations should end it. It runs 64 million.

s0 IS CALLEE-SAVED, so the loop only misbehaves if something in the call fails to preserve it. Checked: 0x8004888C's prologue saves ONLY ra (it is a 45-entry jump-table dispatcher on [0x80078AD0], table at 0x80011230), but the function it dispatches to here, 0x8003DAE4, does save s0/s1/s2 in its own prologue. ratan2 (0x80016AB4) uses only at/v0/v1/a0-a3. So on paper s0 survives, and in practice the loop says it does not.

NOT OUR NATIVE BODIES — PSXPORT_NO_NATIVE=1 reproduces the stall identically (last frame 4531 either way), so the substrate is doing this on its own.

LEADING HYPOTHESIS, stated as one: a RECOMPILER mistranslation somewhere in the dispatched path leaves s0 wrong across the call. That would be a framework bug of the most serious kind and must not be assumed on this reasoning alone.

CHEAPEST NEXT TEST, and it settles it without reading more code: log s0 immediately before and after the jal. An override on 0x8004888C can read c->r[16] on entry and again after super-calling, and report the first call where they differ — that names the offending path directly instead of inferring it. Do that before touching the recompiler.

### Note (2026-07-29)
REGISTER-PRESERVATION HYPOTHESIS REFUTED. Extended fntrace to snapshot the callee-saved registers (s0-s7, gp, sp, fp, ra) around every traced call and compare. Result: 0x8004888C and 0x8003DAE4 each report ZERO violations across 62,331,761 calls. Both honour the ABI, so nothing is clobbering the loop counter.

THE CHECKER WAS VALIDATED BEFORE THE RESULT WAS BELIEVED, via PSXPORT_FNTRACE_SELFTEST=1 which XORs s0 after the call: it then reports 'VIOLATES THE ABI: s0 entered as 8006FCF4, returned as 25A35951' on all 1856 calls of the control function. A '0 violations' reading is only worth anything once the checker has been made to say the other thing — a control case passing proves nothing, because a broken checker passes everything too.

SO THE CONTRADICTION IS SHARPER, NOT RESOLVED. All of the following are now measured, and they cannot all be true of a correctly executing loop:
  * the loop at 0x80048C04 counts s0 up and exits when s0 >= [0x800756CC]
  * s0 starts at 0 (set in the delay slot of the jal at 0x80048BE8)
  * s0 is incremented every iteration by the delay slot at 0x80048C08
  * [0x800756CC] measures 2 at the stall
  * s0 is preserved across the call (0 ABI violations)
  * the loop nevertheless runs ~62M times, ~206,000 per call of its caller

Something in that list is wrong, and the cheapest way to find out which is to stop reasoning about it and OBSERVE the loop directly: log s0 and [0x800756CC] as seen by the loop itself on each of the first few iterations. An override on 0x8004888C can print c->r[16] and c->mem_r32(0x800756CC) on entry; if s0 is not what the static reading predicts, the delay-slot translation is the thing to examine next, and if [0x800756CC] is not 2 there then the watchpoint reading was taken at the wrong moment.

### Note (2026-07-29)
STATE AT HANDOFF — a live contradiction, not a diagnosis. Measured this session:

    0x80012204     1 call
    0x8003385C  2244 calls
    0x8004A200   302 calls        (function spans 0x8004A200..0x8004A7E4)
    0x80048B9C   302 calls        (spans 0x80048B9C..0x80048D08)
    0x8004888C   62,331,761 calls
    0x8003DAE4   62,331,761 calls  (exactly equal — every dispatch goes here)

0x8004888C has exactly THREE callers (xrefs.py): 0x80048C04, 0x8004A8E4, 0x8004AAFC. The latter two live in function 0x8004A7EC, which fntrace reports NEVER CALLED. So the only live caller is the loop at 0x80048C04, inside 0x80048B9C — which runs 302 times, and whose loop does 2 iterations (register dump shows s0 alternating 1,2,1,2..., i.e. the loop completing correctly and being re-entered). 302 x 2 = 604 expected calls. 62 MILLION observed.

Those numbers cannot all be right. Candidates, none yet tested:
  * fntrace's own counting is wrong for this site — its trampoline clears the override, re-dispatches and restores, and 0x8004888C is a jump-table dispatcher, so a re-entrant path could be counted differently than assumed. VALIDATE THE COUNTER before trusting 62M.
  * 0x80048B9C is entered more often than fntrace reports (its own count could be under-reported for the same reason).
  * [0x800756CC] is larger than 2 at the stall.

THE LAST POINT IS UNMEASURED AND MY ATTEMPT TO MEASURE IT FAILED. A SIGUSR1 snapshot did not produce a file, and 'ls -t scratch/raw/snap_*.bin' silently returned snap_title.bin — a stale dump from the TITLE SCREEN earlier in the session. Any value read from it describes the wrong regime entirely. This issue has now been bitten by wrong-regime readings three times; when reading state at the stall, confirm the snapshot is NEW (check its mtime) before reading a single word out of it.
