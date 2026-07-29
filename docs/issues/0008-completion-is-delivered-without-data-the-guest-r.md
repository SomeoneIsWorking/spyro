---
id: 8
title: Completion is delivered without data: the guest re-reads LBA 37 forever
status: resolved
symptom: With the CD completion event delivered, the wait loop now succeeds and the guest issues further requests — but PSXPORT_DEBUG=cd shows it seeking only LBA 37, repeatedly. It is retrying one read, not loading the WAD.
tags: cd,blocker
created: 2026-07-28
updated: 2026-07-29
---

## State

Delivering the guest's CD event callback func_80016490(a0=2) from our synchronous CD path correctly advances the state machine: gate clears, func_80016500's wait succeeds, the next request is issued. That confirmed the gate/callback analysis (claim C013).

## Why it is NOT a working read

No sector data is transferred. The guest is told its read completed while its destination buffer is untouched, so it validates the (absent) data and retries the same sector. Every Setloc in a 30s run resolves to LBA 37 — the start of WAD.WAD — and never advances.

This is deliberately left visible in game/core/cd_queue.cpp with the consequence named, rather than being presented as a working CD path. Frames are still 8 (the boot splash); nothing new renders.

## The real fix

Couple transfer and completion: read sectors from Cd::setloc_lba into the guest's destination buffer, then deliver completion. That needs the transfer path read out of its body — func_8006606C is the candidate (it computes 512 vs 585/582 words per sector from a mode word, i.e. sector-size selection, which is what a CD DMA setup does). Its argument roles must be confirmed from the body before anything is wired, per the rule that has kept four CD chokepoints correct.

## Do not

Do not extend the current shortcut by faking data (zero-filling the buffer, or short-circuiting the guest's validation). The retry loop is the honest signal that the read has not happened.

### Note (2026-07-28)
TESTED AND FALSIFIED: a1 as the destination buffer. Probes gave a1=0x8007AA38 (heapBase) for both read-path functions, which looked conclusive; a per-sector copy there produced NO behavioural change (guest re-issued the same LBA 37 read, frames stayed 8). Reverted — an inferred destination that fails its predicted effect is an unvalidated 2048-byte-per-iteration guest-memory write. a1 may be a descriptor/mode block, not a buffer. SECOND finding: the completion edge-trigger LATCHES — the guest re-issues its read (re-setting the gate) before the next sample, so exactly one completion is ever delivered. Any replacement must key off the read ISSUE, not off observing the gate reach 0.

### Resolution (2026-07-29)
The observation was correct and became the key to the fix. a0 IS always 37 — it is WAD.WAD's base LBA, not a per-request address — so 'seeking only LBA 37' is what a correct trace of this loader looks like. The per-request selector is the a3 BYTE OFFSET, which nothing was reading at the time; sector = a0 + a3/2048 (C106). It was not retrying one read, it was issuing many reads whose distinguishing argument was invisible to the tracer. Live now: PSXPORT_DEBUG=cdq shows a3 = 0x00000/0x5F000/0x5F800/0x5B800/0x00800 on the sync path and 0x0DF800/0x127000/0x148800/0x188800/0xB83800 on the streaming path, moving 2048-292864 bytes each. 29343744 bytes total off the disc per 40s gate. Gate 14/14.
