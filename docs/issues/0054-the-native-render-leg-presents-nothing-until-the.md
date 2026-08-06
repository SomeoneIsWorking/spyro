---
id: 54
title: The native render leg PRESENTS NOTHING until the display tail is owned — a producer's quads reach the queue and are never composited
status: resolved
symptom: PSXPORT_SPYRO_FRAME_LOOP=1 with the native render leg: the picture is frozen on the boot logo while the frame loop runs thousands of drawn frames. present_shot at any index inside that window returns the Universal-globe composite (16216 colours, bit-identical across 24 consecutive presents). presentskip reports presents=444 with rebuild_geom=3.
tags: render,native-leg,present,frame-loop,display-tail
created: 2026-08-06
updated: 2026-08-06
---

## Symptom

With `PSXPORT_SPYRO_FRAME_LOOP=1` and the NATIVE render leg (no `PSXPORT_RENDER_PSX`), the first
producer emitted correctly — `PSXPORT_DEBUG=rqflush` showed `n=1`/`n=2` flushes with sensible
y-ranges alternating between the two display buffers — and NOTHING appeared. Every present capture in
the producer's own active window was the same frozen boot-logo composite.

The trap: **a prim count is not a picture, and neither is a queue flush.** The queue was right, the
draw offsets were right, and the picture was a frame from two screens earlier.

## Root cause

Spyro's ONLY per-frame vblank wait lives in the render driver's DISPLAY TAIL
(`0x8001ED5C` / `0x8007CEE4`: `DrawSync(0)`, `VSync(0)`, the `>=2`-field throttle on
`[0x80075950]`/`[0x80075954]`, `PutDispEnv`, `PutDrawEnv`, `DrawOTag`). This port's
present, pad service, vblank callback and pacing all hang off that wait
(`game/core/vsync.cpp` `deliver_field`). The native leg skipped the whole tail — so no field was
ever spent, no present ever ran, and the composite stayed wherever boot left it.

MEASURED, before the fix: 1556 drawn frames on the native leg with **not one `presentskip` line
between them** under `PSXPORT_DEBUG=rqflush,presentskip` — an unbroken run of `drawFrame` flushes.
Total presents for the whole run: 444, of which `rebuild_geom=3`.

Two things had to be true at once and only one was, which is why it read as a producer bug:
the draw env was also frozen (nothing flipped `[0x80075888]`), so even a present would have
composited the buffer that was not on screen.

## Fix

`game/render/frame_env.cpp` — frontier `frame.own-render-driver` parts (1) and (2), game-side:
`nativeFrameBegin` flips the env and programs GP0 E3/E4/E5/E1/E2/E6 + the `isbg` fill from the
game's own DRAWENV; `nativeFrameEnd` spends the `>=2`-field throttle through
`spyro_deliver_field` (the same `deliver_field` the reference leg uses — ONE definition of a
field) and then sets GP1(05) from the env's DISPENV.

AFTER: presents=3548, `rebuild_geom=1942`, and the producer's picture is on screen
(claim C167's A/B).

## The reusable part

When a native producer 'draws nothing', check whether the port PRESENTED at all before touching the
producer. `PSXPORT_DEBUG=presentskip` answers it in one run and carries its own denominator;
`rqflush` proves the queue is fine. Reading those two together is what separated 'the producer is
wrong' from 'the frame never ended'.
