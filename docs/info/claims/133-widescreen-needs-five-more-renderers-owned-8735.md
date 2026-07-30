---
id: C133
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen,ownership
---

## Claim

Widescreen needs five more renderers owned, ~8735 instructions: 0x80022A2C (598, active+validatable), 0x8004F000 (303), 0x8001F798 (1113), 0x80020F34 (1726), 0x800258F0 (4995, 7 bounds). 0x800580F4 is NOT a target — its 0x02000000 is a packet tag, not a clip bound.

## Evidence

Scan of lui rX,0x0200 classified by FIRST USE to end of function:  = ordering-table tag,  = clip bound. 12 real bounds and 3 tags across the 19-member family. Confirmed by reading 0x800580F4 directly (tag stored at 0x80058248; a sprite renderer with one RTPS and no clip-code packing) and 0x8004EBA8 (bound at 0x8004ED8C, used by  40 instructions later).

## What would falsify it

any renderer whose bound count changes when read directly — the classifier follows only the FIRST use of the register and would misread a value used as a tag first and a bound later. Read the function before committing to transcribe it; that is what caught this error.
