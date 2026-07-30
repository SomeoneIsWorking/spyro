---
id: C134
kind: claim
status: holds
created: 2026-07-30
tags: render,widescreen,milestone
---

## Claim

Widescreen is visually correct at 16:9: the 684-wide frame is centred and shows more world than 4:3, with one known defect (uncleared VRAM in the rightmost ~34 columns, issue 0039).

## Evidence

Confirmed by LOOKING at the pixels, which C131 had not done — it asserted "recovers real geometry" from a prim count, and a prim count is not a picture. At 16:9 the 684-wide framebuffer at frame 46501 is correctly centred (the DEMO MODE caption centred, Spyro mid-frame) and shows visibly more world on the left than the 512-wide 4:3 capture of the same frame. Files: scratch/screenshots/aspect_43.png, wide684.png.

One defect remains and is filed as issue 0039: the rightmost ~34 columns show uncleared texture-atlas VRAM, because only one of six clip-bound renderers is widened so nothing draws there and the band is never cleared to background.

NEARLY MISFILED AS A RENDERER BUG. tools/shot.py cropped to a hardcoded 512, so the first 16:9 capture showed the left 512 of a 684-wide picture — indistinguishable from a shifted, cropped, broken render. Checking the full VRAM before believing it is what avoided recording a false conclusion against working code. Fixed (--width), and it is the third capture path here to mislead the same way after I008, I032 and I033.

## What would falsify it

geometry appearing that should be culled, the frame losing its centring, or the wide band showing content from the WRONG scene — and note this is one renderer of six, so most of the frame's geometry is still clipped to 512
