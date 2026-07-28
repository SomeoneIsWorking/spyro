---
id: C042
kind: claim
status: holds
created: 2026-07-28
tags: overlay,blocker
---

## Claim

The intro stage allocates arena space for a level overlay and calls its handler, but never issues the CD read — the queue is idle, not stalled

## Evidence

Probes (PSXPORT_DEBUG=lvl,cdq) over a full run. Sequence: boot leaves the stage mode at 13 (the intended intro stage); sub-state [0x80078D78] advances 0 -> 3; the mode-13/sub-3 handler 0x80032B08 runs repeatedly while its own sub-sub-state [0x80078D7C] advances 0 -> 1 -> 2. That handler IS the level-load setup — at 0x80032B68 it reads the arena base constant [0x800113A0] = 0x8007AA38 and writes it to the arena cursors [0x800785D8]/[0x800785DC] — but the reset is SKIPPED because the guard [0x80078D94] reads 2, not 0. The cursor nevertheless advances from 0x8007DDE8 to 0x8008A3B8, i.e. arena space IS being allocated well past the handler address 0x8008772C. The handler pointer [0x800758CC] is then installed as 0x8008772C and called, and the port dies there. CRUCIALLY the CD queue is IDLE throughout: every service tick logs gate=0 status=0x40->0x40 pending=0->0 queued=0->0. So the load is not queued-and-unserviced and the game is not waiting on the CD — it never asks. Combined with C041 (no direct path from this handler to the loader), the read is simply never issued on the path taken.

## What would falsify it

Any cdq line showing a non-zero queued/pending entry during the intro, which would mean a request IS outstanding and my service path is dropping it.
