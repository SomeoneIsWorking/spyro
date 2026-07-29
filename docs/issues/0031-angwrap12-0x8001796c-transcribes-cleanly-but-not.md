---
id: 31
title: angwrap12: 0x8001796C transcribes cleanly but nothing headless ever calls it
status: open
symptom: 0x8001796C (12-bit signed angle wrap, 11 static callers) transcribes cleanly, but across a 4000-frame headless run PSXPORT_NDIFF records ZERO calls to it, so the transcription cannot be differentially verified.
tags: ownership,input,blocked
created: 2026-07-29
updated: 2026-07-29
---

NOT INSTALLED, deliberately. An override whose body has never been checked against the recompiled one is an unverified replacement of guest behaviour sitting on a live path; 'it obviously transcribes correctly' is exactly the confidence the differential exists to check, and this session already had spin60 diverge on call #1 in a register that 'could not matter'.

BLOCKED ON INPUT, not on analysis. All 11 callers sit on gameplay paths, and the port cannot yet drive gameplay headlessly — see issue 0027 (holding START enters a stage sub-state and hangs). Once a run can reach gameplay, install and verify in one step.

The transcription, from the disassembly:

    sub a0,a0,a1 ; andi a0,a0,0xFFF ; addi a1,a0,-2048 ; blez a1,L ; nop
    jr ra ; addi v0,a0,-4096        L: jr ra ; addi v0,a0,0

  d = (a0 - a1) & 0xFFF; v0 = (d <= 2048) ? d : d - 4096       // wraps into (-2048, 2048]

Two things to get right, both of which are silent when wrong:
  * The test is blez, NOT bltz — d == 2048 takes the 'keep' arm. Off by one here is invisible in
    almost every call and wrong at exactly the half-turn.
  * a1 EXITS AS d-2048, the comparison value. This branch's delay slot is a nop, so unlike the 8-bit
    helper at 0x80017908 (whose delay slot leaves a1 = 256 on BOTH paths) nothing overwrites it.
  * a0 exits as the masked difference d, not as the wrapped result.

4096 units to the turn is the PSX convention.
