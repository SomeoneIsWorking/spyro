---
id: 55
title: PSXPORT_RENDER_PATH=psx presented a fully black frame while the software rasterizer was drawing correctly
status: resolved
symptom: render path psx presents 0.0% non-black / 1 colour at every present, while a direct s_vram capture of the same run is 81.5% non-black / 2117 colours
tags: gpu,framework,render-path,present,soft-gpu
created: 2026-08-11
updated: 2026-08-11
---

## What it looked like

Headless, `PSXPORT_RENDER_PATH=psx`, `PSXPORT_PRESENT_SHOT_AT=700,1200,2010`, read with
`tools/ppm_look.py`:

    present  700   0.00% non-black   1 colour
    present 1200   0.00%             1
    present 2010   0.00%             1

The SAME run, `shot` at present 700 (which now routes by rasterizer, see below):

    psx_direct.ppm  512x240   81.5% non-black   2117 colours   <- a real picture

So the software rasterizer was drawing the frame into `s_vram` and the PRESENT was discarding it.
Two captures from one run, one showing content and one not, is what separated "renders nothing" from
"presents nothing" — the reason to have both instruments.

## Root cause — ONE blindness, TWO consumers

The VK present learns that the framebuffer changed ONLY through `gpu_vk_dirty()`, and every call
site of it in `gpu_native.cpp` is gated `if (vk_path())`. On `RenderPath::Psx` that is false, so:

1. `present_rebuild_decision` (gpu_vk_present_policy.h) saw `batchEmpty=true` — the software path
   never tees a primitive to VK, so it is permanently true — and a write counter that never moves.
   It returned `PRESENT_REUSE_LAST` for the entire run, re-showing a composite that had never been
   built once. Both of its inputs are structurally pinned on this path.
2. Fixing only that STILL presented black: the upload takes the dirty RECT list, which is empty for
   the same reason, so a rebuild uploaded nothing. Measured — do not assume the first fix was the
   whole fix.

## Fix (psxport e16c58dc)

Third input `swRasterIsPicture` to the decision (checked first; it does NOT consult
`preserveVramBackdrop`, which answers "is the GUEST's VRAM the picture" — here the picture is in
`s_vram` because we rasterized it there), and `s_dirty.markAll()` at the present when `sw_path()`.
Not a heuristic: on that path we drew every pixel of that buffer ourselves.

Also fixed alongside, same class of bug: the REPL `shot` picker asked `gpu_vk_enabled()` ("is the VK
backend up") instead of "which rasterizer drew this frame", so under `psx` it captured the empty VK
image and reported it as the shot. It routes through `GpuState::gpu_native_shot` now and names the
render path in its log line.

## Verified

Same command, after: 81.7%/2166, 93.3%/3737, 93.3%/3508 at presents 700/1200/2010 — and the colour
counts DIFFER from the `gte` leg's 2008/3534/3284 at the same frames, which is what proves the
capture is the software rasterizer's own output rather than the VK picture leaking through. A
matching pair of counts would have meant the instrument was still reading the wrong buffer.

Regression gate: 4 new cases in `psxport/tests/test_present_empty_batch.cpp`, asserting BOTH
directions — with the new input false, every pre-existing decision is bit-identical, so the fix
cannot degrade into "always rebuild".
