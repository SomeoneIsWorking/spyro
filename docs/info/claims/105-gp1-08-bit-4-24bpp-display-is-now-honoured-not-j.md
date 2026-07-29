---
id: C105
kind: claim
status: holds
created: 2026-07-29
tags: render,gpu,gpu_vk
---

## Claim

GP1(08) bit 4 (24bpp display) is now HONOURED, not just decoded. 24bpp packs RGB888 across 1.5 VRAM halfwords per pixel, so display column x lives at BYTE offset disp_x*2 + x*3 in the row rather than halfword disp_x+x. Both decoders of the display region were fixed: the present shader (present.frag, new pc.fmt.x uniform + a vram_byte helper over the RG8 texture) and the CPU shot/readback (dump_to). The bit crosses from gpu_native's GP1 decode to gpu_vk via gpu_vk_set_display_depth(), because present()'s signature carries the display RECT but not its FORMAT. A scaled (ires>1) present is never 24bpp — that composite is rendered by our own raster in 1555.

## Evidence

Spyro sets GP1(08)=08000012 (bit4=1, 512x240) at frame 1 and clears it at frame 436, so the whole logo sequence is 24bpp. Same frame (300) captured before and after: BEFORE the Universal Interactive Studios logo is rainbow-scrambled and occupies only the left two-thirds of the width; AFTER it renders correctly — silver UNIVERSAL wordmark over the Earth globe, correct colours, full 512 width, legible 'www.universalstudios.com' and copyright lines. Both images inspected directly, not judged by statistics (ppm_look calls both 'real frame': 36.1%/14357 colours vs 27.2%/16216 — that metric cannot tell correct from scrambled, which is why the pixels were looked at). Gate 14/14 PASS.

## What would falsify it

A 24bpp screen renders with a horizontal colour-phase shift (would mean the byte offset is off by one or two), or a 15-bit screen regresses (would mean the flag is latching when it should not).
