---
id: I024
kind: instrument
status: trusted
created: 2026-07-29
---

## Instrument

tools/ppm_look.py — summarise a VRAM capture (coverage + distinct colours) and convert it to PNG

## Validated by

Reproduces the numbers that settled issue 0018 by hand: the empty buffer at (0,0) reports 0.0% / 1 colour, the real frame at (0,240) reports 93.3% / 2126 colours. The distinct-colour count is the discriminator that matters — one colour means an empty buffer or a dead read, thousands means a real frame — and it is exactly the distinction that got missed when all-black captures were read as 'the port renders black' rather than 'wrong buffer'.

## Known failure modes

(none recorded yet)
