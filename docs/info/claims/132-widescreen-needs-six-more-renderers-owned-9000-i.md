---
id: C132
kind: claim
status: falsified
created: 2026-07-30
tags: render,widescreen,ownership
falsified_on: 2026-07-30
---

## Claim

Widescreen needs six more renderers owned, ~9000 instructions total: 0x800580F4 (244), 0x8004F000 (303), 0x80022A2C (598), 0x8001F798 (1113), 0x80020F34 (1726), 0x800258F0 (4995). Of these, 0x800580F4 and 0x80022A2C are confirmed ACTIVE and differentially validatable.

## Evidence

Clip-bound scan (lui rX,0x0200) over real function extents from generated/rec_decls.h, correcting C127's family-span error. Identity probes at PSXPORT_NDIFF=40: 0x800580F4 40/40 exact, 0x80022A2C 40/40 exact, 0x8004F000 and 0x80050240 never called in the measured scene (0 calls, so unproven rather than failing). 0x8004EBA8 is already owned at 278 instructions and verified 400/400.

## What would falsify it

a renderer whose identity probe DIVERGES (it would need a different acceptance test), or a scene in which one of the never-called ones runs — 'not called' is a statement about the measured scene, not about the game

## FALSIFIED 2026-07-30

WRONG AGAIN, and this is the third time on the same measurement — the count came from grepping  without checking what the value is USED for. 0x02000000 is ambiguous: it is 512<<16 as a clip bound AND the ordering-table tag for a 2-word packet. I counted tags as bounds.

Caught it by reading 0x800580F4 instead of trusting the scan: its only 0x0200 is at 0x80058248, stored immediately by  — a packet tag. It is a SPRITE/billboard renderer (one RTPS, a distance-LOD shift, a perspective divide, a GP0 E1 draw-mode word), has NO clip bound, and is not a widescreen target at all. It was top of my list purely because it was smallest.

Classifying by first use, scanning to end of function (a lookahead of 8 instructions also gave a wrong answer — 0x8004EBA8's lui and its  are 40 apart):
    0x8004EBA8    278 instr   1 bound    OWNED (C130), widened (C131)
    0x80022A2C    598 instr   1 bound    active + identity-probed 40/40  <- next
    0x8004F000    303 instr   1 bound    never called in the measured scene
    0x8001F798   1113 instr   1 bound
    0x80020F34   1726 instr   1 bound
    0x800258F0   4995 instr   7 bounds   hottest guest function; also issue 0038
    0x800580F4    244 instr   0 bounds   NOT a target (packet tag, sprite renderer)

Replaced by C133. THE LESSON, since this measurement has now been wrong three ways: a constant's VALUE does not identify its ROLE. Follow the use — stored means tag, subtracted means bound — and scan to the end of the function, because a hand-written renderer loads its bounds once at the top and uses them deep in a loop.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.

## FALSIFIED 2026-07-30

WRONG AGAIN, and this is the third time on the same measurement. The count came from grepping for lui rX,0x0200 without checking what the value is USED for. 0x02000000 is ambiguous: it is 512<<16 as a clip bound AND the ordering-table tag for a 2-word packet. I counted tags as bounds.

Caught by READING 0x800580F4 rather than trusting the scan: its only 0x0200 is at 0x80058248 and is stored immediately by a sw to the packet pointer, so it is a tag. That function is a SPRITE/billboard renderer (one RTPS, a distance-LOD shift, a perspective divide, a GP0 E1 draw-mode word), carries NO clip bound, and is not a widescreen target. It had been top of my list purely for being smallest.

Classifying by first use and scanning to END OF FUNCTION (an 8-instruction lookahead also gave a wrong answer, because 0x8004EBA8 loads its bound 40 instructions before using it):
    0x8004EBA8    278 instr   1 bound    OWNED (C130), widened (C131)
    0x80022A2C    598 instr   1 bound    active + identity-probed 40/40  <- next
    0x8004F000    303 instr   1 bound    never called in the measured scene
    0x8001F798   1113 instr   1 bound
    0x80020F34   1726 instr   1 bound
    0x800258F0   4995 instr   7 bounds   hottest guest function; also issue 0038
    0x800580F4    244 instr   0 bounds   NOT a target

Replaced by C133. THE LESSON, since this has now been wrong three ways: a constant's VALUE does not identify its ROLE. Follow the use (stored means tag, subtracted means bound) and scan to the end of the function, because a hand-written renderer loads its bounds once at the top and uses them deep inside a loop. This is the project's own "a grep count is text, not code" rule, and I broke it three times on one question.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
