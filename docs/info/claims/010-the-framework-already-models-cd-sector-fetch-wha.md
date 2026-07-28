---
id: C010
kind: claim
status: holds
created: 2026-07-28
tags: cd
---

## Claim

The framework already models CD sector fetch; what it lacks is guest-visible IRQ delivery

## Evidence

runtime/recomp/cdc_native.c: load_sector() reads a sector into the data FIFO using s->loc_lba, and cdc_irq(s,1,...) queues INT1 data-ready; mem.cpp routes 0x1F801800-3 to the model. But the queued IRQ never reaches guest code, so libcd's state machine never advances — evidenced by the pre-override boot printing 'CD timeout: CD_cw:(CdlNop)' while polling.

## What would falsify it

if guest code is ever seen running a CD IRQ callback, or a read completes without an override, then IRQ delivery does exist and this is wrong
