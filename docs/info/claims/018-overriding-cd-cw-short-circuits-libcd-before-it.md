---
id: C018
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

Overriding CD_cw short-circuits libcd before it ever transfers: the guest neither pops the data FIFO nor programs DMA3

## Evidence

New cdcr tracer over a full boot: 14 status reads, 5 irq reads, ZERO data-FIFO pops and ZERO response reads. PSXPORT_IO_VERBOSE=1 shows no unhandled IO, and mem.cpp handles DMA channels 0/1/2/4/6 but NOT channel 3 (CD) — so the guest is not programming DMA3 either. Both transfer routes are untouched, which means libcd never reaches its transfer stage: our override of CD_cw (the command primitive its read state machine is built on) ACKs the command and returns, so the higher-level libcd read logic never runs.

## What would falsify it

if a run ever logs a DATA-FIFO pop or a DMA3 register access, the guest does reach a transfer path and this is wrong
