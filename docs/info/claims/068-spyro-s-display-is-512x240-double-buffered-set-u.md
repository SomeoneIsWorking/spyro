---
id: C068
kind: claim
status: holds
created: 2026-07-28
tags: gpu,frame,display
---

## Claim

Spyro's display is 512x240, double-buffered, set up at 0x800122D0-0x8001233C; the current-DRAWENV pointer [0x80075888] is the buffer selector and has exactly ONE writer, the flip at 0x8001ED5C.

## Evidence

Boot init calls 0x8005EA94 twice — (0x80076EE0, 0, 8, 512, 224) and (0x80076F64, 0, 248, 512, 224) — and 0x8005EB4C twice — (0x80076F3C, 0, 240, 512, 240) and (0x80076FC0, 0, 0, 512, 240). Exactly two calls each (whatis.py: direct jal count 2 for both) identifies them as SetDefDrawEnv / SetDefDispEnv and the arrangement as double buffering: buffer A draws at y=8 while B displays at y=240, and vice versa. Env layout: draw0 +0, disp0 +92, draw1 +132, disp1 +224 from 0x80076EE0. The flip at 0x8001ED60 reads [0x80075888], compares against draw0 and stores draw0+132 or draw0, i.e. a plain toggle; a scan of every reference to 0x80075888 finds 39 loads and exactly ONE store (0x8001ED98), so nothing else moves the buffer. The DRAWENV background-clear RGB (+25/26/27 and +157/158/159) is written from ~15 scene functions, which is why per-scene clear colour varies.

## What would falsify it

a second writer of [0x80075888] appearing once more overlays are recompiled, or a mode change that reprograms the envs to other than 512x240 (a PAL/NTSC or hi-res path would do it)
