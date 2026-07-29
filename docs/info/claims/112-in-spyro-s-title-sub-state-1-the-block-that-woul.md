---
id: C112
kind: claim
status: holds
created: 2026-07-29
tags: input,stage,overlay
---

## Claim

In Spyro's title sub-state 1 the block that would advance to sub-state 2 (0x8007B85C in OV_5B800, which stores sub=2 and the gate value 5) is NEVER EXECUTED — its four preconditions all hold, so the obstacle is reachability, not the conditions. Under FORCE_BUTTONS after f837, with the sub-1 arm confirmed running: [0x80078D84] climbs 0x20->0x5C (its >= 8 test passes), [0x80078D8C] == 0 (passes), and the pulsed input supplies an edge every 32 frames (passes). Yet a watchpoint over the whole state block 0x80078D78..0x80078DB0 shows that after f900 the ONLY stores in it are [0x80078D80] and [0x80078D84], 137255 each, both from pc=0x80058CC0 — the idle-animation counters called from ra=0x8007CC50, just past the handler's common exit. [0x80078D88] is never written, and 0x8007B818 in the same straight-line region writes it, so that region does not run.

## Evidence

PSXPORT_FORCE_BUTTONS=FFF7 with PSXPORT_WWATCH=0x80078D78,0x80078DB0 over 1400 frames, 275313 logged stores. REPL reads of the state words at f900/940/980/1020 give the condition values above. Residency of OV_5B800 previously confirmed 256/256 against a title-screen RAM dump. The block's true entry is 0x8007B85C (0x8007B858 is the preceding branch's delay-slot nop) and tools/xrefs.py finds its single predecessor at 0x8007B804.

## What would falsify it

Any observed store to [0x80078D88] or [0x80078D7C] from within the sub-1 arm after f900, which would mean the region does execute and the reachability conclusion is wrong.
