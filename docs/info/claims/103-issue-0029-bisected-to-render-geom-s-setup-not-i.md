---
id: C103
kind: claim
status: holds
created: 2026-07-29
tags: gpu,framework
---

## Claim

Issue 0029 bisected to render_geom's SETUP, not its render passes: skipping all of render_geom leaves the uploaded backdrop intact (50254 non-zero), skipping only the three band passes still loses it (0).

## Evidence

Two runs, same REPL script, same present: with PSXPORT_NO_GEOM=1 (render_geom not called) 'readback nonzero=50254/524288'; with PSXPORT_NO_BANDS=1 (setup runs, the three render_pass_set calls skipped) 'readback nonzero=0/524288'. So the loss happens in render_geom before any band renders. Ruled out by code inspection within that region: ensure_ires_targets is idempotent ('if (s_ires_scale == i) return', and at i<=1 it releases nothing and returns), and the ires downsample and the DONT_CARE composite-back are both guarded — the former by 'if (ires)', the latter by 'if (!semiTotal) return' with semiTotal 0 on these screens. What remains in the setup is gpu_vk_video_status and the copy pass that uploads s_vram_snap plus the vertex buffers.

## What would falsify it

if skipping only the copy pass restores the backdrop, it is that; if not, gpu_vk_video_status is doing something to the targets
