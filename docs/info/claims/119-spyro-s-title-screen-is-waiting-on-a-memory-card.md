---
id: C119
kind: claim
status: holds
created: 2026-07-29
tags: input,memcard,event
---

## Claim

Spyro's title screen is waiting on a MEMORY-CARD event the port never delivers. Full chain, every link measured: the exit gate needs [0x80078D7C]==5; the live sub-1 case calls 0x80067628 which returns 0 while [0x80075B58]==0; that flag's only setter is 0x80067CD4 (now delivered, C118) but it bails because 0x80069030 returns 0 twice; 0x80069030 returns [0x800751B0]>>31 and that index is stuck at 1; index 1 selects handler 0x800663D8, whose state [0x80075C18] is stuck at 11; state 11 calls 0x8006841C which returns [0x80075B2C] + ([0x80075B30]<<1) + ([0x80075B34]<<2) + ([0x80075B38]<<3), and all four are 0; the only function that sets those four to 1 is 0x80067DD0, which is NEVER CALLED. 0x80067DD0 has no static callers and one reference: 0x80067EA0 registers it via OpenEvent(0xF4000001, 4, 0x1000, 0x80067DD0) — class 0xF4000001 is HwCARD, spec 4 is I/O-end, mode 0x1000 is EvMdINTR. So it is the memory-card I/O-completion callback.

## Evidence

PSXPORT_FNTRACE with FORCE_BUTTONS over 2000-3000 frames: 0x800663D8 253157 calls, 0x8006841C 253156 calls, 0x80067CD4 270006 calls (after C118) — all running; 0x80067DD0 NEVER CALLED. REPL reads with FORCE_BUTTONS: [0x800751B0]=1 stuck across f900/1300/1900, [0x80075C18]=11 stuck across f900/1700, all four of [0x80075B2C/30/34/38]=0, handler table [0x80075C4C]=0x800663D8. tools/writers.py gives exactly three immediate-form writers per flag, with the sole '=1' setter in each case inside 0x80067DD0. Its single reference is the lui/addiu pair at 0x80067EC0 feeding a3 into 'jal 0x8005DB74' with a0=0xF4000001, a1=4, a2=0x1000; 0x80067EA0 itself runs once, at frame 835.

## What would falsify it

0x8005DB74 turning out not to be OpenEvent once its dispatch is read, or 0x80067DD0 being observed called without any memory-card event being delivered.
