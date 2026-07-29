---
id: C116
kind: claim
status: falsified
created: 2026-07-29
tags: input,irq,callback
falsified_on: 2026-07-29
---

## Claim

0x80067CD4 — the sole setter of the flag the title screen waits on — is a CALLBACK the port never delivers. It is registered at 0x80066304 by 'jal 0x8005DE8C' with a0=7 and a1=0x80067CD4, the address formed by a lui/addiu pair at 0x800662FC/0x80066300 (which is why a word-scan for a pointer to it found nothing, and why it has zero static callers). 0x8005DE8C is a libetc-style registrar: it dispatches through the table at [0x800749AC]+0x14. Its sibling 0x8005DE58 is VSyncCallback, which this port DOES intercept and hand-deliver every frame in game/core/vsync.cpp because no IRQ fires on its own — 0x8005DE8C gets no such treatment, so callback 7 is registered and then never invoked. INFERENCE, not yet confirmed: a0=7 is the PSX interrupt number, where 7 is controller/memory-card byte-received; the handler's helpers (0x80069030, 0x80068FC4) would then be that device's state machine.

## Evidence

lui/addiu pair scan of MAIN and every overlay image finds exactly one construction of 0x80067CD4, at 0x800662FC+0x80066300, feeding a1 into 'jal 0x8005DE8C' with 'addiu a0,zero,7' in the delay slot. PSXPORT_FNTRACE over 3000 frames with FORCE_BUTTONS: 0x80067CD4, 0x80069030 and 0x80068FC4 all NEVER CALLED. Separately REFUTED as the mechanism: the one registered InterruptElement (0x80075C58) has handler 0x8006969C and verifier 0x80069634, both also NEVER CALLED, and that verifier tests I_STAT/I_MASK bit 0 (VBLANK — [0x8007521C] reads 0x1F801070) then calls [0x800751E4], which is NULL in a title-screen RAM dump. So that element is not the path either.

## What would falsify it

0x8005DE8C turning out not to be an interrupt/callback registrar once its dispatch target [[0x800749AC]+0x14] is read, or a0=7 meaning something other than an interrupt number.

## FALSIFIED 2026-07-29

The registration half is right and is re-confirmed below; TWO readings in it are wrong. (1) I said the registration 'never ran' on the strength of slot 7 reading 0 — but that snapshot came from a run which never left sub-state 0, so the registration had not happened yet. Reading state from the wrong regime is the exact mistake issue 0027 already records once ('do not reuse readings taken in sub 0'), and I repeated it. Measured in the FORCE_BUTTONS regime, 0x800662BC runs at frame 835 and slot 7 DOES hold 0x80067CD4. (2) The a0=7 'PSX controller/memory-card IRQ' inference is refuted: 0x800749C0 is a libetc CALLBACK-SLOT table, not an IRQ table — VSyncCallback (0x8005DE58) hardcodes a0=4 for a slot holding the VSync handler, whereas PSX IRQ 4 is TIMER0. Index 7 is a libetc slot index of still-unknown meaning. Corrected and completed in C117.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
