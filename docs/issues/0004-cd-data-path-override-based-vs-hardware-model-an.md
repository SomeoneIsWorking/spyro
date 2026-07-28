---
id: 4
title: CD data path: override-based vs hardware-model — an architectural fork
status: open
symptom: Guest issues Setmode 0x80 x2, Setloc 00:02:37 (LBA 37 = WAD.WAD), Setmode 0xA0, ReadN (0x06, ra=0x80063E58), then spins forever. No sectors are ever delivered.
tags: cd,architecture,blocker
created: 2026-07-28
updated: 2026-07-28
---

## The exact sequence (PSXPORT_DEBUG=cdcmd — from the running system)

    cmd=0x0E Setmode param=80   ra=0x80063D28
    cmd=0x0E Setmode param=80   ra=0x80063D28
    cmd=0x02 Setloc  00:02:37   ra=0x80063D28   -> LBA 37 = WAD.WAD
    cmd=0x0E Setmode param=A0   ra=0x80063D28
    cmd=0x06 ReadN   mode=1     ra=0x80063E58   -> spins

## The fork

**A. Override-based (wired today).** Intercept libcd at CD_cw and ACK commands. Cheap, and it got the port past CdInit/CD_sync to a rendered splash. But an ACK is not data: something must still transfer sectors, and stock libcd's read carries no LBA, so we would serve it from the newly-tracked Cd::setloc_lba. The catch: overriding CD_cw *bypasses the framework's own CD controller model*, which already knows how to do this.

**B. Hardware-model-based.** runtime/recomp/cdc_native.c already models the controller — register/FIFO banking, and load_sector() which fills the data FIFO from loc_lba and queues an INT1 data-ready. mem.cpp routes 0x1F801800-3 to it. A stock-libcd game talks to exactly those registers, so this is the path it was designed for.

**What blocks B:** libcd's state machine advances on the CD interrupt reaching GUEST code. cdc_irq() queues the IRQ inside the model, but this runtime raises no interrupts into the guest, so the callback libcd installed never runs and the read never completes. B needs guest-visible IRQ delivery (or an HLE that advances libcd's state machine) — a real framework feature, not a wiring change.

Evidence B is inert today: before any overrides were wired, the boot still printed 'CD timeout: CD_cw:(CdlNop)' — the guest polled and the modelled controller never satisfied it.

## Why this is a decision, not a task

A is faster and matches how the reference consumer works. B is more faithful, reusable for any stock-libcd game, and removes the need to override libcd at all — but costs an interrupt path. Picking A now may mean unpicking it later.
