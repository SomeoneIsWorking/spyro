---
id: C022
kind: claim
status: holds
created: 2026-07-28
tags: boot
---

## Claim

The post-CD stall is a BIOS EVENT poll: func_8005CBB0 tests event class 0xF1000000, which never becomes ready

## Evidence

In-process probe (override + super-call) on func_8005CBB0: every call has a0=0, A[0x800730F0]=0, B[0x80073588]=0 and returns v0=0, unchanged across calls. The third global it loads and passes onward, [0x800730E8], is 0xF1000000 — a PSX BIOS EVENT CLASS descriptor (the same 0xFxxxxxxx family as GameConfig::irqEventClasses, whose reference-consumer values are 0xF2000003/0xF0000001/0xF0000009). So this is TestEvent-shaped polling on an event that is never delivered, not a compute loop.

## What would falsify it

if 0xF1000000 is shown not to be an event class, or the two globals are seen changing without any event delivery, this reading is wrong
