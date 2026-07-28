---
id: C093
kind: claim
status: holds
created: 2026-07-29
tags: cd,framework,regression
---

## Claim

An upstream psxport change (5daf2fe4, moving CD sector advance to the interrupt-ack path) broke this port completely — 4.4M runaway out-of-range reads, 8 frames instead of 18809 — and the gate caught it.

## Evidence

Gate went from 14/14 to 6 FAILs: frames 18809 -> 8, bytes from disc 13178880 -> 4096, CD completions 63 -> 1, overlays 7 -> 2, with 4416363 'out of range' lines in the run log where the previous gate had ZERO. Bisected by reverting 5daf2fe4 alone: bad reads drop to 0. MECHANISM: the commit moves sector advancement out of the drain path into the ack path, which is right for a streaming reader that only reads sector headers and never drains — but acking an INT1 then raises the NEXT INT1, which is acked, which advances again. For a consumer whose ack handling differs that is a feedback loop with no bound, and the head walks to LBA 23476094 on a ~281k-sector disc. FIXED WITHOUT REVERTING: load_sector now clears s->reading when the LBA is unreadable, because a real drive cannot keep presenting sectors past the lead-out — so the loop terminates on the hardware's own terms and the upstream intent is preserved.

## What would falsify it

if the streaming consumer 5daf2fe4 was written for now stalls at end-of-disc, the stop is too aggressive and needs to distinguish 'past the lead-out' from 'this particular sector is unreadable'
