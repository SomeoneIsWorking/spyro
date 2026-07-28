---
id: C024
kind: claim
status: holds
created: 2026-07-28
tags: recomp
falsified_on: 2026-07-28
reconfirmed: 2026-07-28
---

## Claim

Spyro DOES load code from WAD.WAD and call it: the overlay question is answered

## Evidence

The new regression gate caught a recomp-MISS the manual log reading had not: 'no recompiled fn for 0x8007ABAC (caller ra=0x800339E4, c->pc=0x800647A0)'. 0x8007ABAC is ABOVE the resident text end (0x80075800) and sits at heapBase+0x174 (heapBase=0x8007AA38) — inside the very region the owned loader had just filled with 2048 bytes read from WAD.WAD. So the guest loads code off the disc into the heap and calls it. That is the overlay mechanism the disc's file tree could not show, since there are no per-overlay FILES.

## What would falsify it

if 0x8007ABAC is later shown to be data being called through a corrupted pointer rather than loaded code, this is wrong — check whether the loaded bytes at that offset decode as a valid MIPS prologue

## FALSIFIED 2026-07-28

FALSIFIED by its own stated falsifier. The words at heapBase+0x174 (0x00256000, 0x037F2800, 0x0000D000, 0x037FF800) all decode as 'sll' — and 0x00000000 is itself sll $0,$0,0. Every real function entry in this executable opens with addiu (stack frame) or lui: func_80016500=27BDFFC8 addiu, func_8005CBB0=3C028007 lui, func_80064CEC=3C028007 lui. Four consecutive sll is DATA, not a MIPS prologue. So 0x8007ABAC is not loaded code and the guest is calling through a garbage function pointer — most likely one it read out of the bytes MY loader placed there, meaning the recomp-MISS is a symptom of my own wrong data rather than evidence of an overlay mechanism. A second observation supports that: a later load logs the same region as all zeros, so its content is not even stable.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.

## Re-confirmed 2026-07-28

RE-CONFIRMED on much stronger evidence than the original. The original rested on runtime sampling of 0x8007ABAC, which showed data — but that sampled the region at loader time, before whatever loads code there had run, so it could not settle the question. The decisive evidence is STATIC: the instruction at 0x800339DC is 0x0C01EAEB, a DIRECT jal to 0x8007ABAC, and three sibling hardcoded jals target 0x8007AA50 / 0x8007BFD0 / 0x8007CEE4 from genuine game code. A hardcoded call into non-resident space means the game expects code loaded there (C026). My falsification of this claim was itself wrong — it inferred from a timing-dependent sample what only the binary could answer.
