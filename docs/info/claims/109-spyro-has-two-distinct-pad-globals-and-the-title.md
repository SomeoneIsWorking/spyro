---
id: C109
kind: claim
status: falsified
created: 2026-07-29
tags: input,pad
falsified_on: 2026-07-29
---

## Claim

Spyro has TWO distinct pad globals and the title screen's advance path tests the EDGE one, not the held one. [0x80077380] is the HELD word (0xFFFF0800 continuously while START is down); [0x80077378] is the 'newly pressed' EDGE word, non-zero for exactly two frames per press. Both are written every frame by pc=0x8006B64C ra=0x80053D50. Consequence: PSXPORT_FORCE_BUTTONS, which holds a button from boot, can NEVER satisfy an edge test — so every prior conclusion of the form 'the menu does not want START' drawn from a FORCE_BUTTONS run is unfounded, because the input never reached the branch being tested.

## Evidence

PSXPORT_WWATCH=0x80077378,0x8007737C over a run with REPL 'press start': 241180 stores to that word, of which exactly three are non-zero — 0xFFFF0000 at f436 (pad init) and 0x00000800 at f1301 and f1302, the two frames spanning one press. A REPL read at the same moment shows [0x80077380] = 0xFFFF0800 held continuously. 0x800 is bit 11 = START, matching the game's internal encoding recorded earlier in issue 0027.

## What would falsify it

A run in which PSXPORT_FORCE_BUTTONS produces a non-zero [0x80077378] on any frame after pad init, which would mean the port synthesises edges from a held button and the two words are not distinguishable this way.

## FALSIFIED 2026-07-29

The pad-global half is right; the FORCE_BUTTONS half is WRONG and I inferred it from the name instead of reading pad_input.cpp. PSXPORT_FORCE_BUTTONS does NOT hold a button — Pad::serviceFrame PULSES it, 'setButtons((mFc % 32u) < 8u ? mForceMask : PAD_NONE)', 8 frames down and 24 up, and the code comment states the reason outright: 'so each press is a fresh EDGE the game's current&~prev input logic actually sees — a continuous hold would edge only once.' So FORCE_BUTTONS generates repeated edges and is the STRONGER menu-driving instrument, while the REPL's 'press' (a persistent hold, one edge) is the weaker one. I had it exactly backwards, and it produced a false negative: holding via the REPL from boot never reaches sub=1, while FORCE_BUTTONS=FFF7 reaches it at f835 reproducibly. The surviving, re-verified part is recorded as C110.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
