---
id: 29
title: preserveVramBackdrop does not make the upload-only logo screens appear — cause unknown
status: open
symptom: with GameConfig::preserveVramBackdrop=1 (band 1 no longer clears to black), the SCE/Universal logo screens STILL read 0.0% non-black / 1 colour in both display buffers
tags: gpu,framework
created: 2026-07-29
updated: 2026-07-29
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
