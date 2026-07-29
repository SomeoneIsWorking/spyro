---
id: I032
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

REPL `shot` — captures `[s_disp_x, s_disp_y]`. The region itself is CORRECT; the hazard is WHEN you
take it. `run N` returns after frame N has been presented AND the display origin has already flipped
to the next frame's target, so a `shot` issued at that prompt captures the buffer about to be drawn
into — a fresh clear — not the frame you just ran to.

Two rules make it reliable:

  * Take `vram <path>` (full 1024x512) when you need to know what the game actually drew. It is
    unambiguous: both buffers are in one image, so you can see which holds the frame instead of
    trusting an origin.
  * Pick an ODD frame. This port submits ~1600 prims on odd frames and 0 on even ones (double
    buffered), so a capture on an even frame is a picture of nothing regardless of buffer.

## Validated by

MEASURED, after an earlier version of this note got the cause wrong (see below). VRAM dumps at
f46501/46503/46505/46507 with `PSXPORT_DEBUG=gpu` logging the display origin per frame:

    f46501  distinct colours buf(0,0)=1723  buf(0,240)=   2   log says disp (0,0)
    f46503                   buf(0,0)=   2  buf(0,240)=1803   log says disp (0,240)
    f46505                   buf(0,0)=1896  buf(0,240)=   2   log says disp (0,0)
    f46507                   buf(0,0)=   2  buf(0,240)=1942   log says disp (0,240)

Four for four, the rendered image is in exactly the buffer the log says is displayed. So the display
region tracks the flip correctly and `present_window()` — which uses the same region — is fine.

## Known failure modes

FAILS SILENTLY: a flat capture and a renderer that draws nothing are the same picture. That is what
made it expensive on issue 0035.

CORRECTION, recorded because the wrong version of it was committed and would have sent the next
session to fix working code: this note first claimed "the display region is one flip out of step with
the draw region", i.e. an engine bug in the buffer flip, and said `present_window()` was affected
too. That was inference from a single mistimed capture, not a measurement. The table above refutes
it. The lesson is the one this project keeps re-learning — two captures disagreeing tells you
something is wrong, not WHICH thing, and the cheap experiment (dump both buffers, read the logged
origin) settles it in one run.
