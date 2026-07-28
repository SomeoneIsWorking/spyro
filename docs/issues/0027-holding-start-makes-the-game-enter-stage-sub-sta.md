---
id: 27
title: Holding START makes the game enter stage sub-state 1 and hang there — input works, the branch it takes does not complete
status: open
symptom: with PSXPORT_FORCE_BUTTONS=FFF7 the port makes exactly TWO stage transitions in 90s (mode 13 sub 0 -> sub 1) and loads NO level overlays; idle makes dozens and cycles four
tags: input,stage,blocker
created: 2026-07-28
updated: 2026-07-28
---

THIS IS PROGRESS, NOT A REGRESSION. It is the first evidence that the port's input reaches the game's own decision-making: two runs differing only in the button take visibly different branches, both rc=137 with zero recomp misses (C070). Idle: mode alternates 13/0, sub reaches 3, a level handler is installed at [0x800758CC], four level overlays cycle. START held: mode stays 13, sub reaches 1 — never seen idle — [0x800758CC] stays 0, and only the three boot overlays ever load.

WHAT SUB-STATE 1 RUNS. Mode 13 with sub != 3 dispatches to 0x8007ABAC in OV_5B800 (the boot overlay). Read from the CORRECT image (per C065 — reading it from any other overlay would show unrelated bytes), its head is:

  8007ABAC  lw   v0, [0x80078D78]        ; the sub-state
  8007ABCC  slti v0, v0, 2               ; sub < 2 ?
  8007ABD0  beq  -> 0x8007ABE8           ;   no  -> s4 = [0x80078D7C]
  8007ABDC       s4 = [0x80078D88]       ;   yes -> s4 = [0x80078D88]
  8007ABF4  a0 = [0x80075680]            ; a pointer to a frame counter
  8007AC04  v1 = [a0] + 2                ; counter advances 2 per call
  8007AC08  slti v0, v1, 1100            ; TIMEOUT threshold
  8007AC0C  beq  -> 0x8007AC7C           ;   counter >= 1100 -> take the other path
  8007AC38  slti v0, v0, 300             ; ARM threshold
  8007AC48  lw   v0, [0x80077380]        ; HELD buttons — read only past 300

So this is the title-screen timer: it arms input after ~300 counts and times out to the attract demo at ~1100. Holding START past the arm point moves sub to 1 and then nothing further happens for 90 seconds.

NEXT STEP. Read the sub=1 arm of 0x8007ABAC (from OV_5B800.BIN, not from RAM — the current build produces no miss, so the RAM dump is stale and whatis.py now says so, I015). The question is what sub=1 waits on: a CD load that never completes, a fade counter, or a memory-card probe. A single-press test was attempted and was mis-designed — PSXPORT_FORCE_STOP_AT=400 stops input after ~400 pad frames, which at ~165fps headless is about one second, i.e. while still booting. Re-test with the stop well after the 300-count arm point.

DO NOT read this as 'input is broken'. It is the opposite: the button is reaching the game's branch logic, and the branch is the thing to debug.
