---
id: C115
kind: claim
status: holds
created: 2026-07-29
tags: input,stage,irq
---

## Claim

The title screen's failure to advance is a single unbroken causal chain ending in a function the port never invokes. (1) Leaving sub-state 1 needs [0x80078D7C] == 5 (C111). (2) The sub-1 arm is NOT linear: 0x8007B0B8 loads [0x80078D88], bounds-checks it against 16 and jumps through a 16-entry table at 0x8007AA54, so only the selected case runs — which is why most of the arm measured dead (C112/C114). (3) The live case calls 0x80067628 every frame and exits immediately at 0x8007B100 when it returns 0. (4) 0x80067628 returns 0 precisely when [0x80075B58] == 0; when that flag is set it takes the other path, reports and clears it. (5) [0x80075B58] has 15 immediate-form writers, 14 of which clear it; the ONLY setter is 0x80067D10 (v0=1) inside function 0x80067CD4. (6) 0x80067CD4 is NEVER CALLED, and has zero static callers and zero data references anywhere in MAIN or any overlay — so it is reached indirectly, and the recompiler classifies it as a genuine function entry.

## Evidence

PSXPORT_FNTRACE over 3000 frames with FORCE_BUTTONS=FFF7 (port in sub-state 1). All 13 MAIN callees of the sub-1 arm traced in one batch: only 0x80067628 (164553 calls, first f837 from ra=0x8007B100), 0x800665B8 (exactly 1 call, f837, ra=0x8007B12C — matching the known [0x80078D7C]=1 write) and the general-purpose 0x80016958 are reached; the other ten are NEVER CALLED. A second batch traced 0x80067CD4, 0x80069030 and 0x80068FC4 — all three NEVER CALLED. Writers of 0x80075B58 enumerated with tools/writers.py (15 sites, one non-zero). xrefs.py finds no branch to 0x80067CD4 and a word-scan finds no pointer to it in MAIN or any overlay image.

## What would falsify it

0x80067CD4 being observed called in any run, or a branch/pointer to it being found in a module not yet scanned (only MAIN and the extracted overlay images were searched).
