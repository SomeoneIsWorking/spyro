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

### Note (2026-07-28)
CORRECTION — 'hangs' was WRONG, and the way it was wrong is the lesson. I inferred a hang from the ABSENCE of stage transitions (two in ninety seconds). A write-watchpoint on the stage globals shows the opposite: at sub=1 the game is continuously active. [0x80078D84] is a counter incrementing every 2 frames (…0x730D, 0x730E, 0x730F…, reaching 0x7312+), written from pc=0x80058CC0 with ra=0x8007CC50, and [0x80078D80] cycles through 0/0xC/1. Sub-state 1 is a LIVE ANIMATING STATE — a title screen or menu doing its idle animation while waiting for input. Absence of state-machine transitions is not absence of progress, and counting transitions was the wrong instrument for the question 'is it stuck'.

WHAT THE WATCHPOINT ALSO SETTLED, in one run, after two static scans had failed to: sub=1 is written exactly ONCE, at frame 835, by func_8002BFE0 called from ra=0x8007AD04 — inside the title handler 0x8007ABAC — and never again. Both earlier static scans for writers of [0x80078D78] missed it because the store goes through a register (s0=0x80078D78, s1=1), not a lui/addiu pair. PSXPORT_WWATCH=<lo>,<hi> (plus PSXPORT_WWATCH_BT=1) is the right instrument for 'who wrote this address' and should be reached for before a third static scan.

TITLE-SCREEN SEMANTICS, now read from OV_5B800 (the correct image):
  * the counter at [[0x80075680]] advances 2 per call; past ~300 input is armed, at ~1100 it takes the timeout path, and it cycles 1172..1530 as the idle animation
  * held buttons are masked with 0x0840 in the game's INTERNAL encoding (s0 = ~((byte2<<8)|byte3)), i.e. bit 11 = START and bit 6 = X/cross; either sets the counter to 1170
  * 0x8006272C is rand() — the LCG seed*0x41C64E6D + 12345, returning (seed>>16)&0x7FFF — so the branch it feeds is the random idle-animation choice, NOT a status check to satisfy. An earlier reading of that branch as a possible blocker was wrong.

NEXT: the question is no longer 'why is it stuck' but 'what input does this menu want'. Try START then X/cross (standard active-low masks 0xFFF7 and 0xBFFF; combined 0xBFF7), and watch [0x80078D78] for a transition past 1.

### Note (2026-07-28)
EXIT PATH LOCATED, still gated. The transition out of sub=1 is at 0x8007CC20 (sw s3=2 -> [0x80078D78]) in OV_5B800, and the code leading to it reads:

  8007CBA0  bne  s0, v0 -> 0x8007CC48     ; *** THE GATE — jumps PAST the transition ***
  8007CBAC  v0 = [0x80078D84]             ; the counter that IS advancing (C071)
  8007CBB4  bne  v0, zero -> 0x8007CBC4   ; counter==0 -> jal 0x8007AAD4(a0=5)
  8007CBD0  v0 = [0x80078D84] + 1
  8007CBD8  sw   v0, [0x80075918]         ; clamped to 15 just below
  8007CBF4  slti v0, v1, 16
  8007CBF8  bne  v0, zero -> 0x8007CC48   ; counter < 16 -> skip the transition
  8007CC00  jal  0x8006631C
  8007CC20  sw   s3, [0x80078D78]         ; sub = 2

The counter test is NOT what blocks it: [0x80078D84] is observed at 0x730D+ (29453), far above 16. So control never reaches 0x8007CBF8 at all — the branch at 0x8007CBA0 takes it to 0x8007CC48 first. Identifying s0 and v0 at that comparison is the next concrete step, and it is a runtime question (both are loaded earlier in the function), so use PSXPORT_WWATCH / a probe rather than another static read.

ALSO RULED OUT this round: neither START alone (0xFFF7) nor START+X (0xBFF7) advances past sub=1 — both give an identical trace, sub reaching 1 at frame 835 and staying, with only the three boot overlays loaded. So the menu is not simply waiting for one of those two buttons.
