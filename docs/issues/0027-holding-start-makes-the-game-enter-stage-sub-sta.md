---
id: 27
title: Holding START makes the game enter stage sub-state 1 and hang there — input works, the branch it takes does not complete
status: open
symptom: with PSXPORT_FORCE_BUTTONS=FFF7 the port makes exactly TWO stage transitions in 90s (mode 13 sub 0 -> sub 1) and loads NO level overlays; idle makes dozens and cycles four
tags: input,stage,blocker
created: 2026-07-28
updated: 2026-07-29
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

### Note (2026-07-29)
STILL OPEN DELIBERATELY — the gate does not press buttons. The headless gate run supplies no input at all, so it can never enter the sub-state this describes and its passing says nothing about this issue. Reproducing it needs a run that holds START (PSXPORT_REPL=1 'press start'). Flagged by 'catalog.py stale' only because it is tagged blocker; that flag is correct to raise and the answer is 'uncovered', not 'fixed'.

### Note (2026-07-29)
GATE IDENTIFIED, and half of it did NOT need a runtime probe. The previous note said 'identifying s0 and v0 at that comparison is the next concrete step, and it is a runtime question (both are loaded earlier in the function)'. That is true of s0 but WRONG for v0: 0x8007CBA0 is reached by exactly one branch, 'bne $s0,$v0 -> 0x8007CBA0' at 0x8007CAA8, whose DELAY SLOT is 'addiu $v0, $zero, 5'. The delay slot always executes, so control arrives at the gate with v0 = 5 unconditionally. The gate is simply:  s0 == 5.

AND s0 IS A GLOBAL, not a computed value. Walking the single predecessor chain back: 0x8007C554 sets s1 = lui 0x8008 / addiu -0x7284 = 0x80078D7C, and 0x8007C55C does 'lw $s0, ($s1)'. Nothing rewrites s0 between there and the gate. So the exit from sub=1 requires:

    [0x80078D7C] == 5

MEASURED, AND IT IS NEVER 5. REPL read at the title screen with START pressed: [0x80078D7C] = 0. A write-watchpoint over the whole run (PSXPORT_WWATCH=0x80078D7C,0x80078D80 PSXPORT_WWATCH_BT=1) catches exactly TWO stores, both writing ZERO: one at f0 (boot) and one at f436 from pc=0x80016914 ra=0x8002D198 with a0=0x80078D78 a2=0x5C — a 0x5C-byte block clear of the whole stage-state area. Nothing in a full run ever writes it a non-zero value.

That also means the gate region is not merely closed, it is UNREACHABLE: 0x8007C564 is 'bnez $s0 -> 0x8007C674', so with s0 == 0 control falls through and never reaches 0x8007C8B4 -> 0x8007CAA8 -> 0x8007CBA0 at all. Chasing 'why does the branch at 0x8007CBA0 go the wrong way' would have been chasing code that does not execute.

NOT A SUSPECT: the 0x5C block clear runs through 0x80016914, which this port owns natively (fill), but that body is per-call differentially verified against the recompiled one (0 divergences, 64 calls) — it is faithfully reproducing a clear the guest itself performs.

RESIDENCY VERIFIED before trusting any of the above, per C065: whatis.py against a fresh title-screen RAM dump reports the arena matching OV_5B800 on 256/256 first words, and the resident word at 0x8007CBA0 is 0x16020029 — the same bne read from the image.

