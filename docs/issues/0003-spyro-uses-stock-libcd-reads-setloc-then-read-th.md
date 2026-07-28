---
id: 3
title: Spyro uses STOCK libcd reads (Setloc-then-read); the framework's cd_read contract does not fit
status: open
symptom: After the boot splash the guest spins: a stack profile sits in func_800163E4 (<- 80016500 <- 8001250C <- 800127C0 <- main) and sampled write addresses repeat at 0x801FFDB0/B4 — the same stack slots, i.e. a loop re-pushing one frame, not forward-progressing init. Nothing advances because no CD read ever delivers data.
tags: cd,reads,blocker
created: 2026-07-28
updated: 2026-07-28
---

## The mismatch

psxport's `cd_read` handler has the contract `(a0=blocks, a1=lba, a2=buf)` — a single synchronous read primitive that is TOLD its LBA. That fits psxport's reference consumer (Tomba!2's `FUN_8008c1ec`, an engine-specific loader).

Spyro links **stock Sony libcd**, where the read path is the classic two-step:
1. `CdControl(CdlSetloc, msf, 0)` sets the drive position;
2. `CdRead(sectors, buf, mode)` / ReadN transfers from wherever the head was left.

The LBA is therefore **not an argument to the read at all**. Confirmed from the body: `func_80065DBC` (the `CdRead: Shell open...`/`retry...` function) keeps only `a0` (in r17) and passes 0 to its callees. `func_800659F0` carries `CdRead: sector error`.

## Why wiring it anyway would be harmful

`cd_read` would take whatever happens to be in a1/a2 as lba/buf and `mem_w8` 2048 bytes per block into that address. A wrong buf is arbitrary guest-memory corruption surfacing far from the cause — strictly worse than the current honest spin.

## What is actually needed

A Spyro-specific read path that reconstructs the missing LBA:
- **Capture Setloc.** `cd_command` already intercepts cmd 0x02, but routes it to `xa_stream_setloc` (XA audio streaming) and does NOT retain a data-read position. Spyro needs the BCD MSF converted to an LBA and remembered.
- **Serve the read** from that remembered LBA into the buffer the guest passes, matching stock libcd's own argument order — which must be read out of `func_80065DBC`/`func_800659F0` rather than assumed.

## Note on the framework

This is not a framework bug; `cd_read` is a reasonable primitive for a game that has one. It is a genuine per-game difference: a stock-libcd game needs a Setloc-tracking read path. If a second stock-libcd consumer appears it may be worth generalising.
