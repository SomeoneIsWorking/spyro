---
id: C191
kind: claim
status: holds
created: 2026-08-14
tags: render,paired-actor,fps60,interpolation
depends: game/render/fx_paired_actor.cpp#spyro_paired_actor_fps60_world_pass, game/render/paired_actor_decode.cpp#resolve_normal_faces_continuous, game/render/frame_env.cpp#nativeFrameEnd
---

## Claim

Spyro's reached 0x80023AC4 normal opaque/textured arm rebuilds both FPS60 presents from immutable
host recipes: exact guest-derived endpoint policy at t=0/1 and a deterministic continuous
source-primitive policy between ticks, on the current alternating draw page without advancing audio
or presentation on a separate field clock.

## Evidence

`scratch/logs/paired_temporal_paced.log` and `paired_temporal_audio_paced.log` both exit 0 after 4100
presented frames: 160/274 recipe pairs are compatible, all 160 midpoint resolves succeed, exactly
320 redirected passes run (160 midpoint + 160 endpoint), and framebuffer selection reports
`wrong_half=0/1830`. `paired_temporal_tforce0.log` and `paired_temporal_tforce1.log` each complete 320
exact endpoint passes with the same zero-mismatch page census. `PSXPORT_SELFTEST=pairedpose` passes 32
checks, including raw-view saturation, continuous subpixel NCLIP, quad split cells, continuous OT,
display-page selection, and current-destination endpoint remapping.

## What would falsify it

Any compatible live interval refuses or omits either present-time pass; t=0/1 differs from the exact
endpoint resolver after destination normalization; midpoint reads guest state, bypasses IR/near
clamping, or stops resolving the stable source stream; the paired group is duplicated or absent; the
selected display page differs from the current draw page; field present/pace/ack resumes inside the
delegated pair; or the named resolver, temporal hook, or frame-end policy changes without rerunning
the paced, audio-enabled, forced-endpoint, and hermetic discriminators.
