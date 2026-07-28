---
id: 16
title: Rendered logo screens show colour speckling and horizontal truncation
status: open
symptom: The SCE 'SONY COMPUTER ENTERTAINMENT presents' and Universal Interactive logo screens render as rainbow-speckled noise where the text should be solid white/coloured, and the image runs off the right edge of the 512x240 dump.
tags: gpu,render
created: 2026-07-28
updated: 2026-07-28
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
