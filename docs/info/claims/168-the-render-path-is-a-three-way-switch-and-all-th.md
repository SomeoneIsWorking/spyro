---
id: C168
kind: claim
status: holds
created: 2026-08-11
tags: render,render-path,gpu,tri-state
depends: external/psxport/runtime/recomp/render_mode.h#RenderPath, external/psxport/runtime/recomp/render_path.cpp#render_path_install, external/psxport/runtime/recomp/gpu_vk_present_policy.h#present_rebuild_decision
---

## Claim

The render path is a three-way switch and all three paths present a real picture in this port

## Evidence

One binary, one disc, headless, PSXPORT_PRESENT_SHOT_AT=700,1200,2010, read with tools/ppm_look.py. native: 93.2/93.3/93.3% non-black, 1990/3515/2844 colours. gte: 93.3% x3, 2008/3534/3284. psx (PSX software rasterizer into s_vram): 81.7/93.3/93.3%, 2166/3737/3508. Each run's own [render] announce line names the path, the geometry source, the rasterizer and whether enhancements were locked out, so no capture is attributed by assumption. psx was 0.0%/1 colour before psxport e16c58dc (issue 0055) and the colour counts now differ from gte's at every frame, which is what distinguishes the software rasterizer's output from the VK picture.

## What would falsify it

native and gte producing IDENTICAL colour counts at every frame would mean this run never exercised a native producer (with PSXPORT_SPYRO_FRAME_LOOP unset the picture comes from the guest's own driver on BOTH, so these numbers do NOT prove the native leg drew anything); psx matching gte exactly would mean the capture is still reading the VK image; and any of the three presenting 0.0% non-black again means a present-path regression
