---
id: C104
kind: claim
status: holds
created: 2026-07-29
tags: render,gpu_vk
---

## Claim

Issue 0029 root cause: render_geom's 'total == 0' early return cleared s_vram_tex to BLACK unconditionally, ignoring GameConfig::preserveVramBackdrop. An upload-only screen (loading screen, fade, static art blitted straight into VRAM) submits zero primitives by definition, so it took that branch and had its uploaded backdrop wiped ABOVE every other backdrop control in the function — which is why preserving the backdrop at band 1 (C100) could never take effect on the very frames it was added for. Fix: honour preserveBackdrop in that branch.

## Evidence

Same binary, headless, 'run 300' + shotregion. Before: readback nonzero=0/524288. After: 50254/524288, identical to the PSXPORT_NO_GEOM control that skips render_geom entirely. Frame is real, not a flat field: ppm_look.py reports 512x240, 36.1% non-black, 14357 distinct colours. Full gate 14/14 PASS (divergences 0, native bodies verified 120, overlays 7, arena UNMATCHED 0).

## What would falsify it

A game sets preserveVramBackdrop=1 and reports stale VRAM persisting across frames that legitimately have nothing to draw; or a native-renderer-owned port (preserveVramBackdrop=0) starts showing raw PSX VRAM on empty frames.
