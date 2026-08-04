---
id: 29
title: preserveVramBackdrop does not make the upload-only logo screens appear — cause unknown
status: resolved
symptom: with GameConfig::preserveVramBackdrop=1 (band 1 no longer clears to black), the SCE/Universal logo screens STILL read 0.0% non-black / 1 colour in both display buffers
tags: gpu,framework
created: 2026-07-29
updated: 2026-08-04
---

The change is sound and does what it says at the code level; it just does not produce the expected picture, and I could not find out why.

VERIFIED, so these are all ruled out:
  * The flag reaches the renderer — a one-shot log printed 'render_geom: preserveVramBackdrop=1'.
  * Internal resolution is NOT the cause: ires_i=1 scale=1 across 35597 presents, so C IS s_vram_tex ('already holds the uploaded VRAM' per the code's own comment), not the s_ires_color composite.
  * The upload is unconditional and its source is right: present_window() -> blit_src(s_vram, ...) -> gpu_vk_present -> upload_vram(cmd, src) with src = the CPU VRAM.
  * The content IS in CPU VRAM at those presents, in THIS build: GPU_DUMP f00120 has 1019 colours (SCE) and f00300 has 14357 (Universal).
  * The frame axes DO line up this time — GPU_DUMP indexes presents and the REPL's 'run N' advances one present per iteration; I checked rather than assumed, having got exactly this wrong earlier (C098).
  * BOTH display buffers were captured, (0,0) and (0,240). An earlier round of this investigation was fooled by capturing only one.
  * 3D rendering is unaffected — gameplay frames still come back with ~1900 colours, so preserving the backdrop has not left stale content under the world.

SO THE REMAINING GAP is between 'upload_vram writes CPU VRAM into s_vram_tex' and 'readback_vram reads s_vram_tex and sees one colour', with band 1 no longer clearing. Something in between still discards it, or the readback and the upload are not looking at the same texture instance (note GpuVkState is per-Game).

The config field is KEPT and committed on its own merits — the policy belongs to the consumer, not hardcoded — but it must NOT be described as fixing this issue, because it does not.

### Note (2026-07-29)
LOCALISED to a single present. PSXPORT_GPU_TRACE plus a REPL shotregion gives, back to back:

  [gpu_vk] present #200 src nonzero=5256/524288    <- CPU VRAM HAS the content
  [gpu_vk] readback nonzero=0/524288               <- GPU texture is EMPTY

Both counters span the full 1024x512, so they compare directly. That kills the three explanations the earlier attempts kept circling:
  * 'the content is not in CPU VRAM at that moment' — it is, 5256 non-zero.
  * 'the frame axes do not line up' — irrelevant, both numbers come from the same present.
  * 'you captured the wrong display buffer' — irrelevant, the readback counts the WHOLE texture.

So the loss happens between upload_vram() writing s_vram_tex and readback_vram() reading it, within one present, with band 1 no longer clearing (preserveVramBackdrop=1 is confirmed reaching render_geom).

UNTESTED CANDIDATES, in order: the float-RGBA semi intermediate (s_color_rgba) and whatever composites it back onto C; the depth/stencil target setup; and the possibility that the readback's own command buffer runs before the present's has been submitted, though present submits at its end.

NEXT MEASUREMENT, which settles the falsifier: read back immediately AFTER upload_vram and before render_geom. If that reads 0 too, the upload never lands and the pass chain is innocent — which would point at the transfer buffer or the texture instance instead.

### Note (2026-07-29)
BISECTED to render_geom's SETUP, not its render passes. Two runs, same REPL script, same present:

  PSXPORT_NO_GEOM=1  (render_geom not called at all)   -> readback nonzero=50254/524288
  PSXPORT_NO_BANDS=1 (setup runs, band passes skipped) -> readback nonzero=0/524288

So the upload DOES land in s_vram_tex, and something in render_geom BEFORE the first band discards it.

RULED OUT inside that region, by reading the code rather than more runs:
  * ensure_ires_targets — idempotent ("if (s_ires_scale == i) return"), and at i<=1 it releases nothing and returns immediately.
  * the ires downsample that blits C back over s_vram_tex — guarded by "if (ires)", and ires is 1.
  * the DONT_CARE composite-back onto colorTgt — sits after "if (!semiTotal) return", and semiTotal is 0 on these screens (batch tri=0 tex=0 semi=0).
  * the three band passes themselves — Pass A uses LOAD when preserveVramBackdrop is set, and the bisect above exonerates them anyway.

WHAT IS LEFT in the setup: gpu_vk_video_status(&g.game->core, ...) and the copy pass that uploads s_vram_snap and the vertex buffers.

NEXT: skip only the copy pass. If the backdrop survives, it is that upload; if not, gpu_vk_video_status is touching the targets. Either answer is one run away.

NOTE ON METHOD: this was found by bisecting the function with two temporary env-gated skips, after four rounds of capture-and-infer had produced three wrong conclusions. Bisection asked a question the measurement could answer; the captures kept asking questions that needed interpretation.

### Resolution (2026-07-29)
render_geom's 'total == 0' early return in external/psxport/runtime/recomp/gpu_vk.cpp cleared s_vram_tex to black unconditionally. Upload-only screens submit zero primitives by definition, so they always took that branch — which sits ABOVE every other backdrop control in the function, and is why GameConfig::preserveVramBackdrop (added for exactly these frames, C100) never reached them. That branch now honours preserveBackdrop and skips the clear, leaving s_vram_tex holding the frame's uploaded VRAM; s_present_ires is already 0 there, so present() samples the native texture either way. Verified on ONE binary: readback nonzero 0 -> 50254/524288, matching the PSXPORT_NO_GEOM control that skips render_geom entirely; ppm_look reports 512x240, 36.1% non-black, 14357 colours (a real frame, not a flat field); gate 14/14 PASS. See C104. C103 ('bisected to render_geom's SETUP') is FALSIFIED: on an upload-only frame the run never reaches setup, so the NO_BANDS and NO_COPY gates both sat downstream of the culprit and could never fire — which is exactly why they read identical, and reading that shared 0 as evidence about the setup was the error. Those two gates were also compared across different builds. NOTE: these screens are 24bpp and the depth bit is decoded but not honoured (C097), so they will now appear but be mis-coloured and two-thirds wide until that is handled.

### Note (2026-08-04)
FORWARD POINTER (2026-08-04): this exact assumption came back one level up. afca817d added an empty-batch early-out to GpuVkState::present() ABOVE upload_vram, so an upload-only screen stopped reaching render_geom at all and the preserveVramBackdrop control this issue added could no longer be consulted. Same black screens, different line. See issue 0043 and C149.
