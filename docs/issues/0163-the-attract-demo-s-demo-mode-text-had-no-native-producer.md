---
id: 163
title: The attract demo's "DEMO MODE" caption never drew because the native FIELD arm had no producer for 0x80018908
status: resolved
symptom: Retail shows gold "DEMO MODE" glyph Mobys bottom-centre during the attract demo; the port draws nothing there, although the twelve g_Hud Mobys draw
tags: spyro1,attract-demo,hud,text,shaded-moby,s030
created: 2026-10-01
updated: 2026-10-01
---

## Answer

Root cause: the native FIELD arm (`SpyroRenderer::renderScene`) REPLACES the guest's stage-0 draw and
composes each layer from its own producer. `game/render/frame/scene.cpp` classified `0x80018908` ("demo-mode
text", gate `g_DemoMode`) in its layer list, but no producer was ever written for it, so the guest never
built the glyph Mobys and the shaded pass had nothing to draw. It was not a screen-space-path problem
(d163fbd's), a refusal, or a cull: the glyphs did not exist.

Retail bytes (SCUS_942.28, text at file offset 0x800, RAM image built the PS-X EXE way, disassembled
with `external/psxport/tools/disasm.py`):

- gate `0x8001EFF0 lw $v0,0x5714($v0)` / `0x8001EFF8 beqz` (g_DemoMode `0x80075714`), `0x8001F000 jal
  0x80018908`, between `jal 0x80019300` (collectables, `0x8001EFE4`) and `jal 0x80019698` (actor pass,
  `0x8001F008`); the only call site of `0x80018908` in the image.
- `0x80018908`: spacing vector (0x10, 1, 0x1400) at `0x80018908-0x80018920`, position (0xC7, 0xC8,
  0x1100) at `0x80018924-0x80018938`, `$a0 = 0x80010AC0` (bytes `"DEMO MODE\0"`), `$a3 = 0x12`, shade `2`
  at `0x8001893C`, `jal 0x800181AC` at `0x80018964`. Wobble loop `0x8001898C-0x800189CC`: `sb
  (cos[(g_LevelTicks*4 + i*12) & 0xFF] >> 7), 0x46(moby)` (`lhu` of `0x8006CC78`, `g_LevelTicks`
  `0x800758C8`). `jal 0x80018880` at `0x800189D4` appends the arena to the shaded queue. Every glyph
  carries render radius 0xFF, so it takes the screen-space path d163fbd drew the g_Hud Mobys with.

Runtime evidence before the fix (attract demo, `g_DemoMode` = 1, between frames): the shaded queue held
only the g_Hud Mobys at `0x80077FEC..` and nothing else; no glyph centre with y = 200/201 was ever logged
on `shadedscreen` across a 420 s no-input run.

## Fix

`game/render/frame/scene/demo_text_scene.*` is the missing producer: a pure `plan(g_DemoMode)` (the retail constants
above through `hud_text::layoutCaption`) and `submit`, called in `frame_rendererScene` between the collectables
and the model chain. It reproduces the guest's arena and queue writes through `hud_text`, exactly as the
completed-gem text and the pause and tally captions already do (the shaded-queue scene reads its Mobys
from those words; this is not new guest state, it is the state `0x80018908` would have written). The
queue order is newest first, as `0x80018880` copies the arena.

The wobble existed twice (the completed-gem text with `ticks*4`, the tally with `ticks*2`) and the queue
append once inline; both are now `hud_text::wobble` and `hud_text::enqueueShaded`, used by all three
callers. The glyphs are arena Mobys, not `g_Hud` records, so `HudAnchor` leaves them at the widened
projection's centre, which is the correct class for a centred caption.

Tests: `demo_text_scene` (plan equals the retail constants and glyph classes; nothing outside the demo;
newest-first queue and wobble phase; atomic refusal on a full arena and on a full queue; append after
existing entries past a stale terminator) and `hud_text_builder` (wobble phase wrap and sign;
enqueue append and terminator; refusal when the terminator would not fit).

## Also found: the census dropped every HUD correction

`tools/hud_anchor_census.py`'s default route merges a gameplay log and a front-end log, and the merge
carried elements, refusals and frames but not `corrections`. On the default route every `g_Hud` Moby
correction was therefore unread: it printed `left-edge=0, centred=14, right-edge=0` and PASSed without
checking the gem, lives or key parts. `merge_logs` merges them now, and the selftest has two merged-log
cases (one correct, one wrong-signed that FAILs; shown to pass wrongly when the merge line is removed).
Over the same two logs the census now prints `left-edge=2, centred=16, right-edge=2`.

## Not covered

The key counter, treasure row, life orbs and completed-gem text have no reachable route here (they need a
key, treasure, life orbs, or a finished level). By code: the key is `g_Hud` Moby 11 on the screen-space
path (`hud-key`, right edge); the treasure row and life orbs are 2D sprites placed through `ui_anchor`;
the completed-gem text is arena glyph Mobys, which `HudAnchor` does not recognise, so at 16:9 it is
centred with the widened projection rather than anchored left as the gem counter it replaces is.
None of the four was observed drawing.
