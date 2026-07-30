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

### Note (2026-07-30)
CAUSE NARROWED, and one proposed fix RULED OUT before implementing it.

THE OBVIOUS FIX IS FORBIDDEN, and the framework says so in its own comment. I had proposed "clear the wide band each frame". gpu_native.cpp's display-blank carries an explicit warning against exactly that: VRAM columns beyond the 4:3 width at the display Y are NOT framebuffer, they are the TEXTURE ATLAS (object textures and CLUTs), and a previous change that widened this clear "ZEROED the atlas — corrupting every object whose texture lives there", visible only when the game STARTED in widescreen. The margin is the RENDERER's job at present time, never a guest-VRAM clear. Reading before implementing is what caught this; the fix I filed last tick would have reintroduced a reverted bug.

A REAL BUG FOUND AND FIXED ON THE WAY (psxport): the GP0 E4 handler widened the draw-area right clip by (wide_width - 320). Hardcoded 320 again — for this 512-wide game it over-extends the draw area 192 columns past anything the renderer draws. Now relative to the live display width, identical at 320. It was NOT the cause of the strip, which persists.

WHERE THE STRIP ACTUALLY IS, measured rather than eyeballed: per-80-column colour variety across the 684-wide frame reads 50 / 139 / 176 / 258 / 360 / 893 / 709 / 564 / 429 — smooth, with no atlas-like spike, yet the image still shows noise in roughly the last 30 columns at TOP and BOTTOM only. Those are precisely the regions with no geometry coverage: sky above the horizon and ground below it. The 3D terrain that the owned renderer widened DOES reach into the band; what does not is the 2D BACKDROP.

SO THE CAUSE IS THE 2D BACKDROP NOT BEING STRETCHED to the wide width, leaving uncovered columns that present samples as atlas. psxport already has machinery for this — gpu_native.cpp widens a full-screen 2D backdrop when gpu_vk_wide_engine() && (s_prev_had3d || s_prev_had_bg2d) — so the next question is why that is not firing for this game. Worth checking whether s_prev_had3d is the gate that fails: it is set from s_seen3d, which requires a primitive to resolve per-vertex DEPTH, and depth coverage here is 2.5% (C128). If so, widescreen's backdrop stretch is silently coupled to native-depth coverage, which would be worth recording in its own right.

A TOOL TRAP HIT TWICE IN ONE TICK, both in tools/shot.py, both now fixed: it cropped to a hardcoded 512 (so a 684-wide frame read as shifted and broken — nearly filed against working code), and its buffer-choice heuristic scored colour variety over the FULL requested width, letting the highly-varied atlas columns decide which framebuffer "has the frame" and pick the wrong one. It now scores only the 4:3 columns, which are always framebuffer. Fourth and fifth instances of this project's recurring pattern: the instrument samples differently than the thing it claims to show.
