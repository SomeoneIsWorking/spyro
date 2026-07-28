---
id: 9
title: The CD data path may be completable through the cdc model rather than by inferring a destination
status: resolved
symptom: Two attempts to find the transfer destination by inference failed (a1 falsified, C014). Meanwhile the guest is writing CD controller registers directly.
tags: cd,architecture
created: 2026-07-28
updated: 2026-07-28
---

## What changed

`PSXPORT_DEBUG=cdcw` shows the guest driving 0x1F801800-3 itself — 44 writes per boot, bank selects, param-FIFO pushes and command writes, from func_80065270 and func_80065108 (claim C016). mem.cpp routes those into `cdc_native.c`, which already implements the register/FIFO model AND `load_sector()`, filling its data FIFO from `loc_lba`.

## Why this matters

Issue #4 framed the choice as A (override libcd, serve sectors ourselves) vs B (drive the modelled controller). We chose A. But the guest is ALREADY partly on path B: it programs the controller directly, so the model sees Setloc/ReadN whether or not our libcd overrides ACK them.

That suggests the destination question may be the wrong question. Rather than discovering which guest address to copy sectors into, the data may simply need to become available where the guest already expects to fetch it — the controller's data FIFO — with the guest's own code doing the transfer.

## What to check next

1. Does the guest READ 0x1F801802 (data FIFO pop) after issuing its read? If yes, the transfer is its own code and we only need the FIFO filled.
2. Does `load_sector()` ever run — i.e. does the cdc model see a Setloc that sets `loc_lba`, or do our libcd overrides intercept it first and leave the model's position unset?
3. If (2) is the gap, the fix is to let the position reach the model (we already track it in `Cd::setloc_lba`) rather than to copy bytes anywhere.

## Caution

This does NOT mean abandoning option A. It means the A/B line is blurrier than issue #4 assumed, and the cheapest completion may borrow the model's FIFO. Verify (1) and (2) before building either.

### Resolution (2026-07-28)
WRONG — premise does not hold. Zero bank-0 command writes reach cdc_native.c across a full boot and PSXPORT_DEBUG=cdc logs nothing; the 44 register writes are configuration only (bank selects + volume/IRQ mask). The guest issues its real commands through libcd, which our CD_cw override intercepts first, so the model never sees Setloc/ReadN and load_sector never runs. Option A (override-based) remains correct and the DESTINATION question is still the right question.
