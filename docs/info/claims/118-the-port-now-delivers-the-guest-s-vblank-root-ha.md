---
id: C118
kind: claim
status: holds
created: 2026-07-29
tags: vsync,callback,irq
---

## Claim

The port now delivers the guest's VBlank ROOT handler rather than one hand-picked callback. libetc keeps a root-handler table at 0x80073928 indexed by IRQ (slot 0 = VBLANK), and the guest installs 0x8005E560 there at boot via 0x8005E224, which enables bit 1<<0 in I_MASK ([0x800749B4] = 0x1F801074). That root handler increments the vblank counter [0x800749E0] and then walks the 8-entry callback table at 0x800749C0, calling every registered slot. game/core/vsync.cpp previously ran only the handler captured from VSyncCallback — slot 4 — so every other slot the game registered was silently dropped. It now runs the root handler, and re-reads the counter instead of maintaining it, because the root handler owns that increment. The captured slot-4 handler remains as a fallback for before libetc installs the root handler.

## Evidence

PSXPORT_FNTRACE after the change: 0x8005E560 270841 calls (first frame 0), and slot 7's handler 0x80067CD4 270006 calls (first frame 835, from ra=0x8005E5B0 — the dispatcher's own jalr). Before the change 0x80067CD4 was NEVER CALLED. Gate 14/14 with 41282 frames presented, 0 recomp misses, 0 native/substrate divergences, so running the full callback chain every frame is behaviourally safe. Root table read from a title-screen RAM dump: [0x80073928] = 0x8005E560, [0x800749B4] = 0x1F801074 (I_MASK), [0x800749B0] = 0x1F801070 (I_STAT).

## What would falsify it

The vblank counter [0x800749E0] advancing at a different rate than one per presented frame, or a slot handler being observed to run twice per vblank (which would mean the port is delivering it in addition to the root handler rather than through it).
