---
id: C114
kind: claim
status: holds
created: 2026-07-29
tags: input,stage
---

## Claim

C112 confirmed by a second, independent instrument. Tracing the two MAIN functions that the candidate path calls — 0x80032AB0 at 0x8007B878 and 0x8001277C at 0x8007B8DC — over 3000 frames with FORCE_BUTTONS driving the port into title sub-state 1 shows BOTH as NEVER CALLED. 0x80032AB0 is invoked before the button test, so control does not even enter the region, let alone fail one of the block's conditions. The watchpoint result (no stores to [0x80078D88], which that region writes) and the call trace agree.

## Evidence

PSXPORT_FNTRACE=0x80032AB0,0x8001277C with PSXPORT_FORCE_BUTTONS=FFF7 over 3000 frames: both report NEVER CALLED. The tracer was validated in the same session against a known-positive (0x8001F798, 71558 calls, independently at 0.99% of host profile samples) and a known-negative (0x8006276C, never called, independently confirmed by ndiff).

## What would falsify it

Either function being observed called in any run that reaches title sub-state 1, which would mean the region does execute and both this and C112 are wrong.
