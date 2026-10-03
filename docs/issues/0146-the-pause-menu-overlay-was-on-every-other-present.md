---
id: 0146
title: The pause menu's panel and border were on 4 of 8 presents because they were placed without the drawing offset
status: fixed
symptom: At both aspects the pause menu's 2D overlay showed on 2 presents, vanished for 2, and so on
  (4 of 8 at 16:9, 8 of 16 at 4:3; issue 0144 section 5). At 60 fps it would read as flicker.
tags: render,overlay,pause,double-buffer,fps60
created: 2026-09-29
updated: 2026-09-29
---

## Which it was: the port, not the guest

The guest submits the panel on every menu frame (0144 section 5: 33 of 33 menu frames, and
`PSXPORT_PRIMRGB` finds it on every census frame). So the other-frame gap was the port's.

`pause_menu_scene::submitPanel` and `submitBorder` emitted the recipe's coordinates as they were. Those
coordinates are the guest's own, relative to the GPU drawing offset (GP0 E5), and the hardware adds
that offset to every vertex. Spyro double-buffers by alternating the offset's y between 0 and 240,
while the draw-area clip `s_da_*` is absolute. On the buffer at y=240 the unoffset quad (y 67..176)
fell entirely outside the clip (240..479), so nothing was drawn. That is the 67 / -173 `display_bbox`
alternation 0144 recorded. Every other 2D producer in `game/render` already adds `gpu.s_off_y`
(for example `field_collectables.cpp`, `field_shadow_submitter.cpp`). The captions go through the
shaded pass, which places them itself.

## Fix

`pause_menu::placePanel` and `pause_menu::placeSegment` (`game/render/frame/scene/pause_menu_recipe.*`) put the
recipe into framebuffer space by adding the offset. Both scene submitters use them.
`test_pause_menu_recipe` pins the offset on both buffers (0 and 240) and that the border shades are
kept.

## Measured

16:9, `drive.py gameplay --tap start --after 60 --preseq 8 --env PSXPORT_PRIMRGB=0,56,192`, read by
:

| build | panel on | spans |
|---|---|---|
| main at e06beae | 4 of 8 | 232 |
| this fix, run 1 | **8 of 8** | 232 x6, 231 x2 |
| this fix, run 2 | **8 of 8** | 232 x8 |

Run 1's two 231 readings are the second buffer's presents. Column 226 is the border line, and it is not
panel fill. Its colour excess changes with the world behind it, which is the same one-column jitter
the 4:3 control showed as 232/233 in 0144. Columns 227..457 are identical in both buffers.

## Interpolated 60 fps: measured, the in-between presents carry the overlay

Shipping settings (`fps60=1`, 16:9) with `PSXPORT_DEBUG=fps60`, main at bb08875. The 8-present
`--preseq` window is 4 in-between presents (`slotA: in-between ... t=0.500`, fences f3488..f3491, all
logged between `preseq armed` and `preseq done`) and 4 real ones. finds the
panel on **8 of 8, all 232 columns**, so no present at 60 fps drops it. That is expected: the in-between
pass replays the same captured queue as the real present (`psxport runtime/psx/fps60.cpp`,
`Fps60::frame_commit`), so a 2D item submitted once per logic frame is drawn in both.
