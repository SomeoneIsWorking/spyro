---
id: C121
kind: claim
status: holds
created: 2026-07-29
tags: event,memcard,framework
---

## Claim

Delivering EvMdINTR events by CALLING their handler unblocks Spyro's title screen. Hle::deliverEvent previously only set ev[i].fired, which is right for polled EvMdNOINTR events (TestEvent reads and clears it) but wrong for EvMdINTR, where the BIOS invokes the handler and the game never polls. Spyro opens NINE events: one polled (0xF0000009 spec 0x20, mode 0x2000) and EIGHT in callback mode — four HwCARD 0xF0000011 and four SwCARD 0xF4000001, specs 4/0x100/0x2000/0x8000. psxport's memory-card model already delivered SwCARD spec 4 and HwCARD spec 4; those deliveries were landing on slots whose handlers were never called. With the fix they run, and the whole measured chain of issue 0027 completes.

## Evidence

Before: 0x80067DD0 NEVER CALLED. After: REACHED at frame 838, together with HwCARD's 0x80067E20. Watchpoint on the stage globals with FORCE_BUTTONS then shows the full progression that was stuck for this entire investigation: f835 sub=1, f837 gate=1, f871 gate=2, f961 sub=2 AND gate=5 written from ra=0x8007B8E4 — the block at 0x8007B85C previously proven unreachable (C112/C114) — then f995 sub=3 and further transitions. Gate 14/14 (41322 frames, 0 divergences, 0 recomp misses). Note the gate does not press buttons, so it does not exercise this path; the evidence above is from FORCE_BUTTONS runs.

## What would falsify it

An EvMdINTR handler observed running twice per delivery, or a game regressing because a handler that used to be merely marked now actually runs.
