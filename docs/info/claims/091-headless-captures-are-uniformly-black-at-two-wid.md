---
id: C091
kind: claim
status: holds
created: 2026-07-29
tags: gpu,instrument
---

## Claim

Headless captures are uniformly black at two widely separated points, with fade, region and 'blank moment' all ruled out — so the rendered content is not in s_vram_tex at readback time.

## Evidence

Six frames at guest frame 900 and six at 4000: every one is 512x240 with exactly ONE distinct colour. Ruled out: the fade (dump_to's fade_mode 0 takes neither the add nor the subtract branch, so the null-safe default is a genuine no-op), the region (s_last_w/h read 512x240, matching the display env of C068), and a blank moment (two capture points ~3100 frames apart, and the gate reports 2542 frames submitting prims in the last quarter). Meanwhile the CPU-side s_vram demonstrably varies — the gate's 21 distinct frame occupancies come from it. Uniform output is the broken-instrument tell, so this is recorded as an instrument limitation, NOT as 'the port renders black'.

## What would falsify it

capturing via the s_ires_color composite path, or after forcing s_present_ires, showing non-black content would confirm the source selector rather than the readback is at fault
