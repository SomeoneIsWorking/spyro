---
id: 12
title: New stall after the CD fix: func_8005CBB0, a library-region poll
status: open
symptom: After serving the game loader, the boot advances past the CD wait and now stalls with 5/5 profile samples in gen_func_8005CBB0 <- gen_func_80014564 <- gen_func_800127C0 <- main.
tags: boot,blocker
created: 2026-07-28
updated: 2026-07-28
---

## Where it is

func_8005CBB0 sits in the library region (between crt0 0x8005B8E0 and libcInit 0x8005DB14). Its body reads two globals (via lui 0x8007 + offsets 12528 and 13704), compares each against 1, and on the non-early path loads a third (offset 12520) and calls func_8005DB84 — the shape of a readiness/state poll rather than a compute loop.

## Why this matters

It is a DIFFERENT branch from the CD wait that held the boot for several iterations: func_80014564 was previously only a reader of the CD gate, never the active path. Serving the loader moved execution here (claim C021), which is the first evidence that the CD work unblocked something real rather than just moving bytes.

## Next

Identify what the two globals mean and who sets them — same in-process method that worked on the CD chain (override + super-call + log the actual words), not static decode. If it is a poll on something an interrupt would normally set, it is the same class of problem as the CD completion callback and may have the same shape of fix.

### Note (2026-07-28)
IDENTIFIED via in-process probe: this is a BIOS EVENT poll, not a generic readiness check. Every call sees a0=0, A[0x800730F0]=0, B[0x80073588]=0, returns 0 — never changing. The third global [0x800730E8] holds 0xF1000000, a PSX event-class descriptor (same 0xFxxxxxxx family as GameConfig::irqEventClasses). So the guest is waiting on an event the runtime never delivers — the SAME class of problem as the CD completion callback, which was fixed by delivering the event the hardware would have raised rather than by poking the flag. Spyro's GameConfig::irqEventClasses is still {0,0,0}; that is the framework seam for this. Next: identify which event class 0xF1000000 is and what delivers it, then wire it the same way.
