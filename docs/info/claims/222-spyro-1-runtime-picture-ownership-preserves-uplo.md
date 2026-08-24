---
id: C222
kind: claim
status: holds
created: 2026-08-24
tags: render,widescreen,presentation
depends: titles/spyro1/core/spyro1_runtime.cpp#Spyro1Runtime::guestVramIsPicture, game/render/presentation_owner.h#SpyroPresentationOwner, game/render/render_frame.cpp#SpyroRenderer::drawFrame, external/psxport/runtime/recomp/guest_vram_composite_policy.h#GuestVramCompositePolicy
reconfirmed: 2026-08-24 20:24:20
verified_at: 2026-08-24 20:24:20
---

## Claim

Spyro 1 runtime picture ownership preserves upload-only boot logos and clears the native stage-13 guard bands

## Evidence

Real SCUS_942.28, shipping launcher, app build 50c02b9-dirty+psxport-bc8c8897. PID 548759 present 30/300 rendered real 252/16216-color SCEA/Universal uploads. PIDs 545600/546633 classified present 700 as native stage 13 at 16:9/4:3 with 2738/2961 colors; each scaled top/bottom eight-row guard crop was exactly one color black with mean zero. Full commands, hashes, logs and images: scratch/screenshots/spyro-vram-policy-20260824/capture-metadata.txt.

## What would falsify it

If the same build/policy makes either boot upload disappear, produces a non-black stage-13 guard-band crop, or a capture log shows the tested frame was not native stage 13, this claim is falsified.

## Re-confirmed 2026-08-24 20:24:20

After 426c83b, tracked presentation-owner contents match the visually verified tree: SCEA and Universal uploads remained visible; root inspected matched native stage-13 16:9/4:3 captures whose measured guard crops are one-color black with mean zero.
