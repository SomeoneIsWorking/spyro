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
