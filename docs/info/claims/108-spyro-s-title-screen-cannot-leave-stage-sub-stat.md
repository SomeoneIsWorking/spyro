---
id: C108
kind: claim
status: falsified
created: 2026-07-29
tags: input,stage,overlay
falsified_on: 2026-07-29
---

## Claim

Spyro's title screen cannot leave stage sub-state 1 because the exit gate tests a global that nothing ever writes. The transition (sw s3=2 -> [0x80078D78], at 0x8007CC20 in OV_5B800) is guarded at 0x8007CBA0 by 'bne s0,v0 -> exit'. v0 is NOT runtime state: 0x8007CBA0 has exactly one predecessor, the branch at 0x8007CAA8, whose delay slot 'addiu v0,zero,5' always executes — so the gate is s0 == 5. s0 is loaded once at 0x8007C55C from s1 = 0x80078D7C and never rewritten, so the exit requires [0x80078D7C] == 5. The region is in fact unreachable rather than merely closed: 0x8007C564 is 'bnez s0' so s0 == 0 falls through and never reaches the gate.

## Evidence

REPL read at the title screen with START pressed: [0x80078D7C] = 0. PSXPORT_WWATCH=0x80078D7C,0x80078D80 with WWATCH_BT over a full run catches exactly two stores, both of ZERO — f0 (boot) and f436 from pc=0x80016914 ra=0x8002D198 a0=0x80078D78 a2=0x5C, a 0x5C-byte clear of the whole stage-state block. Residency verified per C065 before trusting the disassembly: whatis.py against a fresh title-screen RAM dump reports the arena matching OV_5B800 on 256/256 first words, with the resident word at 0x8007CBA0 being 0x16020029, the same bne as the image. The branch scanner used to find the single predecessor was validated first against 0x8007CC48, where it recovered all four branches the manual listing showed.

## What would falsify it

A run in which [0x80078D7C] is observed non-zero (would mean a writer exists on a path not yet reached, and the 'nothing ever writes it' conclusion is scoped to runs that stall at the title screen), or a second predecessor to 0x8007CBA0 appearing in a different resident overlay.

## FALSIFIED 2026-07-29

ITS OWN FALSIFIER FIRED, exactly as written ('a run in which [0x80078D7C] is observed non-zero would mean a writer exists on a path not yet reached'). Under PSXPORT_FORCE_BUTTONS=FFF7 the port DOES leave sub-state 0: sub becomes 1 at f835 and [0x80078D7C] is written 1 at f837, by pc=0x80068F44 from ra=0x8007B12C — inside the sub-state-1 arm. The static half of the claim survives and is re-recorded as C111: the gate is still s0 == 5 with s0 = [0x80078D7C], and that is still a static fact from the single predecessor's delay slot. What was over-scoped was 'nothing ever writes it' — that was measured only in runs which never left sub 0, because the REPL hold I was driving with cannot reach sub 1 (see C109 falsified / C110). Note the real writer is a store through a pointer inside a called function, so tools/writers.py could never have found it — its documented blind spot, demonstrated live.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