NEXT: the question is now 'what is [0x80078D7C], and what should write it 5?'. It sits inside the 0x5C-byte stage-state block at 0x80078D78 that is cleared as a unit, alongside the sub-state itself at 0x80078D78. Find its writer in the IMAGE (a store through a register, so use the numeric branch/store scan or a watchpoint on a run that gets further, not a lui/addiu immediate scan — two such scans already missed the sub-state's writer for the same reason).

### Note (2026-07-29)
THE WRITER OF 5 IS FOUND, AND THE FORCE_BUTTONS TESTS WERE STRUCTURALLY UNABLE TO WORK.

[0x80078D7C] has NINETEEN immediate-form writers (tools/writers.py, new). Exactly ONE stores 5: 0x8007B8F8 in OV_5B800, and the instruction pair just before it stores 2 to [0x80078D78] — so that single block sets BOTH the sub-state and the gate global. Enumerating writers was useless on its own; what made it answerable was printing each store's constant, which is why writers.py does.

The block (0x8007B858..0x8007B8F8) needs FOUR things, all read from the resident image:
  1. [0x80078D84] >= 8            (slti 8 / bnez -> exit)
  2. func_80032AB0() == 0         (bnez v0 -> exit)
  3. [0x80077378] & 0x840 != 0    (START bit 11 or X bit 6)
  4. [0x80078D8C] == 0            (bnez -> 0x8007B914)

WHY EVERY PREVIOUS BUTTON TEST FAILED. Condition 3 reads 0x80077378, which is the EDGE ('newly pressed') word — NOT the held word at 0x80077380 that earlier notes were reading. Both are written every frame by pc=0x8006B64C ra=0x80053D50. A watchpoint proves the distinction: with the REPL's 'press start' the edge word goes 0x00000800 for exactly two frames (f1301-f1302) and is 0 on every other frame of a 241180-store run, while the held word sits at 0xFFFF0800 continuously. PSXPORT_FORCE_BUTTONS holds a button from boot, so it produces NO EDGE — which is why FFF7 and BFF7 both gave identical do-nothing traces. That was never a statement about what the menu wants.

ALSO: func_80032AB0 is itself an input handler — it tests edge bit 0x10 and, when set, writes sub-state = 1 and clears [0x80078D88]. So condition 2 only fails when that other button is pressed. START alone passes it.

STILL UNEXPLAINED, and this is the next step. Pressing START 14 times at 40-frame intervals from f1400, with [0x80078D84] climbing monotonically 3 -> 23 -> 43 -> ... -> 263 (so condition 1 holds from the second press onward), [0x80078D8C] == 0 throughout, and a real edge on each press, NEVER moves [0x80078D78] off 0. All four conditions look satisfiable yet the store never happens, so the likeliest explanation is that this block is NOT DISPATCHED in the state the port is actually in — i.e. 0x8007B858 belongs to a different arm of the handler than the one running. Settle that before analysing the conditions further: put a probe/override on the overlay function, or watch [0x80078D88]/the dispatch selector, rather than reading more code. Do not assume the block runs.

### Note (2026-07-29)
CORRECTION TO MY OWN PREVIOUS NOTE, and it inverted the conclusion. I wrote that PSXPORT_FORCE_BUTTONS 'holds a button from boot, so it produces NO EDGE', and therefore that every earlier FORCE_BUTTONS result was unfounded. That is wrong. I inferred it from the option's NAME rather than reading pad_input.cpp. Pad::serviceFrame PULSES it:

    setButtons((mFc % 32u) < 8u ? mForceMask : PAD_NONE);   // 8 frames down, 24 up

and the comment there states the reason: 'so each press is a fresh EDGE the game's current&~prev input logic actually sees — a continuous hold would edge only once.' So FORCE_BUTTONS is the STRONGER menu-driving instrument and the REPL's 'press' is the weaker one — 'press' is a persistent HOLD (held &= ~bit; driveHold), giving exactly one edge ever. I had the two exactly backwards, and it cost me a false negative: 40 REPL 'presses' looked like 40 attempts but were one hold plus 39 no-ops, and never reached sub 1.

MEASURED BOTH WAYS ON THE CURRENT BUILD:
  * FORCE_BUTTONS=FFF7      -> sub 0 -> 1 at f835 (pc=0x8002BFE0 ra=0x8007AD04), reproducing the
                               original observation exactly.
  * REPL 'press start' at f0 (reports the identical held=FFF7) -> sub NEVER leaves 0 through 1500
                               frames, watchpoint-confirmed.
Two mechanisms that report the same held mask, different guest behaviour. Use FORCE_BUTTONS to drive menus; do not treat a REPL hold as equivalent.

WHERE THAT LEAVES THE STATE MACHINE. The handler dispatches on the sub-state at 0x8007AD28-0x8007AD5C: sub 0 -> 0x8007AD64, sub 1 -> 0x8007B0B8, sub 2 -> 0x8007C454, else exit. So the block at 0x8007B858 that sets sub=2 and [0x80078D7C]=5 belongs to the SUB-1 ARM — which answers the previous note's open question: it was never dispatched in the runs I was doing, because those runs never left sub 0.

Under FORCE_BUTTONS the sub-1 arm DOES run: [0x80078D7C] is written 1 at f837 by pc=0x80068F44 from ra=0x8007B12C. But across 4000 frames it is never written 5, so 0x8007B8F8 still does not execute. THAT is now the precise open question — with the sub-1 arm confirmed running and edges arriving every 32 frames, which of the block's four conditions ([0x80078D84] >= 8, func_80032AB0()==0, edge & 0x840, [0x80078D8C]==0) is false? Measure them under FORCE_BUTTONS at a frame after 837; do not reuse readings taken in sub 0, which is the mistake this note is correcting.

Also worth noting: the writer at 0x80068F44 is a store through a pointer inside a called function, so tools/writers.py could not have found it. Its documented blind spot, demonstrated live.
