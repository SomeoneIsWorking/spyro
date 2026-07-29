---
id: C116
kind: claim
status: holds
created: 2026-07-29
tags: input,irq,callback
---

## Claim

0x80067CD4 — the sole setter of the flag the title screen waits on — is a CALLBACK the port never delivers. It is registered at 0x80066304 by 'jal 0x8005DE8C' with a0=7 and a1=0x80067CD4, the address formed by a lui/addiu pair at 0x800662FC/0x80066300 (which is why a word-scan for a pointer to it found nothing, and why it has zero static callers). 0x8005DE8C is a libetc-style registrar: it dispatches through the table at [0x800749AC]+0x14. Its sibling 0x8005DE58 is VSyncCallback, which this port DOES intercept and hand-deliver every frame in game/core/vsync.cpp because no IRQ fires on its own — 0x8005DE8C gets no such treatment, so callback 7 is registered and then never invoked. INFERENCE, not yet confirmed: a0=7 is the PSX interrupt number, where 7 is controller/memory-card byte-received; the handler's helpers (0x80069030, 0x80068FC4) would then be that device's state machine.

## Evidence

lui/addiu pair scan of MAIN and every overlay image finds exactly one construction of 0x80067CD4, at 0x800662FC+0x80066300, feeding a1 into 'jal 0x8005DE8C' with 'addiu a0,zero,7' in the delay slot. PSXPORT_FNTRACE over 3000 frames with FORCE_BUTTONS: 0x80067CD4, 0x80069030 and 0x80068FC4 all NEVER CALLED. Separately REFUTED as the mechanism: the one registered InterruptElement (0x80075C58) has handler 0x8006969C and verifier 0x80069634, both also NEVER CALLED, and that verifier tests I_STAT/I_MASK bit 0 (VBLANK — [0x8007521C] reads 0x1F801070) then calls [0x800751E4], which is NULL in a title-screen RAM dump. So that element is not the path either.

## What would falsify it

0x8005DE8C turning out not to be an interrupt/callback registrar once its dispatch target [[0x800749AC]+0x14] is read, or a0=7 meaning something other than an interrupt number.
