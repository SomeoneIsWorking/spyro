---
id: 39
title: Widescreen: the wide band beyond 512 shows uncleared texture-atlas VRAM
status: open
symptom: At 16:9 the 684-wide framebuffer renders correctly and centred with more world visible on the left, but the rightmost ~34 columns show raw texture-atlas VRAM rather than scene content.
tags: gpu,widescreen
created: 2026-07-30
updated: 2026-07-30
---

VISUALLY CONFIRMED that widescreen renders correctly and wider — this is the pixel verification C131 was missing, since a prim count is not a picture. At 16:9 the 684-wide framebuffer is correctly centred (DEMO MODE centred, Spyro mid-frame) and shows genuinely more world on the left than the 4:3 capture of the same frame. Side by side: scratch/screenshots/aspect_43.png (512) vs wide684.png (684).

THE DEFECT: the rightmost ~34 columns of the wide framebuffer show raw TEXTURE-ATLAS VRAM instead of scene content — uncleared memory, not geometry. psxport's own code already knows this hazard; gpu_native.cpp carries a comment about "raw VRAM texture-atlas garbage in the [320,nw) band. Off at 4:3".

WHY IT SHOWS NOW, and why it is expected rather than mysterious: only ONE of the six clip-bound renderers is owned and widened. Everything else still trivially rejects at 512, so the band from 512 to 684 receives terrain from the owned renderer and nothing else — and wherever nothing draws, the uncleared VRAM underneath shows through. The band is not being cleared to the scene's background across its full wide width.

TWO CANDIDATE FIXES, and they are not equivalent:
  (a) clear/fill the wide band each frame so undrawn columns are background rather than atlas garbage. Cheap, and correct regardless of how many renderers are owned.
  (b) own the remaining renderers so the band is actually drawn. That is the real fix for CONTENT, but it will not help the columns beyond where any renderer draws, so (a) is still needed.
Do (a) first: it is small, it is independent, and it makes every later widening visually assessable instead of masked by garbage.

A TOOL DEFECT FOUND THE SAME WAY, and it nearly cost a wrong conclusion: tools/shot.py cropped to a hardcoded 512. At 16:9 that shows the LEFT 512 of a 684-wide picture, which looks exactly like a shifted, cropped, broken render — I was one step from filing "widescreen is broken" against working code. It now takes --width and its help text says why. That is the third capture path in this project to mislead the same way (I008, I032, I033); the pattern is always "the instrument crops or samples differently than the thing it claims to show".
