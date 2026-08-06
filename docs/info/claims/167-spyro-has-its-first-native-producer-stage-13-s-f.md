---
id: C167
kind: claim
status: holds
created: 2026-08-06
tags: render,producer,native-graphics,stage13
depends: game/render/fx_title_menu.cpp, game/render/frame_env.cpp, game/render/render_frame.cpp
---

## Claim

SPYRO HAS ITS FIRST NATIVE PRODUCER: stage 13's front-end sprite layer (the SPYRO THE DRAGON logo + the pulsing PRESS START strip) is drawn entirely from game state on the native render leg, with no gen_func_* body making the picture.

## Evidence

Producer game/render/fx_title_menu.cpp, a port of guest 0x8007CD38 driven by 0x8007CEE4's own state machine (Ghidra headless over scratch/raw/snap_title_3000.bin; OV_5B800 confirmed resident 256/256 by tools/whatis.py). A/B on ONE binary, ONE line (an early return in spriteEmit), SAME 24 consecutive presents 2000..2023 inside the producer's own active window, headless: ENABLED -> 269-283 distinct colours, the logo and PRESS START on screen (scratch/shots/gateA); DISABLED -> 1 distinct colour, 24/24 flat black (scratch/shots/gateB). The in-band signal separates the legs: titlefx reports emitted=2 on 939 frames / 1 on 14 / 0 on 602 enabled, and emitted=0 on all 1555 frames disabled. The seam STILL ABORTS for the next unimplemented scene (stage 13 with [0x80078D78]==3, handler 0x8001E6B8).

## What would falsify it

the producer drawing anything when the guest's own arm would not, or the picture differing from the reference leg's stage-13 sprite layer in position, size, mirroring or modulation colour — neither has been diffed against the reference leg pixel-for-pixel, only inspected
