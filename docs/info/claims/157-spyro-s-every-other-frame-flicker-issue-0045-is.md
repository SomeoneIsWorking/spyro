---
id: C157
kind: claim
status: holds
created: 2026-08-06
tags: render,flicker,gpu,framework,user-reported
depends: external/psxport/runtime/recomp/gpu_vk.cpp#upload_vram
---

## Claim

Spyro's every-other-frame flicker (issue 0045) is the composite having NO PERSISTENCE: upload_vram() memcpy'd all 1024x512 of guest CPU VRAM over s_vram_tex on every present, and under vk_path() the guest's polygons never reach CPU VRAM — so every present erased the rasterized picture out of the buffer the double-buffered guest was about to display. Uploading only the regions gpu_vk_dirty() reports removes the flicker completely.

## Evidence

WINDOWED, PSXPORT_PAD_REPLAY=replays/bugs/flicker-session.pad, 20 CONSECUTIVE presents 2200..2219 via PSXPORT_PRESENT_SHOT_AT into a per-run dir, measured by DISTINCT COLOUR COUNT (tools/ppm_look.py). A/B on ONE tree, ONE binary, one line toggled. CONTROL (whole-canvas upload): 3169,2,3158,2,3171,2,3156,2,3174,2,3140,2,3171,2,3179,2,3160,2,3149,2 — 10 of 20 presents are a solid 2-colour fill (0xffdead, the guest's clear). FIX (dirty-rect upload): 3169,3169,3158,3158,3171,3171,3156,3156,3174,3174,3140,3140,3171,3171,3179,3179,3160,3160,3149,3149 — 0 of 20 flat. Scene presents are md5-IDENTICAL across the two arms. Mechanism proved before the fix: PSXPORT_DEBUG=rqflush shows the queue y-ranges alternating y=[-93..332] / y=[147..572] (the two buffers, 240 apart), and DELETING RenderQueue::flush's deferred-reset re-emit alone turned 20/20 presents flat. presentskip shows all 20 are PRESENT_REBUILD_GEOM, so the empty-batch rule is not involved. Framework suite 19/19; headless boot 111489 presents.

## What would falsify it

if a present is ever seen whose displayed band lost geometry the guest did not overwrite — i.e. any return of a 2-colour present in a >=20-consecutive-present distinct-colour capture, windowed
