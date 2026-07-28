---
id: C094
kind: claim
status: holds
created: 2026-07-29
tags: gpu,milestone
---

## Claim

THE PORT RENDERS CORRECTLY. First visual confirmation: the Insomniac Games logo over Spyro's 3D landscape, captured headless at 512x240 — recognisable geometry, correct colours, clean compositing.

## Evidence

REPL 'shotregion scratch/screenshots/b1.ppm 0 240 512 240' at guest frame 900: 93.3% non-black, 2126 distinct colours. Converted and viewed: the 'Developed by / Insomniac Games' logo quad composited over a 3D scene — lavender mountains, green hills, teal water, a blue-to-orange sky gradient. No speckling or truncation visible in this frame. The paired region (0,0) is 0.0% non-black with ONE colour, which is what made every earlier capture look black: content lives in draw1's buffer (y=248..472 per C068) and the earlier dumps read draw0's (y=8..232). Resolution matches C068's RE'd display env exactly.

## What would falsify it

one frame at one moment. Later scenes — especially in-level gameplay rather than a logo screen — could still show the speckling and horizontal truncation recorded in issue 0016, which this does not disprove.
