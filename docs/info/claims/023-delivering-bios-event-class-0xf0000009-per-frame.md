---
id: C023
kind: claim
status: holds
created: 2026-07-28
tags: boot
---

## Claim

Delivering BIOS event class 0xF0000009 per-frame unblocks the boot: 226 -> 436 frames with changing content

## Evidence

PSXPORT_DEBUG=ev showed the guest opens exactly one event: class=0xF0000009 spec=0x20 -> handle=0xF1000000, the handle func_8005CBB0 polls forever (C022). Put that class in GameConfig::irqEventClasses and delivered it from the vblank wait — necessary because the framework's normal delivery point (native_step_frame) never runs while the guest owns its own frame loop. Result in a 40s run: frames 226 -> 436, and 18 DISTINCT frame-occupancy values vs ~2 before (held splash + black), i.e. the picture is actually changing rather than static.

## What would falsify it

if a later run shows occupancy collapsing back to a couple of distinct values, the progression stopped and this no longer holds
