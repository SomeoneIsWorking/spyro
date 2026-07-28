---
id: 16
title: Rendered logo screens show colour speckling and horizontal truncation
status: open
symptom: The SCE 'SONY COMPUTER ENTERTAINMENT presents' and Universal Interactive logo screens render as rainbow-speckled noise where the text should be solid white/coloured, and the image runs off the right edge of the 512x240 dump.
tags: gpu,render
created: 2026-07-28
updated: 2026-07-29
---

Observed in scratch/gate/frames/f00100.ppm (SCE) and f00250/f00400.ppm (Universal). The content is
clearly correct — the words are legible and correctly positioned — so this is not a geometry or
addressing failure. Two separate defects:

  1. COLOUR: solid text renders as per-pixel rainbow speckle. Suggests a pixel-format or CLUT decode
     mismatch on a VRAM upload path, not a rasteriser bug — these screens are uploaded via dma2, with
     zero prims submitted (see issue 0015).
  2. GEOMETRY: 'SONY COMPUTER ENTERTAINM...' and 'UNIVERSAL INTE...' are cut off at the right edge.
     The display is reported as 512x240 @ (0,0)/(0,240). Either the game is in a wider mode than the
     dump assumes, or the display-area origin/width is being applied wrongly.

NOT YET INVESTIGATED — recorded so it is not lost, and kept separate from the frame-3781 crash
(issue 0015) which is the current blocker. Do not assume a common cause; they may well be unrelated,
and the crash is on the OT path while these screens use no OT at all.

Worth checking whether the reference consumer (Tomba!2) shows the same colour behaviour on its own
upload path — if it does, this is a framework defect rather than a Spyro one.

### Note (2026-07-29)
ROOT CAUSE FOUND — one bug, not two. The game switches the display to 24-BIT COLOUR for these screens and the framework never decoded GP1(0x08) bit 4.

Verified rather than inferred: adding the decode plus a report gives 'display depth -> 24-BIT (GP1(08)=08000012, 512x240)' during the logo screens and '-> 15-bit (GP1(08)=08000002)' after them.

BOTH recorded symptoms are consequences of that single mismatch:
  * COLOUR: 24bpp packed bytes read as 15bpp halfwords scrambles every pixel — the rainbow speckle.
  * GEOMETRY: 24bpp packs 1.5 halfwords per pixel, so 512 halfwords cover only ~341 of 512 pixels. The image is cut at about two thirds across, which is exactly where 'UNIVERS|AL' stops.

The issue's own hypothesis — 'a pixel-format or CLUT decode mismatch on a VRAM upload path' — was close: it is a pixel-format mismatch, but at the DISPLAY end rather than the upload end, and it explains the truncation too, which a CLUT theory does not.

Its suggestion to check whether the reference consumer shows the same behaviour is now unnecessary: this is a framework gap (an undecoded GP1 bit) and would affect ANY game that uses 24bpp stills, which is most PS1 games with pre-rendered logo screens or FMV.

DONE SO FAR: the bit is decoded and reported (psxport). NOT DONE: honouring it in the readback and present paths, which is the actual fix and is a larger change — the readback assumes 15bpp throughout.

NOTE ON THE EVIDENCE: the frames this issue was filed from are PSXPORT_GPU_DUMP output, which reads CPU-side s_vram and misses the VK renderer entirely (I008). The VK readback is now working (issue 0018) and is the right observer for re-checking this once the depth is honoured.

### Note (2026-07-29)
MECHANISM NOW COMPLETE, and there are TWO separate faults on these screens, not one.

FAULT A (colour + truncation, C097): the game switches the display to 24bpp and GP1(08) bit 4 was decoded nowhere, so 24-bit VRAM is read as 15-bit. That explains the rainbow speckle AND the two-thirds cut. Now decoded and reported upstream; not yet honoured.

FAULT B (the screens are not displayed at all, C099): psxport's render_geom clears the colour target to BLACK in band 1 — 'the PC renderer shows ONLY what a native producer submitted' — before drawing the geometry bands. The uploaded PSX VRAM backdrop is discarded every present. These logo screens are uploads with ZERO primitives, so nothing survives the clear and the port shows black.

Measured, with the present index pinned from the GPU_DUMP series rather than assumed: presents 25-200 hold the SCE logo, 225-425 the Universal logo, and from 450 the guest is submitting primitives. Across 25-425 the VK readback reports 1 distinct colour in both display buffers; the GPU_DUMP frames from the same presents contain the artwork because they read CPU s_vram BEFORE the wipe.

So the speckle recorded in this issue was never what a player would see. It is what the CPU-side buffer contains, viewed through an observer that also gets the depth wrong. On screen these frames are black.

FAULT B is a framework/consumer mismatch rather than a defect: the black clear is correct for a port whose native producer owns rendering. Spyro still runs the guest's own drawing, and for these screens that means uploads.
