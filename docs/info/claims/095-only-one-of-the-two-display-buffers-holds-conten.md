---
id: C095
kind: claim
status: holds
created: 2026-07-29
tags: gpu
---

## Claim

Only ONE of the two display buffers holds content at a given instant, while the presented region alternates — so roughly half of presented frames come from an empty buffer.

## Evidence

Full-VRAM dump at frame 900: content occupies x=0..512, y=248..472 (draw1's region per C068) and draw0's region y=8..232 is 0% non-black. Meanwhile a 45s run presents 136 frames at disp=(0,0) and 155 at disp=(0,240) — the region alternates correctly, so the port is NOT stuck on one buffer (an earlier reading that it was came from the trace printing only every 200th present, which aliased with the alternation). Direct region dumps confirm: (0,240) is 93.3% non-black, (0,0) is 0.0%.

## What would falsify it

if draw0 fills in later in the run, this is just an early-boot state and not a standing condition — capture both regions at several points before treating it as a defect
