---
id: 80
title: Native stage-13 guard bands expose preserved guest texture atlas
status: resolved
symptom: At 16:9 the fully native Spyro title scene has noisy multicolor rows at y=0..7 and y=232..239 even though the reference scene and fps60 painter draw-area selftest are clean
tags: render,widescreen,native,presentation,reported
created: 2026-08-22
updated: 2026-08-24
---

## Root cause

Spyro's legacy `preserveVramBackdrop` is one immutable `GameConfig` bit for two different frame
owners. It must be true while upload-only boot logos make guest VRAM the picture, but the fully
native stage-13 renderer owns its whole picture and needs an initially black presentation base.
Keeping the boot value during native frames exposes unrelated texture-atlas rows in the undrawn
guard bands. The renderer also did not have a typed per-frame ownership transition to invalidate a
composite built under the previous policy.

## What was tried / dead ends


## Resolution

The Spyro consumer now owns the per-Game transition state and publishes it at its reference/native
frame seams. Final resolution awaits the framework's transition-aware runtime policy and a real
pixel verification after the derived Spyro 1 override is wired.

### Note (2026-08-22)
Fresh d2266f4b measurement: native f701 has 671/741 colors and 4047/3937 non-black pixels in x0..511 across y0..7/y232..239; the same rebuilt binary with only legacy GameConfig::preserveVramBackdrop changed 1->0 has exactly one color (black) and zero non-black pixels in both bands. Therefore current corruption is raw guest VRAM preserved beneath a fully native frame, not the prior authored-painter clip spill. The static flag must remain true for upload-only boot logos (C099), so setting it to zero globally is a rejected stopgap. Correct owner is a runtime/per-frame policy: guest upload/reference frames preserve VRAM; fully native stage-13 frames clear their presentation base. Separately observed but not causal: each native logic frame is captured at flush ordinals 0,1,2 by the frame flush plus two deferred VBlank field flushes; the preserve toggle alone removes the bands despite that duplication.

### Note (2026-08-22)
Native deferred-field flush ownership was also corrected in game/core/vsync.cpp: deliver_field now flushes only when it presents immediately. A fresh painterplan run changed one logic frame from three captured copies at flush ordinals 0/1/2 to one capture at ordinal 0 (logged twice only because fps60 emits interp+real presents). The guard bands remain corrupted with preserveVramBackdrop=1, confirming duplicate capture was independent debt rather than the screenshot root cause.

### Note (2026-08-24)
Spyro consumer ownership is now explicit: a per-Game SpyroPresentationOwner defaults to guest VRAM for upload-only boot, and render_frame publishes guest/native ownership before the matching frame can present. A pure both-answer test covers default and transitions. Runtime override remains deliberately unwired until psxport's guestVramIsPicture policy forces rebuilds on true<->false transitions; reading RenderMode alone is invalid because native is configured during the guest-upload boot logos too.

### Resolution (2026-08-24)
Resolved with psxport bc8c8897 and Spyro 1 title-owned dynamic picture ownership. SpyroPresentationOwner defaults to guest VRAM for upload-only boot, publishes guest/native ownership before each matching present, and Spyro1Runtime exposes it through guestVramIsPicture(const Game&). Fresh shipping-path PIDs 548759/545600/546633 exited 0: standard presents 30/300 render the SCEA and Universal uploads (252/16216 colors), while native stage-13 present 700 is real at 16:9 (2738 colors) and 4:3 (2961 colors); every scaled top/bottom eight-row guard-band crop is exactly one color black with mean 0. Commands, hashes, logs and absolute artifacts: scratch/screenshots/spyro-vram-policy-20260824/capture-metadata.txt.
