---
id: C117
kind: claim
status: holds
created: 2026-07-29
tags: input,irq,callback
---

## Claim

The title screen stalls because the guest's libetc CALLBACK DISPATCHER never runs in this port. 0x800749C0 is a callback-slot table written by the setter 0x8005E5D8 (table[index] = fn); 0x8005DE58 is VSyncCallback and hardcodes index 4, while 0x8005DE8C is the generic SetCallback(index, fn). Spyro registers 0x80067CD4 into slot 7 at frame 835 (0x800662BC -> 0x8005DE8C). The reader/dispatcher that walks the table is 0x8005E560 — and it is NEVER CALLED. So slot 7's handler never executes, [0x80075B58] is never set, 0x80067628 returns 0 every frame, the sub-1 arm exits at 0x8007B100, and [0x80078D7C] never reaches the 5 the exit gate needs. VSync appears to work only because game/core/vsync.cpp hand-delivers slot 4's handler directly, bypassing this dispatcher entirely — so the port special-cases exactly one callback and drops the rest.

## Evidence

PSXPORT_FNTRACE with FORCE_BUTTONS=FFF7 over 3000 frames: 0x800662BC 1 call at f835 from ra=0x8007B02C; 0x8005DE8C 1 call at f835 from ra=0x8006630C; 0x8005E508 (callback-system init) 1 call at frame 0; 0x8005E560 (the dispatcher) NEVER CALLED. REPL read of the table in the same regime: [0x800749D0] = 0x80053C68 (slot 4, the VSync handler vsync.cpp delivers) and [0x800749DC] = 0x80067CD4 (slot 7). The table is 8 entries; 0x800749E0 onward is auxiliary state (a counter, and 0x1F801114/0x1F8010F4 register addresses), not slots.

## What would falsify it

0x8005E560 being observed called in any run, or a second dispatcher of the same table being found (only lui/addiu references to 0x800749C0 were scanned, so a dispatcher reaching it through a computed pointer would have been missed).
