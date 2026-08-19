---
id: 68
title: Untextured polygons were drawn with no draw-area clip: sky triangles from one frame land in the other framebuffer, and geometry overwrites the texture atlas
status: resolved
symptom: A blue/sky-coloured band along the bottom edge of the picture that belongs to no visible object; tan/garbage bands at the top and bottom of the frame instead of clean letterboxing; rendered geometry visible beyond x=511 in a VRAM dump, on top of the texture atlas
tags: gpu,render,glitch,framework,vulkan
created: 2026-08-19
updated: 2026-08-19
---

ROOT CAUSE (framework psxport 7782da9c). The PSX GPU clips EVERY primitive to the drawing area set by GP0(E3)/GP0(E4). `tritex.frag` enforced that with a discard; `tri.frag` had no clip at all — no da attribute, no test — and `render_queue.cpp` routes `mode==3` (untextured) to `gpu_vk_draw_tri`, whose signature did not carry the draw area. The irony is that `RqItem` had been carrying `da_*` the whole time and passing it to the textured call on the very next line.

HOW IT WAS PINNED DOWN, since the coordinates alone cannot do it. `PSXPORT_PRIMDUMP` showed prims at y=223 while the frame was drawing into the buffer at offset y=240 — but a bbox in post-offset VRAM space looks the same whether the guest asked for it there or the clip failed. Adding dax0/day0/dax1/day1/offx/offy columns to the dump settled it in one run: draw area (0,248)-(511,471), offset (0,240), and 30 prims reaching y=223 — 25 rows above their own clip. ALL 30 were tex=0. Not one textured prim escaped.

FIX. TriVtx carries da[4] like TexVtx; tri.vert passes it; tri.frag discards outside it, dividing gl_FragCoord by the ires scale first exactly as tritex.frag does; the untextured pipeline gains that fragment uniform. The wide-margin fill passes the FULL canvas on purpose — it paints the strip OUTSIDE the guest draw area, so clipping it there would erase its whole reason to exist.

EVIDENCE. Title screen f7501 md5 5ce239fc -> d93bcfe2: the blue band is gone and both framebuffers letterbox correctly (Spyro's draw area is rows 8..231 of a 240-row buffer). Field f6001 b6223ab7 -> 31a6095c, same. VRAM beyond x=511 is texture atlas again instead of overpainted geometry — so this was corrupting TEXTURES too, not only the visible frame. Framework suite 60/60, gate 13 PASS.

NOT CAUSED BY THE DEPTH WORK, checked before blaming it: with ProjPrim retention reverted to the old kGens=2 the same frame is byte-identical (5ce239fc both ways) at 1.67% vs 63.91% depth coverage. The glitch predates all of it.
