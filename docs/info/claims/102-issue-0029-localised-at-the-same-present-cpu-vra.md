---
id: C102
kind: claim
status: holds
created: 2026-07-29
tags: gpu,framework
---

## Claim

Issue 0029 localised: at the SAME present, CPU VRAM holds 5256 non-zero pixels while the GPU texture reads 0/524288 — the content is lost between upload_vram and readback_vram inside one present.

## Evidence

PSXPORT_GPU_TRACE with a REPL shotregion at present ~300: 'present #200 src nonzero=5256/524288' immediately followed by 'readback nonzero=0/524288'. Both counters cover the full 1024x512, so they are directly comparable. This rules out, definitively, the three explanations that consumed the previous attempts: the content IS in CPU VRAM at that moment, the frame axes are irrelevant because both numbers come from the same present, and buffer choice is irrelevant because the readback counts the WHOLE texture, not a display region. With preserveVramBackdrop=1 band 1 no longer clears, so the loss is elsewhere in the pass chain — the float-RGBA semi intermediate (s_color_rgba) and whatever composites it back onto C are the untested candidates.

## What would falsify it

if a readback taken immediately after upload_vram (before render_geom) also reads 0, the upload itself never lands and the pass chain is innocent
