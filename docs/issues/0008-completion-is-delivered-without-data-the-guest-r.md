---
id: 8
title: Completion is delivered without data: the guest re-reads LBA 37 forever
status: open
symptom: With the CD completion event delivered, the wait loop now succeeds and the guest issues further requests — but PSXPORT_DEBUG=cd shows it seeking only LBA 37, repeatedly. It is retrying one read, not loading the WAD.
tags: cd,blocker
created: 2026-07-28
updated: 2026-07-28
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
