---
id: 10
title: The override point is too low: own the game's LOADER, not libcd's command primitive
status: resolved
symptom: Four attempts to make reads deliver data have failed. New evidence: the guest never pops the CD data FIFO and never programs DMA3, so no transfer path is ever entered.
tags: cd,architecture
created: 2026-07-28
updated: 2026-07-29
---

## What the new tracer showed

Added `PSXPORT_DEBUG=cdcr` (CD register READ tracer, the counterpart to the existing cdcw). Over a full boot: 14 status reads, 5 irq reads, **zero** data-FIFO pops, **zero** response reads. Separately, `mem.cpp` implements DMA channels 0/1/2/4/6 but not channel 3 (CD), and PSXPORT_IO_VERBOSE shows no unhandled IO — so DMA3 is not being programmed either.

Both possible transfer routes are untouched. (claim C018)

## Why — and it is our own doing

We override `CD_cw` (func_80064CEC), the command primitive that libcd's read state machine is built on. Our handler ACKs the command and returns, so libcd's higher-level read logic never advances to the stage where it would move bytes. We are intercepting *below* the layer that performs the transfer.

That also explains, in hindsight, why the destination could never be found by inspecting arguments: nothing was ever going to write it, because the writer never ran.

## The correction

Tomba!2's port does NOT override libcd primitives for data. It overrides the ENGINE's file loader — GameConfig's `cdFileLoad` / `cdAsyncRead` (e.g. its FUN_8001db8c with a (dest, lba, size) contract) — i.e. the GAME-level 'load this thing' call, served natively end to end.

Spyro needs the same: find its game-level loader — the function that asks for a file/extent and expects it in memory afterwards — and own that whole call. At that level the destination is an explicit argument rather than something to be reverse-engineered out of a DMA path.

## Concrete next step

Find the caller chain above the read: which game function initiates the WAD load and what it passes. Start from the read-issue site already known (func_80065DBC, ra chain into func_80016500 / func_8001250C) and walk UP to the first function whose arguments look like (destination, offset, length).

### Resolution (2026-07-29)
DONE — and it had been done for a while, which is why this entry staying open cost a session real time. The correction this issue argued for is implemented: both game-level loaders are owned natively in game/core/cd_queue.cpp (cd_loader on 0x80016500, cd_stream_read on 0x80016698), serving bytes and their measured transfer bookkeeping directly. The retained bodies stay compiled as A/B oracles but are no longer dispatched by the product because both descend into libcd's guest VSync queries. The override point is no longer libcd's command primitive. Full contract in C106, including the a3 byte-offset argument this issue never knew about: sector = a0 + a3/2048, and ignoring a3 moves the right byte count to the right destination with the WRONG content — a failure that looks like success. Live evidence: PSXPORT_DEBUG=cdq shows both paths firing with varied offsets and moving 2048-292864 bytes per call; gate 14/14 with arena loads UNMATCHED == 0, which depends on this path. LESSON (workflow): this issue was still 'open' at score 14, so info.py brief ranked it top for a loader query and sent a session to re-derive the contract by disassembly when cd_queue.cpp already documented it in full, including the exact a3 bug as a fixed one. Resolve issues when the work lands, not when someone next trips over them.
