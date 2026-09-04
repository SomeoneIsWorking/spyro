---
id: C111
kind: claim
status: holds
created: 2026-07-29
tags: input,stage,overlay
---

## Claim

The exit from Spyro's title sub-state 1 is gated at 0x8007CBA0 on [0x80078D7C] == 5, and this is a binary fact, not a runtime one. 0x8007CBA0 has exactly one predecessor — the branch at 0x8007CAA8 — whose delay slot 'addiu v0,zero,5' always executes, so control arrives with v0 = 5 unconditionally; s0 is loaded once at 0x8007C55C from 0x80078D7C and never rewritten. Of the 19 immediate-form writers of that global, exactly one stores 5 (0x8007B8F8 in OV_5B800), and the pair just before it stores 2 to the sub-state, so a single block performs both halves of the transition. That block lives in the sub-state-1 arm of the handler, which the dispatch at 0x8007AD28-0x8007AD5C selects: sub 0 -> 0x8007AD64, sub 1 -> 0x8007B0B8, sub 2 -> 0x8007C454, anything else -> exit.

## Evidence

Read from OV_5B800 with residency confirmed against a fresh title-screen RAM dump (arena matches 256/256 first words; resident word at 0x8007CBA0 is 0x16020029, the same bne as the image). The single predecessor was found with tools/xrefs.py, validated first on 0x8007CC48 where it recovers all four branches a manual listing shows. The unique 5-storing writer was found with tools/writers.py --value 5 (19 writers total). Observed under FORCE_BUTTONS: the sub-1 arm does run and writes [0x80078D7C]=1 at f837, but never 5 across 4000 frames.

## What would falsify it

A second predecessor to 0x8007CBA0 appearing in whichever overlay is resident, or an observed transition out of sub 1 while [0x80078D7C] holds a value other than 5.
