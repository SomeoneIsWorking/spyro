---
id: 43
title: Boot logo screens black AGAIN — the empty-batch present early-out re-introduced issue 0029 one level up
status: resolved
symptom: Spyro boots to a black screen where the SCE and Universal logos should be; presents 30-420 read 0.0% non-black / 1 colour, real frames only from ~600
tags: gpu,framework,regression,boot
created: 2026-08-04
updated: 2026-08-04
---

## What it looked like

Ten presented frames via PSXPORT_SHOT_AT, read with tools/ppm_look.py:

    frame  30   0.00% non-black   1 colour
    frame  60   0.00%             1
    frame 120   0.00%             1
    frame 200   0.00%             1
    frame 300   0.00%             1
    frame 420   0.00%             1
    frame 600  93.33%          2117   <- first real picture
    frame 900  93.33%          2120

The same reader produced both 1 colour and 2117 colours in one run, so the zeros are real and not a
broken instrument.

## The trap this issue exists to stop

C099 said, with status `holds`, that the cause was band 1's unconditional `clearColorBlack`. That was
TRUE WHEN WRITTEN and FALSE by the time it was read: C104 (2026-07-29) had already made render_geom's
`total == 0` early return honour `GameConfig::preserveVramBackdrop`, and C105 had made the 24bpp
display depth work, and both logo screens were verified rendering correctly at that point.

Reading C099 as live led straight to the conclusion that the framework fix had never been attempted
and that the choice was between re-doing it and writing a native producer for the logo screens. Both
were wrong. The correct first move was `catalog.py search "black screen"`, which surfaces issue 0029
and its resolution immediately. C099 is now falsified.

## Actual cause

`afca817d` — "gpu: a present with no new geometry re-shows the last composite" — added to
`GpuVkState::present()`:

    if (geom_batch_empty(*this)) { show_composite(cmd); return; }

It sits ABOVE `upload_vram()` and `render_geom()`. It was added for a real bug (a 30fps guest's idle
field rebuilt a black composite, giving Spider-Man a measured 0.0%/99.4% alternation), and its
hardware analogy is right: a display re-scans the same framebuffer when nothing has drawn.

But "the geometry batch is empty" is not "the guest produced nothing". A port still running the
guest's own drawing has a SECOND producer: a direct framebuffer write. An upload-only screen — a logo
still, a loading screen, a fade — is DMA'd into VRAM with zero primitives. It satisfies the early-out
while being an entirely new picture. So the composite was never built at all and the screen showed
black for its whole duration.

That is exactly issue 0029's assumption ("zero prims means nothing to show"), re-introduced one level
UP, in the one place where the `preserveVramBackdrop` control added to fix 0029 could no longer be
reached. The comment on the early-out even considers `preserveVramBackdrop` and dismisses it — correctly
for a native-producer port, and the reasoning was never re-checked for a guest-drawing one.

## Fix

`present_rebuild_decision()` in the new `runtime/recomp/gpu_vk_present_policy.h`: rebuild when EITHER
producer changed — primitives submitted, or (for a port whose `preserveVramBackdrop` says guest VRAM
IS the picture) guest VRAM written since the composite was built. The write signal is a counter on the
existing `gpu_vk_dirty()` chokepoint, which every CPU->VRAM path already calls and which was a no-op.

The `preserveVramBackdrop` gate is not blast-radius trimming: for a native-producer port render_geom
clears an empty batch to black, so rebuilding on a VRAM write would composite black OVER a good frame.
A guest VRAM write is new picture content exactly when guest VRAM is the picture.

## Verified

AFTER, same command: 30/60/120/200 = 2.7% non-black / 252 colours (the SCE card, legible in the PNG);
300/420 = 27.2% / 16216 (the Universal globe, full width, correct colour); 600/900 unchanged at 93.3%
/ 2117 / 2120. tools/gate.sh 90: 16/16 PASS, 49817 frames. Hermetic test
`external/psxport/tests/test_present_empty_batch.cpp`, shown RED (3/7, exactly the upload-only cases)
against a transcription of the pre-fix rule, then 10/10 green.

New instrument `PSXPORT_DEBUG=presentskip` tallies the decision with its denominator, so the same
question can be asked of any port in one run.
